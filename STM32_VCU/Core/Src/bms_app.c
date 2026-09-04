#include "bms_app.h"
#include <stdio.h>
#include "oled.h"

void BMS_UpdateFault(BmsData_t *data)
{
    data->fault_flag &= ~0x0F;  // Çå³ý 0x0F Î»£¬±£Áô 0x10

    if (data->voltage > BMS_OVP_THRESHOLD) data->fault_flag |= 0x01;
    if (data->voltage < BMS_UVP_THRESHOLD) data->fault_flag |= 0x02;
    if (data->temperature > BMS_OTP_THRESHOLD) data->fault_flag |= 0x04;
    if (data->current > BMS_OCP_THRESHOLD || data->current < -BMS_OCP_THRESHOLD) {
        data->fault_flag |= 0x08;
    }
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
