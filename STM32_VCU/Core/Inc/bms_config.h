#ifndef BMS_CONFIG_H
#define BMS_CONFIG_H

/*************************** 电压阈值（单位：0.1V） ***************************/
#define BMS_OVP_THRESHOLD   4000    // 400.0V 过压
#define BMS_UVP_THRESHOLD   2000    // 200.0V 欠压

/*************************** 温度阈值（单位：℃） ***************************/
#define BMS_OTP_THRESHOLD   60      // 60℃ 过温

/*************************** 电流阈值（单位：0.1A） ***************************/
#define BMS_OCP_THRESHOLD   300     // 30.0A 过流

/*************************** 通信超时（单位：ms） ***************************/
#define BMS_TIMEOUT_MS      3000    // 3秒无报文视为通信丢失

/*================ CAN模式切换：阶段4回环 / 阶段5真实硬件CAN =================*/
#define CAN_LOOPBACK_TEST_MODE     1U   // 1：回环自测模式；0：真实硬件CAN模式

#if (CAN_LOOPBACK_TEST_MODE == 1U)
#define ENABLE_SIM_BMS_TASK        1U   // 开启内部模拟BMS发送任务（回环自测用）
#else
#define ENABLE_SIM_BMS_TASK        0U   // 关闭模拟任务，报文来自外部CAN硬件
#endif

#endif

