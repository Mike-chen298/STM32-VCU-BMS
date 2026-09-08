/* USER CODE BEGIN Header */
/**
---
- File Name          : freertos.c
- Description        : Code for freertos applications
---
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "oled.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "bms_config.h"
#include "bms_app.h"
#include "usart.h"
#include "can.h"
#include "can_driver.h"
/* USER CODE END Includes */
/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */
/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */
/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */
/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
// BmsData_t 已经在 bms_app.h 中定义，此处不再重复
static BmsData_t g_bms_data = {0};
static osMutexId_t g_bms_mutex;

// 串口中断环形缓冲（阶段1?3历史备份，保留不删除）
#define UART_RING_BUF_SIZE 256
static uint8_t uart_ring_buf[UART_RING_BUF_SIZE];
static volatile uint16_t wr_idx = 0;
static volatile uint16_t rd_idx = 0;
static uint8_t parse_buf[15];
static uint8_t it_rx_ch;  // 中断接收静态缓存，禁止局部变量

extern osMessageQueueId_t MsgQueueHandle;
/* USER CODE END Variables */

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Task_Monitor */
osThreadId_t Task_MonitorHandle;
const osThreadAttr_t Task_Monitor_attributes = {
  .name = "Task_Monitor",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Task_RecvMsg */
osThreadId_t Task_RecvMsgHandle;
const osThreadAttr_t Task_RecvMsg_attributes = {
  .name = "Task_RecvMsg",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Task_BMS_Proces */
osThreadId_t Task_BMS_ProcesHandle;
const osThreadAttr_t Task_BMS_Proces_attributes = {
  .name = "Task_BMS_Proces",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for Task_Resp */
osThreadId_t Task_RespHandle;
const osThreadAttr_t Task_Resp_attributes = {
  .name = "Task_Resp",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Task_BMS_Simulate */
osThreadId_t Task_BMS_SimulateHandle;
const osThreadAttr_t Task_BMS_Simulate_attributes = {
  .name = "Task_BMS_Simulate",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Definitions for MsgQueue */
osMessageQueueId_t MsgQueueHandle;
const osMessageQueueAttr_t MsgQueue_attributes = {
  .name = "MsgQueue"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void StartTask_BMS_Simulate(void *argument);
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void StartTask_Monitor(void *argument);
void StartTask_RecvMsg(void *argument);
void StartTask_BMS_Process(void *argument);
void StartTask_Resp(void *argument);
void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
    g_bms_mutex = osMutexNew(NULL);
  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of MsgQueue */
  MsgQueueHandle = osMessageQueueNew (32, 16, &MsgQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of Task_Monitor */
  Task_MonitorHandle = osThreadNew(StartTask_Monitor, NULL, &Task_Monitor_attributes);

  /* creation of Task_RecvMsg */
  Task_RecvMsgHandle = osThreadNew(StartTask_RecvMsg, NULL, &Task_RecvMsg_attributes);

  /* creation of Task_BMS_Proces */
  Task_BMS_ProcesHandle = osThreadNew(StartTask_BMS_Process, NULL, &Task_BMS_Proces_attributes);

  /* creation of Task_Resp */
  Task_RespHandle = osThreadNew(StartTask_Resp, NULL, &Task_Resp_attributes);

  /* creation of Task_BMS_Simulate 模拟BMS发送任务，回环模式使用 */
  Task_BMS_SimulateHandle = osThreadNew(StartTask_BMS_Simulate, NULL, &Task_BMS_Simulate_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  HAL_UART_Receive_IT(&huart1, &it_rx_ch, 1);
  /* USER CODE END RTOS_EVENTS */
}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
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

/* USER CODE BEGIN Header_StartTask_Monitor */
/**
* @brief Function implementing the Task_Monitor thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask_Monitor */
void StartTask_Monitor(void *argument)
{
  /* USER CODE BEGIN StartTask_Monitor */
  can_driver_init(); //CAN驱动初始化
  char line[20];
  uint32_t now;
  static uint8_t display_cnt = 0;
  static uint32_t stat_tick = 0;
  for(;;) {
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);

    stat_tick ++;
    if(stat_tick >=4)
    {
        stat_tick =0;
        printf("CAN stat: rx_total:%u drop:%u\r\n", can_rx_total, can_drop_count);
    }

    BmsData_t data;
    osMutexAcquire(g_bms_mutex, osWaitForever);
    data = g_bms_data;
    osMutexRelease(g_bms_mutex);
    now = osKernelGetTickCount();
    if (data.last_msg_tick != 0 && (now - data.last_msg_tick) > BMS_TIMEOUT_MS) {
      osMutexAcquire(g_bms_mutex, osWaitForever);
      g_bms_data.fault_flag |= 0x10;
      osMutexRelease(g_bms_mutex);
      data.fault_flag |= 0x10;
    }
    if (++display_cnt >= 2) {
      display_cnt = 0;
      uint8_t current_fault;
      osMutexAcquire(g_bms_mutex, osWaitForever);
      current_fault = g_bms_data.fault_flag;
      osMutexRelease(g_bms_mutex);
      data.fault_flag = current_fault;
      BMS_DisplayData(&data, line);
    }
    osDelay(500);
  }
  /* USER CODE END StartTask_Monitor */
}

/* USER CODE BEGIN Header_StartTask_RecvMsg */
/**
* @brief Function implementing the Task_RecvMsg thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask_RecvMsg */
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

/* USER CODE BEGIN Header_StartTask_BMS_Process */
/**
* @brief Function implementing the Task_BMS_Proces thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask_BMS_Process */
void StartTask_BMS_Process(void *argument)
{
  /* USER CODE BEGIN StartTask_BMS_Process */
  CanMsgTypeDef msg;
  for(;;) {
    if (osMessageQueueGet(MsgQueueHandle, &msg, NULL, osWaitForever) == osOK) {
      BmsData_t new_data;
      new_data.voltage  = (uint16_t)(msg.data[0] | (msg.data[1] << 8));
      new_data.current  = (int16_t)(msg.data[2] | (msg.data[3] << 8));
      new_data.temperature = (int8_t)msg.data[4];
      new_data.last_msg_tick = osKernelGetTickCount();
      osMutexAcquire(g_bms_mutex, osWaitForever);
      g_bms_data.voltage = new_data.voltage;
      g_bms_data.current = new_data.current;
      g_bms_data.temperature = new_data.temperature;
      g_bms_data.last_msg_tick = new_data.last_msg_tick;
      g_bms_data.fault_flag &= ~0x10;
      BMS_UpdateFault(&g_bms_data);
      osMutexRelease(g_bms_mutex);
    }
  }
  /* USER CODE END StartTask_BMS_Process */
}

/* USER CODE BEGIN Header_StartTask_Resp */
/**
* @brief Function implementing the Task_Resp thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask_Resp */
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

/* USER CODE BEGIN Header_StartTask_BMS_Simulate */
/**
* @brief 回环模式临时模拟BMS发送任务；硬件联调时屏蔽此任务
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask_BMS_Simulate */
void StartTask_BMS_Simulate(void *argument)
{
    /* USER CODE BEGIN StartTask_BMS_Simulate */
    for(;;)
    {
        CanMsgTypeDef sim_msg;
        sim_msg.id = 0x18000501U;
        sim_msg.len = 8;
        //模拟正常工况电压 307.2V
        sim_msg.data[0] = 0x00;
        sim_msg.data[1] = 0x0C;
        sim_msg.data[2] = 0x00;
        sim_msg.data[3] = 0x00;
        sim_msg.data[4] = 25;
        sim_msg.data[5] = 0;
        sim_msg.data[6] = 0;
        sim_msg.data[7] = 0;

        can_send(&sim_msg);
        printf("[Sim?BMS] send CAN id:0x%08X\r\n", sim_msg.id);

        osDelay(100);
    }
    /* USER CODE END StartTask_BMS_Simulate */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if(huart->Instance == USART1)
  {
    uart_ring_buf[wr_idx] = it_rx_ch;
    wr_idx = (wr_idx + 1) % UART_RING_BUF_SIZE;
    HAL_UART_Receive_IT(&huart1, &it_rx_ch, 1);
  }
}
/* USER CODE END Application */
