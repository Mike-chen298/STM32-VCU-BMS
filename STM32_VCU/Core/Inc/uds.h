#ifndef __UDS_H
#define __UDS_H

#include <stdint.h>

/* ============================================================
 *  UDS诊断CAN ID定义
 *  0x7E0 = 诊断仪→ECU 请求（物理寻址）
 *  0x7E8 = ECU→诊断仪 响应（请求ID + 8）
 * ========================================================== */
#define UDS_REQ_ID      0x000007E0U   /* UDS请求CAN ID */
#define UDS_RES_ID      0x000007E8U   /* UDS响应CAN ID */

/* ============================================================
 *  UDS调试打印开关
 *  1 = 开启调试打印（串口输出请求/响应详情，调试阶段用）
 *  0 = 关闭调试打印（正式发布/封版提交用）
 * ========================================================== */
#define UDS_DEBUG_PRINT  0U

/* ============================================================
 *  UDS服务ID（SID）
 *  正响应SID = 请求SID + 0x40
 * ========================================================== */
#define UDS_SID_DIAG_SESSION        0x10U   /* 诊断会话控制 */
#define UDS_SID_READ_DATA           0x22U   /* 按标识符读数据 */
#define UDS_SID_WRITE_DATA          0x2EU   /* 按标识符写数据 */

#define UDS_SID_POS_DIAG_SESSION    0x50U   /* 0x10 + 0x40 */
#define UDS_SID_POS_READ_DATA       0x62U   /* 0x22 + 0x40 */
#define UDS_SID_POS_WRITE_DATA      0x6EU   /* 0x2E + 0x40 */

/* ============================================================
 *  否定响应码（NRC）
 * ========================================================== */
#define UDS_NRC_GENERAL_REJECT      0x10U   /* 通用拒绝 */
#define UDS_NRC_SERVICE_NOT_SUPPORT 0x11U   /* 服务不支持 */
#define UDS_NRC_SUBFUNC_NOT_SUPPORT 0x12U   /* 子功能不支持 */
#define UDS_NRC_FORMAT_ERROR        0x13U   /* 报文格式/长度错误 */
#define UDS_NRC_CONDITION_NOT_CORRECT 0x22U /* 条件不满足 */
#define UDS_NRC_REQUEST_OUT_OF_RANGE 0x31U  /* 请求超出范围（DID不存在） */
#define UDS_NRC_SUBFUNC_NOT_IN_SESSION 0x7EU /* 子功能在当前会话不支持 */

/* ============================================================
 *  数据标识符（DID）
 *  0xF1xx 范围：车辆制造商自定义数据标识符
 * ========================================================== */
#define UDS_DID_VOLTAGE             0xF190U /* 电池电压（0.1V） */
#define UDS_DID_CURRENT             0xF191U /* 电池电流（0.1A，有符号） */
#define UDS_DID_TEMPERATURE         0xF192U /* 电池温度（℃，有符号） */
#define UDS_DID_FAULT               0xF193U /* 故障码（fault_flag） */

/* ============================================================
 *  诊断会话子功能
 * ========================================================== */
#define UDS_SESSION_DEFAULT         0x01U   /* 默认会话 */
#define UDS_SESSION_EXTENDED        0x03U   /* 扩展诊断会话 */

/* ============================================================
 *  会话超时（毫秒）
 *  扩展会话5秒无诊断请求，自动退回默认会话
 * ========================================================== */
#define UDS_SESSION_TIMEOUT_MS      5000U

/* ============================================================
 *  会话状态枚举
 * ========================================================== */
typedef enum {
    UDS_SESSION_STATE_DEFAULT = 0,
    UDS_SESSION_STATE_EXTENDED
} UDS_Session_t;

/* ============================================================
 *  对外接口
 * ========================================================== */

/**
 * @brief UDS初始化，设置默认会话
 */
void UDS_Init(void);

/**
 * @brief 处理一帧UDS请求，生成响应
 * @param req_data  请求数据指针（CAN数据域8字节）
 * @param req_len   请求数据长度
 * @param res_data  响应数据缓冲区（至少8字节）
 * @return 响应数据长度，0=不发送响应
 */
uint8_t UDS_ProcessRequest(const uint8_t *req_data, uint8_t req_len,
                           uint8_t *res_data);

/**
 * @brief 获取当前诊断会话
 */
UDS_Session_t UDS_GetSession(void);

/**
 * @brief 会话超时检测，应在任务中周期调用
 * @param current_tick 当前系统tick（osKernelGetTickCount）
 */
void UDS_TickSessionTimeout(uint32_t current_tick);

#endif /* __UDS_H */
