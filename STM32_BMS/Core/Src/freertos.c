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
#include <string.h>
#include <stdio.h>
extern UART_HandleTypeDef huart1;   // 声明外部串口句柄
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
// 与ZET6保持完全一致的结构体
typedef struct {
    uint32_t id;
    uint8_t len;
    uint8_t data[8];
} CanMsgTypeDef;
/* USER CODE END PTD */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
// 模拟数据变量（可以在调试器中随时修改）
static uint16_t sim_voltage = 3200;   // 320.0V（单位：0.1V）
static int16_t sim_current = 123;    // 12.3A，乘10
static int8_t sim_temperature = 25;  // 25℃
static uint8_t sim_enable_send = 1;  // 1=发送 0=停止（模拟通信丢失）
/* USER CODE END Variables */

/* Definitions for tasks */
osThreadId_t Task_BMS_SendHandle;
const osThreadAttr_t Task_BMS_Send_attributes = {
  .name = "Task_BMS_Send",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
osThreadId_t Task_HeartBeatHandle;
const osThreadAttr_t Task_HeartBeat_attributes = {
  .name = "Task_HeartBeat",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Function prototypes */
void StartTask_BMS_Send(void *argument);
void StartTask_HeartBeat(void *argument);

void MX_FREERTOS_Init(void);

/**
  * @brief FreeRTOS initialization
  */
void MX_FREERTOS_Init(void)
{
  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* 创建任务 */
  Task_BMS_SendHandle = osThreadNew(StartTask_BMS_Send, NULL, &Task_BMS_Send_attributes);
  Task_HeartBeatHandle = osThreadNew(StartTask_HeartBeat, NULL, &Task_HeartBeat_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* 如需添加其他任务，可在此处添加 */
  /* USER CODE END RTOS_THREADS */
}

/* 心跳任务：PC13 LED 闪烁，500ms 周期 */
void StartTask_HeartBeat(void *argument)
{
  /* USER CODE BEGIN StartTask_HeartBeat */
  for(;;)
  {
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    osDelay(500);
  }
  /* USER CODE END StartTask_HeartBeat */
}

/* BMS 发送任务：每秒生成一组数据，打包成 AA 55 帧通过串口发送 */
void StartTask_BMS_Send(void *argument)
{
  /* USER CODE BEGIN StartTask_BMS_Send */
  CanMsgTypeDef msg;
  uint8_t tx_buf[15];
  // 去掉 counter 变量，不需要了

  for(;;)
  {
    if (sim_enable_send)
    {
      // 固定模拟数据（不变化）
      sim_voltage = 3200;   // 320.0V（正常范围）
      sim_current = 123;    // 12.3A
      sim_temperature = 25; // 25℃

      // 手动模拟异常示例（取消注释即可测试）：
      // sim_voltage = 5000;  // 过压 500.0V
      // sim_temperature = 70; // 过温
      //sim_enable_send = 0;  // 停止发送

      // 2. 填充结构体
      msg.id = 0x00000123;
      msg.len = 8;
      msg.data[0] = (uint8_t)(sim_voltage & 0xFF);
      msg.data[1] = (uint8_t)((sim_voltage >> 8) & 0xFF);
      msg.data[2] = (uint8_t)(sim_current & 0xFF);
      msg.data[3] = (uint8_t)((sim_current >> 8) & 0xFF);
      msg.data[4] = (uint8_t)sim_temperature;
      msg.data[5] = 0x00;
      msg.data[6] = 0x00;
      msg.data[7] = 0x00;

      // 3. 打包成 ZET6 期望的 15 字节帧：AA 55 + ID(4) + LEN(1) + DATA(8)
      tx_buf[0] = 0xAA;
      tx_buf[1] = 0x55;
      memcpy(&tx_buf[2], &msg.id, 4);
      tx_buf[6] = msg.len;
      memcpy(&tx_buf[7], msg.data, 8);

      // 4. 通过串口1发送
      HAL_UART_Transmit(&huart1, tx_buf, 15, 100);
    }

    osDelay(1000); // 每秒发送一次
  }
  /* USER CODE END StartTask_BMS_Send */
}

/* USER CODE BEGIN Application */
/* USER CODE END Application */
