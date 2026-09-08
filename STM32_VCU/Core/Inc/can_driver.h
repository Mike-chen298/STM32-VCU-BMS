#ifndef __CAN_DRIVER_H
#define __CAN_DRIVER_H

#include "main.h"
#include "cmsis_os2.h"

// 和全局统一报文结构体
typedef struct {
    uint32_t id;
    uint8_t len;
    uint8_t data[8];
} CanMsgTypeDef;

extern volatile uint32_t can_rx_total;
extern volatile uint32_t can_drop_count;

/**
 * @brief CAN发送接口
 * @param msg 报文指针
 * @return 0成功，非0失败
 */
int can_send(CanMsgTypeDef *msg);

/**
 * @brief CAN过滤器初始化，启动CAN控制器，开启接收通知
 */
void can_driver_init(void);

#endif
