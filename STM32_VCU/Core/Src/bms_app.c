#include "bms_app.h"
#include <stdio.h>
#include "oled.h"

void BMS_UpdateFault(BmsData_t *data)
{
    // 注意：不再用 fault_flag &= ~0x0F 每次全清，改为靠防抖计数器控制置位/清除
    // 通信超时位0x10不在此处理，由Monitor任务单独管理

    // ===== 过压 0x01（debounce[0]）=====
    if (data->voltage > BMS_OVP_THRESHOLD) {
        if (data->fault_debounce[0] < BMS_FAULT_DEBOUNCE_CNT)
            data->fault_debounce[0]++;
        if (data->fault_debounce[0] >= BMS_FAULT_DEBOUNCE_CNT)
            data->fault_flag |= 0x01;
    } else {
        if (data->fault_debounce[0] > 0)
            data->fault_debounce[0]--;
        if (data->fault_debounce[0] == 0)
            data->fault_flag &= ~0x01;
    }

    // ===== 欠压 0x02（debounce[1]）=====
    if (data->voltage < BMS_UVP_THRESHOLD) {
        if (data->fault_debounce[1] < BMS_FAULT_DEBOUNCE_CNT)
            data->fault_debounce[1]++;
        if (data->fault_debounce[1] >= BMS_FAULT_DEBOUNCE_CNT)
            data->fault_flag |= 0x02;
    } else {
        if (data->fault_debounce[1] > 0)
            data->fault_debounce[1]--;
        if (data->fault_debounce[1] == 0)
            data->fault_flag &= ~0x02;
    }

    // ===== 过温 0x04（debounce[2]）=====
    if (data->temperature > BMS_OTP_THRESHOLD) {
        if (data->fault_debounce[2] < BMS_FAULT_DEBOUNCE_CNT)
            data->fault_debounce[2]++;
        if (data->fault_debounce[2] >= BMS_FAULT_DEBOUNCE_CNT)
            data->fault_flag |= 0x04;
    } else {
        if (data->fault_debounce[2] > 0)
            data->fault_debounce[2]--;
        if (data->fault_debounce[2] == 0)
            data->fault_flag &= ~0x04;
    }

    // ===== 过流 0x08（debounce[3]）=====
    if (data->current > BMS_OCP_THRESHOLD || data->current < -BMS_OCP_THRESHOLD) {
        if (data->fault_debounce[3] < BMS_FAULT_DEBOUNCE_CNT)
            data->fault_debounce[3]++;
        if (data->fault_debounce[3] >= BMS_FAULT_DEBOUNCE_CNT)
            data->fault_flag |= 0x08;
    } else {
        if (data->fault_debounce[3] > 0)
            data->fault_debounce[3]--;
        if (data->fault_debounce[3] == 0)
            data->fault_flag &= ~0x08;
    }
//    // 【临时调试打印】每收到一帧CAN就打印，验证完删掉
//    printf("DBG: v=%u d0=%u d1=%u d2=%u d3=%u f=0x%02X\r\n",
//           data->voltage,
//           data->fault_debounce[0], data->fault_debounce[1],
//           data->fault_debounce[2], data->fault_debounce[3],
//           data->fault_flag);
}

BmsState_t BMS_GetState(BmsData_t *data)
{
    if (data->fault_flag & 0x10) return BMS_STATE_SLEEP;
    if (data->fault_flag & 0x0F) return BMS_STATE_FAULT;
    if (data->temperature > 55) return BMS_STATE_WARNING;
    return BMS_STATE_NORMAL;
}

void BMS_DisplayData(BmsData_t *data, char *line_buf)
{
    OLED_ShowString(1, 1, "VCU BMS");

    if (data->fault_flag & 0x10) {
        OLED_ShowString(2, 1, "V:------");
        OLED_ShowString(3, 1, "I:------");
        sprintf(line_buf, "T:--C F:%02X", data->fault_flag);
        OLED_ShowString(4, 1, line_buf);
    } else {
        sprintf(line_buf, "V:%u.%uV", data->voltage/10, data->voltage%10);
        OLED_ShowString(2, 1, line_buf);
        sprintf(line_buf, "I:%+d.%dA", data->current/10, data->current%10);
        OLED_ShowString(3, 1, line_buf);
        sprintf(line_buf, "T:%dC F:%02X", data->temperature, data->fault_flag);
        OLED_ShowString(4, 1, line_buf);
    }
}

uint8_t BMS_GetFault(BmsData_t *data)
{
    return data->fault_flag;
}
