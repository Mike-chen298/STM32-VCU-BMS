#ifndef BMS_APP_H
#define BMS_APP_H

#include <stdint.h>
#include "bms_config.h"

// BMS 数据结构
typedef struct {
    uint16_t voltage;
    int16_t current;
    int8_t temperature;
    uint8_t fault_flag;
    uint32_t last_msg_tick;
    uint8_t fault_debounce[4];  // 【新增】4个硬件故障防抖计数器
                                // [0]=过压 [1]=欠压 [2]=过温 [3]=过流
} BmsData_t;

// BMS 状态机枚举
typedef enum {
    BMS_STATE_NORMAL = 0,
    BMS_STATE_WARNING,
    BMS_STATE_FAULT,
    BMS_STATE_SLEEP
} BmsState_t;

// 对外接口
void BMS_UpdateFault(BmsData_t *data);
BmsState_t BMS_GetState(BmsData_t *data);
void BMS_DisplayData(BmsData_t *data, char *line_buf);
uint8_t BMS_GetFault(BmsData_t *data);

#endif
