#include "can_driver.h"
#include "can.h"
#include "FreeRTOS.h"
#include <string.h>

// 报文统计
volatile uint32_t can_rx_total = 0U;
volatile uint32_t can_drop_count = 0U;
extern osMessageQueueId_t MsgQueueHandle;

int can_send(CanMsgTypeDef *msg)
{
    if(msg == NULL)
        return -1;

    CAN_TxHeaderTypeDef tx_header;
    uint32_t tx_mailbox;
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
    CAN_FilterTypeDef sFilterConfig;
    sFilterConfig.FilterBank = 0;
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    sFilterConfig.FilterIdHigh  = 0x0000U;
    sFilterConfig.FilterIdLow = 0x0000U;
    sFilterConfig.FilterMaskIdHigh  = 0x0000U;
    sFilterConfig.FilterMaskIdLow = 0x0000U;
    sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
    sFilterConfig.FilterActivation = CAN_FILTER_ENABLE;

    if(HAL_CAN_ConfigFilter(&hcan, &sFilterConfig) != HAL_OK)
    {
        Error_Handler();
    }
    if(HAL_CAN_Start(&hcan) != HAL_OK)
    {
        Error_Handler();
    }
    if(HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK)
    {
        Error_Handler();
    }
}

// CAN接收回调函数
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
