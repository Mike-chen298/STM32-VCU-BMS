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
#include "OLED.h"
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

void MX_FREERTOS_Init(void) {
  g_bms_mutex = osMutexNew(NULL);
  MsgQueueHandle = osMessageQueueNew(10, sizeof(CanMsgTypeDef), &MsgQueue_attributes);

  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);
  Task_MonitorHandle = osThreadNew(StartTask_Monitor, NULL, &Task_Monitor_attributes);
  Task_RecvMsgHandle = osThreadNew(StartTask_RecvMsg, NULL, &Task_RecvMsg_attributes);
  Task_BMS_ProcesHandle = osThreadNew(StartTask_BMS_Process, NULL, &Task_BMS_Proces_attributes);
  Task_RespHandle = osThreadNew(StartTask_Resp, NULL, &Task_Resp_attributes);
}

/* 默认任务：注入测试报文一次 */
void StartDefaultTask(void *argument) {
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
}

/* 监控任务：LED、OLED、超时检测 */
void StartTask_Monitor(void *argument) {
  char line[20];
  uint32_t now;
  static uint8_t display_cnt = 0;

  for(;;) {
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);

    osMutexAcquire(g_bms_mutex, osWaitForever);
    uint16_t volt = g_bms_data.voltage;
    int16_t curr = g_bms_data.current;
    int8_t temp = g_bms_data.temperature;
    uint8_t fault = g_bms_data.fault_flag;
    uint32_t last_tick = g_bms_data.last_msg_tick;
    osMutexRelease(g_bms_mutex);

    now = osKernelGetTickCount();
    if (last_tick != 0 && (now - last_tick) > 3000) {
      osMutexAcquire(g_bms_mutex, osWaitForever);
      g_bms_data.fault_flag |= 0x10;
      fault = g_bms_data.fault_flag;
      osMutexRelease(g_bms_mutex);
    }

    if (++display_cnt >= 10) {
      display_cnt = 0;
      OLED_ShowString(1, 1, "VCU BMS");
      sprintf(line, "V:%u.%uV", volt/10, volt%10);
      OLED_ShowString(2, 1, line);
      sprintf(line, "I:%+d.%dA", curr/10, curr%10);
      OLED_ShowString(3, 1, line);
      sprintf(line, "T:%dC F:%02X", temp, fault);
      OLED_ShowString(4, 1, line);
    }
    osDelay(500);
  }
}

/* 接收任务：轮询串口，组帧投递 */
void StartTask_RecvMsg(void *argument) {
  uint8_t frame[15];
  CanMsgTypeDef msg;
  osStatus_t status;

  for(;;) {
    if (HAL_UART_Receive(&huart1, frame, 15, 20) == HAL_OK) {  // 超时改成20ms
      if (frame[0] == 0xAA && frame[1] == 0x55) {
        memcpy(&msg.id, &frame[2], 4);
        msg.len = frame[6];
        memcpy(msg.data, &frame[7], 8);
        status = osMessageQueuePut(MsgQueueHandle, &msg, 0, 0);
        if (status != osOK) printf("[RecvMsg] queue full\r\n");
      }
    }
    osDelay(1);  // ← 加这行，主动让出CPU 1ms
  }
}

/* 处理任务：解析数据，更新全局变量 */
void StartTask_BMS_Process(void *argument) {
  CanMsgTypeDef msg;
  for(;;) {
    if (osMessageQueueGet(MsgQueueHandle, &msg, NULL, 1000) == osOK) {
      uint16_t volt = (uint16_t)(msg.data[0] | (msg.data[1] << 8));
      int16_t curr = (int16_t)(msg.data[2] | (msg.data[3] << 8));
      int8_t temp = (int8_t)msg.data[4];

      osMutexAcquire(g_bms_mutex, osWaitForever);
      g_bms_data.voltage = volt;
      g_bms_data.current = curr;
      g_bms_data.temperature = temp;
      g_bms_data.last_msg_tick = osKernelGetTickCount();

      g_bms_data.fault_flag = 0;
      if (volt > 4000) g_bms_data.fault_flag |= 0x01;  // ✅ 4000 = 400.0V
      if (volt < 2000) g_bms_data.fault_flag |= 0x02;  // ✅ 2000 = 200.0V
      if (temp > 60)   g_bms_data.fault_flag |= 0x04;  // 这个是对的
      if (curr > 300 || curr < -300) g_bms_data.fault_flag |= 0x08; // 这个是对的，300=30A

      osMutexRelease(g_bms_mutex);
    }
  }
}

/* 响应任务：打印状态 */
void StartTask_Resp(void *argument) {
  for(;;) {
    osMutexAcquire(g_bms_mutex, osWaitForever);
    uint16_t volt = g_bms_data.voltage;
    int16_t curr = g_bms_data.current;
    int8_t temp = g_bms_data.temperature;
    uint8_t fault = g_bms_data.fault_flag;
    osMutexRelease(g_bms_mutex);

    printf("V=%u.%uV I=%d.%dA T=%dC Fault=0x%02X\r\n",
           volt/10, volt%10, curr/10, curr%10, temp, fault);

    if (fault & 0x01) printf("  Over Voltage!!\r\n");
    if (fault & 0x02) printf("  Under Voltage!!\r\n");
    if (fault & 0x04) printf("  Over Temperature!!\r\n");
    if (fault & 0x08) printf("  Over Current!!\r\n");
    if (fault & 0x10) printf("  COMM TIMEOUT!!\r\n");

    osDelay(1000);
  }
}
