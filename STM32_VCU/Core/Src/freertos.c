/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  */
/* USER CODE END Header */
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* USER CODE BEGIN Includes */
#include "oled.h"
#include <string.h>
#include <stdio.h>
extern UART_HandleTypeDef huart1;
/* USER CODE END Includes */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
typedef struct {
    uint32_t id;
    uint8_t len;
    uint8_t data[8];
} CanMsgTypeDef;

typedef struct {
    uint16_t voltage;
    int16_t current;
    int8_t temperature;
    uint8_t fault_flag;
    uint32_t last_msg_tick;
} BmsData_t;

static BmsData_t g_bms_data = {0};
static osMutexId_t g_bms_mutex;

// 串口中断环形缓冲
#define UART_RING_BUF_SIZE 256
static uint8_t uart_ring_buf[UART_RING_BUF_SIZE];
static volatile uint16_t wr_idx = 0;
static volatile uint16_t rd_idx = 0;
static uint8_t parse_buf[15];
static uint8_t it_rx_ch;  // 中断接收静态缓存，禁止局部变量
/* USER CODE END Variables */

/* Definitions for tasks and queue */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
osThreadId_t Task_MonitorHandle;
const osThreadAttr_t Task_Monitor_attributes = {
  .name = "Task_Monitor",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
osThreadId_t Task_RecvMsgHandle;
const osThreadAttr_t Task_RecvMsg_attributes = {
  .name = "Task_RecvMsg",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
osThreadId_t Task_BMS_ProcesHandle;
const osThreadAttr_t Task_BMS_Proces_attributes = {
  .name = "Task_BMS_Proces",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
osThreadId_t Task_RespHandle;
const osThreadAttr_t Task_Resp_attributes = {
  .name = "Task_Resp",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
osMessageQueueId_t MsgQueueHandle;
const osMessageQueueAttr_t MsgQueue_attributes = {
  .name = "MsgQueue"
};

/* Function prototypes */
void StartDefaultTask(void *argument);
void StartTask_Monitor(void *argument);
void StartTask_RecvMsg(void *argument);
void StartTask_BMS_Process(void *argument);
void StartTask_Resp(void *argument);

void MX_FREERTOS_Init(void);

void MX_FREERTOS_Init(void)
{
  /* USER CODE BEGIN 2 */
  g_bms_mutex = osMutexNew(NULL);
  /* USER CODE END 2 */

  MsgQueueHandle = osMessageQueueNew(32, sizeof(CanMsgTypeDef), &MsgQueue_attributes);

  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);
  Task_MonitorHandle = osThreadNew(StartTask_Monitor, NULL, &Task_Monitor_attributes);
  Task_RecvMsgHandle = osThreadNew(StartTask_RecvMsg, NULL, &Task_RecvMsg_attributes);
  Task_BMS_ProcesHandle = osThreadNew(StartTask_BMS_Process, NULL, &Task_BMS_Proces_attributes);
  Task_RespHandle = osThreadNew(StartTask_Resp, NULL, &Task_Resp_attributes);

  /* USER CODE BEGIN 3 */
  // 启动串口单字节中断接收，使用静态缓存 it_rx_ch
  HAL_UART_Receive_IT(&huart1, &it_rx_ch, 1);
  /* USER CODE END 3 */
}

/* 默认任务：注入测试报文一次 */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  CanMsgTypeDef testMsg;
  testMsg.id = 0x12345678;
  testMsg.len = 8;
  testMsg.data[0] = 0x6D; testMsg.data[1] = 0x01;
  testMsg.data[2] = 0x7B; testMsg.data[3] = 0x00;
  testMsg.data[4] = 0x2D; testMsg.data[5] = 0x00;
  testMsg.data[6] = 0x00; testMsg.data[7] = 0x00;

  printf("Inject test message\r\n");
  if (osMessageQueuePut(MsgQueueHandle, &testMsg, 0, 0) != osOK) {
      printf("Put failed\r\n");
  }
  vTaskDelete(NULL);
  /* USER CODE END StartDefaultTask */
}

/* 监控任务：LED、OLED、超时检测 */
void StartTask_Monitor(void *argument)
{
  /* USER CODE BEGIN StartTask_Monitor */
  char line[20];
  uint32_t now;
  static uint8_t display_cnt = 0;

  for(;;) {
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);

    osMutexAcquire(g_bms_mutex, osWaitForever);
    uint16_t volt = g_bms_data.voltage;
    int16_t curr = g_bms_data.current;
    int8_t temp = g_bms_data.temperature;
    uint32_t last_tick = g_bms_data.last_msg_tick;
    osMutexRelease(g_bms_mutex);

    now = osKernelGetTickCount();
    if (last_tick != 0 && (now - last_tick) > 3000) {
      osMutexAcquire(g_bms_mutex, osWaitForever);
      g_bms_data.fault_flag |= 0x10;
      osMutexRelease(g_bms_mutex);
    }

    // OLED 每 2 次循环（约 1 秒）刷新一次
    if (++display_cnt >= 2) {
      display_cnt = 0;
      OLED_ShowString(1, 1, "VCU BMS");

      uint8_t current_fault;
      osMutexAcquire(g_bms_mutex, osWaitForever);
      current_fault = g_bms_data.fault_flag;
      osMutexRelease(g_bms_mutex);

      if (current_fault & 0x10) {
        // 超时状态：数值失效，故障位显示完整值
        OLED_ShowString(2, 1, "V:------");
        OLED_ShowString(3, 1, "I:------");
        sprintf(line, "T:--C F:%02X", current_fault);
        OLED_ShowString(4, 1, line);
      } else {
        sprintf(line, "V:%u.%uV", volt/10, volt%10);
        OLED_ShowString(2, 1, line);
        sprintf(line, "I:%+d.%dA", curr/10, curr%10);
        OLED_ShowString(3, 1, line);
        sprintf(line, "T:%dC F:%02X", temp, current_fault);
        OLED_ShowString(4, 1, line);
      }
    }
    osDelay(500);
  }
  /* USER CODE END StartTask_Monitor */
}

/* 接收任务：消费环形缓冲区字节流，解析AA 55帧，投递消息队列 */
void StartTask_RecvMsg(void *argument)
{
  /* USER CODE BEGIN StartTask_RecvMsg */
  uint8_t parse_idx = 0;
  CanMsgTypeDef msg;
  osStatus_t status;

  for(;;)
  {
    // 使用临界区保护读取 wr_idx 和更新 rd_idx，避免中断干扰
    uint16_t local_wr, local_rd;
    taskENTER_CRITICAL();
    local_wr = wr_idx;
    local_rd = rd_idx;
    taskEXIT_CRITICAL();

    while( local_wr != local_rd )
    {
      uint8_t ch = uart_ring_buf[local_rd];
      local_rd = (local_rd + 1) % UART_RING_BUF_SIZE;

      if(parse_idx == 0)
      {
        if(ch == 0xAA) parse_idx = 1;
      }
      else if(parse_idx == 1)
      {
        if(ch == 0x55) parse_idx = 2;
        else parse_idx = 0;
      }
      else
      {
        parse_buf[parse_idx++] = ch;
        if(parse_idx >= 15)
        {
          memcpy(&msg.id, &parse_buf[2], 4);
          msg.len = parse_buf[6];
          memcpy(msg.data, &parse_buf[7], 8);
          status = osMessageQueuePut(MsgQueueHandle, &msg, 0, 5U);
          if(status != osOK) printf("[RecvMsg] queue full drop frame\r\n");
          parse_idx = 0;
        }
        if(parse_idx > 15) parse_idx = 0;
      }
    }
    // 将更新后的 rd_idx 写回（临界区保护）
    taskENTER_CRITICAL();
    rd_idx = local_rd;
    taskEXIT_CRITICAL();

    osDelay(2);
  }
  /* USER CODE END StartTask_RecvMsg */
}

/* 处理任务：解析数据，更新全局变量 */
void StartTask_BMS_Process(void *argument)
{
  /* USER CODE BEGIN StartTask_BMS_Process */
  CanMsgTypeDef msg;
  for(;;)
  {
    if (osMessageQueueGet(MsgQueueHandle, &msg, NULL, 1000) == osOK)
    {
      uint16_t volt = (uint16_t)(msg.data[0] | (msg.data[1] << 8));
      int16_t curr = (int16_t)(msg.data[2] | (msg.data[3] << 8));
      int8_t temp = (int8_t) msg.data[4];

      osMutexAcquire(g_bms_mutex, osWaitForever);
      g_bms_data.voltage = volt;
      g_bms_data.current = curr;
      g_bms_data.temperature = temp;
      g_bms_data.last_msg_tick = osKernelGetTickCount();

      g_bms_data.fault_flag &= ~0x10;          // 清除超时标志
      g_bms_data.fault_flag &= ~0x0F;          // 清除其他故障（根据新数据重新计算）
      if (volt > 4000) g_bms_data.fault_flag |= 0x01;
      if (volt < 2000) g_bms_data.fault_flag |= 0x02;
      if (temp > 60)   g_bms_data.fault_flag |= 0x04;
      if (curr > 300 || curr < -300) g_bms_data.fault_flag |= 0x08;
      osMutexRelease(g_bms_mutex);
    }
  }
  /* USER CODE END StartTask_BMS_Process */
}

/* 响应任务：打印状态 */
void StartTask_Resp(void *argument)
{
  /* USER CODE BEGIN StartTask_Resp */
  for(;;) {
    osMutexAcquire(g_bms_mutex, osWaitForever);
    uint16_t volt = g_bms_data.voltage;
    int16_t curr = g_bms_data.current;
    int8_t temp = g_bms_data.temperature;
    uint8_t fault = g_bms_data.fault_flag;
    osMutexRelease(g_bms_mutex);

    // 根据超时故障位决定打印有效数据还是 "------"
    if (fault & 0x10) {
      printf("V=------ I=------ T=------ Fault=0x%02X\r\n", fault);
    } else {
      printf("V=%u.%uV I=%d.%dA T=%dC Fault=0x%02X\r\n",
             volt/10, volt%10, curr/10, curr%10, temp, fault);
    }

    if (fault & 0x01) printf("  Over Voltage!!\r\n");
    if (fault & 0x02) printf("  Under Voltage!!\r\n");
    if (fault & 0x04) printf("  Over Temperature!!\r\n");
    if (fault & 0x08) printf("  Over Current!!\r\n");
    if (fault & 0x10) printf("  COMM TIMEOUT!!\r\n");

    osDelay(1000);
  }
  /* USER CODE END StartTask_Resp */
}

/* USER CODE BEGIN 4 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if(huart->Instance == USART1)
  {
    uart_ring_buf[wr_idx] = it_rx_ch;
    wr_idx = (wr_idx + 1) % UART_RING_BUF_SIZE;
    HAL_UART_Receive_IT(&huart1, &it_rx_ch, 1);
  }
}

/* 可选：栈溢出检测钩子（需要在 CubeMX 中启用 configCHECK_FOR_STACK_OVERFLOW = 2） */
// void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
// {
//     for(;;) {
//         HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
//         HAL_Delay(100);
//     }
// }
/* USER CODE END 4 */
