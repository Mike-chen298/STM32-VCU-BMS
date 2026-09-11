#include "can_driver.h"
#include "can.h"
#include "FreeRTOS.h"
#include <string.h>
#include <stdio.h>

// 报文统计（C8T6只发不收，这两个变量保留但不用）
volatile uint32_t can_rx_total = 0U;
volatile uint32_t can_drop_count = 0U;

int can_send(CanMsgTypeDef *msg)
{
    if(msg == NULL)
        return -1;

    CAN_TxHeaderTypeDef tx_header;
    uint32_t tx_mailbox;

    tx_header.StdId = 0;
    tx_header.ExtId = msg->id;
    tx_header.IDE = CAN_ID_EXT;
    tx_header.RTR = CAN_RTR_DATA;
    tx_header.DLC = msg->len;
    tx_header.TransmitGlobalTime = DISABLE;

    if(HAL_CAN_AddTxMessage(&hcan, &tx_header, msg->data, &tx_mailbox) != HAL_OK)
    {
        return -2;
    }
    return 0;
}

void can_driver_init(void)
{
    HAL_StatusTypeDef ret;

    // 第1步：配置CAN过滤器（必须在启动CAN之前配置）
    CAN_FilterTypeDef sFilterConfig;
    sFilterConfig.FilterBank = 0;
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    sFilterConfig.FilterIdHigh = 0x0000U;
    sFilterConfig.FilterIdLow = 0x0000U;
    sFilterConfig.FilterMaskIdHigh = 0x0000U;
    sFilterConfig.FilterMaskIdLow = 0x0000U;
    sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
    sFilterConfig.FilterActivation = CAN_FILTER_ENABLE;
    sFilterConfig.SlaveStartFilterBank = 14;

    ret = HAL_CAN_ConfigFilter(&hcan, &sFilterConfig);
    printf("CAN_Filter ret=%d\r\n", ret);

    // 第2步：启动CAN控制器
    ret = HAL_CAN_Start(&hcan);
    printf("CAN_Start ret=%d, state=%d\r\n", ret, HAL_CAN_GetState(&hcan));

    // C8T6只发不收，不开启接收中断
}

// CAN接收回调（C8T6不用，用#if 0屏蔽，避免编译MsgQueueHandle未定义）
#if 0
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    CAN_RxHeaderTypeDef rx_header;
    uint8_t rx_data[8];
    if(HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_header, rx_data) == HAL_OK)
    {
        if(rx_header.RTR == CAN_RTR_REMOTE)
        {
            return;
        }
        CanMsgTypeDef can_msg;
        can_msg.id = rx_header.ExtId;
        can_msg.len = rx_header.DLC;
        memcpy(can_msg.data, rx_data, 8);
        //中断上下文，timeout必须写0，绝对不能阻塞
        osStatus_t status = osMessageQueuePut(MsgQueueHandle, &can_msg, 0U, 0U);
        if(status == osOK)
        {
            can_rx_total++;
        }
        else
        {
            can_drop_count++;
        }
    }
}
#endif
uint8_t can_check_and_recover_busoff(void)
{
    // 直接读ESR寄存器BOFF位判断Bus-Off（不依赖HAL状态枚举，兼容性好）
    if ((hcan.Instance->ESR & CAN_ESR_BOFF) == 0U)
    {
        return 0;  // 正常，不是Bus-Off
    }

    printf("[CAN] Bus-Off detected! TEC>255, starting recovery...\r\n");

    // 1. 停止CAN控制器
    HAL_CAN_Stop(&hcan);

    // 2. 重新配置过滤器（Bus-Off后寄存器状态不确定，重新配一遍保险）
    CAN_FilterTypeDef sFilterConfig;
    sFilterConfig.FilterBank = 0;
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    sFilterConfig.FilterIdHigh       = 0x0000U;
    sFilterConfig.FilterIdLow        = 0x0000U;
    sFilterConfig.FilterMaskIdHigh   = 0x0000U;
    sFilterConfig.FilterMaskIdLow    = 0x0000U;
    sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
    sFilterConfig.FilterActivation   = CAN_FILTER_ENABLE;
    HAL_CAN_ConfigFilter(&hcan, &sFilterConfig);

    // 3. 重新启动CAN（启动后硬件自动等待128次11隐性位后退出Bus-Off）
    if (HAL_CAN_Start(&hcan) != HAL_OK)
    {
        printf("[CAN] ERROR: CAN_Start failed during recovery!\r\n");
        return 1;
    }

    // 4. 重新激活接收中断（C8T6虽然不用接收，但调用了也不影响，代码统一）
    if (HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK)
    {
        printf("[CAN] ERROR: ActivateNotification failed during recovery!\r\n");
        return 1;
    }

    printf("[CAN] Bus-Off recovery done, CAN restarted.\r\n");
    return 1;
}

