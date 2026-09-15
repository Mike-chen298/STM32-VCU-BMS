/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
#include "can.h"           // 【新增】HAL CAN句柄hcan
#include "can_driver.h"    // 【新增】can_send、can_driver_init
extern UART_HandleTypeDef huart1;   // 声明外部串口句柄
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* CanMsgTypeDef 在 can_driver.h 中定义 */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
// 模拟数据变量（可以在调试器中随时修改）
static uint16_t sim_voltage = 3200;   // 320.0V（单位：0.1V）
static int16_t sim_current = 123;    // 12.3A，乘10
static int8_t sim_temperature = 25;  // 25℃
static uint8_t sim_enable_send = 1;  // 1=发送 0=停止（模拟通信丢失）
static uint32_t uds_test_cnt = 0;
static uint8_t uds_test_idx = 0;

/* UDS自动测试用例表 */
typedef struct {
    const char *name;
    uint8_t data[8];
} UDS_Test_t;

static const UDS_Test_t uds_tests[] = {
    /* 0 */ {"0x22 read voltage(F190)",      {0x03,0x22,0xF1,0x90,0,0,0,0}},
    /* 1 */ {"0x22 read current(F191)",      {0x03,0x22,0xF1,0x91,0,0,0,0}},
    /* 2 */ {"0x22 read temperature(F192)",  {0x03,0x22,0xF1,0x92,0,0,0,0}},
    /* 3 */ {"0x22 read fault(F193)",        {0x03,0x22,0xF1,0x93,0,0,0,0}},
    /* 4 */ {"0x22 read invalid DID(FFFF)",  {0x03,0x22,0xFF,0xFF,0,0,0,0}},
    /* 5 */ {"0x10 switch extended session", {0x02,0x10,0x03,0,0,0,0,0}},
    /* 6 */ {"0x2E write voltage=330.0V",    {0x05,0x2E,0xF1,0x90,0xE4,0x0C,0,0}},
    /* 7 */ {"0x22 read voltage(verify)",    {0x03,0x22,0xF1,0x90,0,0,0,0}},
    /* 8 */ {"0x2E write in default(NRC)",   {0x05,0x2E,0xF1,0x90,0xE4,0x0C,0,0}},
};
#define UDS_TEST_NUM  (sizeof(uds_tests)/sizeof(uds_tests[0]))
/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
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

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* 如需添加其他任务，可在此处添加 */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
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
  can_driver_init();
  for(;;)
  {
      can_check_and_recover_busoff();   // 【新增】Bus-Off检测与自动恢复，每100ms检查一次
    if (sim_enable_send)
    {
      CanMsgTypeDef sim_msg;
      sim_msg.id = 0x18000501U;
      sim_msg.len = 8;
      sim_msg.data[0] = (uint8_t)(sim_voltage & 0xFF);
      sim_msg.data[1] = (uint8_t)((sim_voltage >> 8) & 0xFF);
      sim_msg.data[2] = (uint8_t)(sim_current & 0xFF);
      sim_msg.data[3] = (uint8_t)((sim_current >> 8) & 0xFF);
      sim_msg.data[4] = (uint8_t)sim_temperature;
      sim_msg.data[5] = 0;
      sim_msg.data[6] = 0;
      sim_msg.data[7] = 0;
      can_send(&sim_msg);
    }
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        /* ===== UDS自动测试：每5秒发一个请求，轮流测试所有服务 ===== */
    uds_test_cnt++;
    if (uds_test_cnt >= 50U)   /* 50帧 × 100ms = 5秒 */
    {
        uds_test_cnt = 0;

        /* 发完索引7后，停2个周期(10秒)，让扩展会话超时退回默认会话 */
        static uint8_t wait_timeout = 0;
        if (uds_test_idx == 8 && wait_timeout < 2)
        {
            wait_timeout++;
            printf("[UDS] waiting for session timeout (%d/2)...\r\n", wait_timeout);
        }
        else
        {
            wait_timeout = 0;
            CanMsgTypeDef uds_req;
            uds_req.id = 0x000007E0U;
            uds_req.len = 8U;
            memcpy(uds_req.data, uds_tests[uds_test_idx].data, 8U);
            can_send(&uds_req);
            printf("[UDS TX] %s\r\n", uds_tests[uds_test_idx].name);

            uds_test_idx++;
            if (uds_test_idx >= UDS_TEST_NUM)
            {
                uds_test_idx = 0;
                printf("[UDS] === test cycle done, restart ===\r\n");
            }
        }
    }
    /* ===== UDS自动测试结束 ===== */

    /* ===== 【调试】查询方式读CAN，绕过中断，测试能不能收到 ===== */
    if (HAL_CAN_GetRxFifoFillLevel(&hcan, CAN_RX_FIFO0) > 0U)
    {
        CAN_RxHeaderTypeDef rx_header;
        uint8_t rx_data[8];
        if (HAL_CAN_GetRxMessage(&hcan, CAN_RX_FIFO0, &rx_header, rx_data) == HAL_OK)
        {
            printf("[POLL RX] ID=0x%08X len=%d data:", rx_header.ExtId, rx_header.DLC);
            for(uint8_t i = 0; i < rx_header.DLC; i++)
            {
                printf("%02X ", rx_data[i]);
            }
            printf("\r\n");
        }
    }
    /* ===== 查询测试结束 ===== */
    osDelay(100);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
/* USER CODE END Application */

