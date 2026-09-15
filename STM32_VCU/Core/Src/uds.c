#include "uds.h"
#include "bms_app.h"
#include "cmsis_os2.h"
#include <string.h>

/* ============================================================
 *  外部全局变量（定义在freertos.c中）
 *  注意：需要把freertos.c里 g_bms_data 和 g_bms_mutex 的 static 去掉
 * ========================================================== */
extern BmsData_t g_bms_data;
extern osMutexId_t g_bms_mutex;

/* ============================================================
 *  静态变量
 * ========================================================== */
static UDS_Session_t g_current_session = UDS_SESSION_STATE_DEFAULT;
static uint32_t g_last_diag_tick = 0U;   /* 最后一次诊断请求的tick */

/* ============================================================
 *  内部辅助函数声明
 * ========================================================== */
static uint8_t uds_build_nrc(uint8_t req_sid, uint8_t nrc, uint8_t *res_data);
static uint8_t uds_did_is_supported(uint16_t did);
static uint8_t uds_read_did(uint16_t did, uint8_t *buf, uint8_t *len);
static uint8_t uds_write_did(uint16_t did, const uint8_t *buf, uint8_t len);
static uint8_t uds_handle_session_control(const uint8_t *req, uint8_t len, uint8_t *res);
static uint8_t uds_handle_read_data(const uint8_t *req, uint8_t len, uint8_t *res);
static uint8_t uds_handle_write_data(const uint8_t *req, uint8_t len, uint8_t *res);
/* ============================================================
 *  UDS初始化
 * ============================================================ */
void UDS_Init(void)
{
    g_current_session = UDS_SESSION_STATE_DEFAULT;
    g_last_diag_tick = 0U;
}

/* ============================================================
 *  核心：处理一帧UDS请求
 *  输入：req_data（CAN数据域8字节），req_len
 *  输出：res_data（响应数据缓冲区），返回响应长度
 * ============================================================ */
uint8_t UDS_ProcessRequest(const uint8_t *req_data, uint8_t req_len,
                           uint8_t *res_data)
{
    if (req_data == NULL || res_data == NULL || req_len < 2)
    {
        return 0;  /* 无效输入，不响应 */
    }

    /* 记录最后一次诊断请求时间（用于会话超时） */
    g_last_diag_tick = osKernelGetTickCount();

    /* ---------- 1. 检查PCI：必须是单帧 ---------- */
    uint8_t pci = req_data[0];
    if ((pci & 0xF0U) != 0x00U)
    {
        /* 不是单帧（首帧/流控/连续帧），我们不支持多帧，回格式错误 */
        return uds_build_nrc(req_data[1], UDS_NRC_FORMAT_ERROR, res_data);
    }

    uint8_t data_len = pci & 0x0FU;  /* 单帧有效数据长度 */
    if (data_len < 1U || data_len > 7U)
    {
        return uds_build_nrc(req_data[1], UDS_NRC_FORMAT_ERROR, res_data);
    }

    /* ---------- 2. 取SID，分发服务 ---------- */
    uint8_t sid = req_data[1];

    switch (sid)
    {
    case UDS_SID_DIAG_SESSION:   /* 0x10 会话控制 */
        return uds_handle_session_control(req_data, data_len, res_data);

    case UDS_SID_READ_DATA:      /* 0x22 读数据 */
        return uds_handle_read_data(req_data, data_len, res_data);

    case UDS_SID_WRITE_DATA:     /* 0x2E 写数据 */
        return uds_handle_write_data(req_data, data_len, res_data);

    default:
        /* 不支持的服务，回NRC 0x11 */
        return uds_build_nrc(sid, UDS_NRC_SERVICE_NOT_SUPPORT, res_data);
    }
}

/* ============================================================
 *  0x10 诊断会话控制
 * ============================================================ */
static uint8_t uds_handle_session_control(const uint8_t *req, uint8_t len,
                                          uint8_t *res)
{
    /* 请求格式：02 10 <子功能>，len至少2（SID+子功能） */
    if (len < 2U)
    {
        return uds_build_nrc(UDS_SID_DIAG_SESSION, UDS_NRC_FORMAT_ERROR, res);
    }

    uint8_t subfunc = req[2];

    switch (subfunc)
    {
    case UDS_SESSION_DEFAULT:
        g_current_session = UDS_SESSION_STATE_DEFAULT;
        break;
    case UDS_SESSION_EXTENDED:
        g_current_session = UDS_SESSION_STATE_EXTENDED;
        break;
    default:
        /* 子功能不支持，回NRC 0x12 */
        return uds_build_nrc(UDS_SID_DIAG_SESSION, UDS_NRC_SUBFUNC_NOT_SUPPORT, res);
    }

    /* 正响应格式：07 50 <子功能> <P2高> <P2低> <P2*高> <P2*低>
       P2Server = 0x0032（50ms）
       P2*Server = 0x01F4（5000ms，单位10ms） */
    res[0] = 0x06U;                          /* PCI：单帧，7字节有效数据 */
    res[1] = UDS_SID_POS_DIAG_SESSION;       /* 0x50 */
    res[2] = subfunc;                        /* 回显子功能 */
    res[3] = 0x00U;                          /* P2Server高字节 */
    res[4] = 0x32U;                          /* P2Server低字节 = 50ms */
    res[5] = 0x01U;                          /* P2*Server高字节 */
    res[6] = 0xF4U;                          /* P2*Server低字节 = 5000ms */
    return 7U;
}

/* ============================================================
 *  0x22 按标识符读数据
 * ============================================================ */
static uint8_t uds_handle_read_data(const uint8_t *req, uint8_t len,
                                    uint8_t *res)
{
    /* 请求格式：03 22 <DID高> <DID低>，len至少3（SID+DID2字节） */
    if (len < 3U)
    {
        return uds_build_nrc(UDS_SID_READ_DATA, UDS_NRC_FORMAT_ERROR, res);
    }

    uint16_t did = ((uint16_t)req[2] << 8) | req[3];

    /* 检查DID是否存在 */
    if (!uds_did_is_supported(did))
    {
        return uds_build_nrc(UDS_SID_READ_DATA, UDS_NRC_REQUEST_OUT_OF_RANGE, res);
    }

    /* 读DID数据（加锁访问g_bms_data） */
    uint8_t data_buf[4];
    uint8_t data_len = 0;
    if (!uds_read_did(did, data_buf, &data_len))
    {
        return uds_build_nrc(UDS_SID_READ_DATA, UDS_NRC_GENERAL_REJECT, res);
    }

    /* 正响应格式：PCI 62 <DID高> <DID低> <数据...> */
    res[0] = (uint8_t)(0x03U + data_len);   /* PCI = 3字节头 + 数据长度 */
    res[1] = UDS_SID_POS_READ_DATA;         /* 0x62 */
    res[2] = req[2];                        /* DID高（回显） */
    res[3] = req[3];                        /* DID低（回显） */
    memcpy(&res[4], data_buf, data_len);    /* 数据 */
    return (uint8_t)(4U + data_len);        /* 返回总长度 */
}

/* ============================================================
 *  0x2E 按标识符写数据
 * ============================================================ */
static uint8_t uds_handle_write_data(const uint8_t *req, uint8_t len,
                                     uint8_t *res)
{
    /* 写数据必须在扩展会话下 */
    if (g_current_session != UDS_SESSION_STATE_EXTENDED)
    {
        return uds_build_nrc(UDS_SID_WRITE_DATA, UDS_NRC_SUBFUNC_NOT_IN_SESSION, res);
    }

    /* 请求格式：04 2E <DID高> <DID低> <数据...>，len至少4 */
    if (len < 4U)
    {
        return uds_build_nrc(UDS_SID_WRITE_DATA, UDS_NRC_FORMAT_ERROR, res);
    }

    uint16_t did = ((uint16_t)req[2] << 8) | req[3];
    uint8_t data_len = len - 3U;  /* 减去SID+DID2字节 = 数据长度 */

    /* 检查DID是否存在 */
    if (!uds_did_is_supported(did))
    {
        return uds_build_nrc(UDS_SID_WRITE_DATA, UDS_NRC_REQUEST_OUT_OF_RANGE, res);
    }

    /* 写DID数据（加锁访问g_bms_data） */
    if (!uds_write_did(did, &req[4], data_len))
    {
        return uds_build_nrc(UDS_SID_WRITE_DATA, UDS_NRC_GENERAL_REJECT, res);
    }

    /* 正响应格式：03 6E <DID高> <DID低>（回显DID） */
    res[0] = 0x03U;                          /* PCI：3字节有效数据 */
    res[1] = UDS_SID_POS_WRITE_DATA;        /* 0x6E */
    res[2] = req[2];                        /* DID高（回显） */
    res[3] = req[3];                        /* DID低（回显） */
    return 4U;
}

/* ============================================================
 *  辅助：组否定响应
 *  格式：03 7F <请求SID> <NRC>
 * ============================================================ */
static uint8_t uds_build_nrc(uint8_t req_sid, uint8_t nrc, uint8_t *res)
{
    res[0] = 0x03U;   /* PCI：单帧，3字节有效数据 */
    res[1] = 0x7FU;   /* 否定响应标志 */
    res[2] = req_sid; /* 哪个服务的否定响应 */
    res[3] = nrc;     /* 否定原因码 */
    return 4U;
}

/* ============================================================
 *  辅助：检查DID是否支持
 * ============================================================ */
static uint8_t uds_did_is_supported(uint16_t did)
{
    switch (did)
    {
    case UDS_DID_VOLTAGE:
    case UDS_DID_CURRENT:
    case UDS_DID_TEMPERATURE:
    case UDS_DID_FAULT:
        return 1U;
    default:
        return 0U;
    }
}

/* ============================================================
 *  辅助：读DID数据（加锁）
 * ============================================================ */
static uint8_t uds_read_did(uint16_t did, uint8_t *buf, uint8_t *len)
{
    osMutexAcquire(g_bms_mutex, osWaitForever);

    switch (did)
    {
    case UDS_DID_VOLTAGE:
        /* 电压：uint16_t，小端（低字节在前），2字节 */
        buf[0] = (uint8_t)(g_bms_data.voltage & 0xFF);
        buf[1] = (uint8_t)((g_bms_data.voltage >> 8) & 0xFF);
        *len = 2U;
        break;

    case UDS_DID_CURRENT:
        /* 电流：int16_t，小端，2字节 */
        buf[0] = (uint8_t)(g_bms_data.current & 0xFF);
        buf[1] = (uint8_t)((g_bms_data.current >> 8) & 0xFF);
        *len = 2U;
        break;

    case UDS_DID_TEMPERATURE:
        /* 温度：int8_t，1字节 */
        buf[0] = (uint8_t)g_bms_data.temperature;
        *len = 1U;
        break;

    case UDS_DID_FAULT:
        /* 故障码：uint8_t，1字节 */
        buf[0] = g_bms_data.fault_flag;
        *len = 1U;
        break;

    default:
        osMutexRelease(g_bms_mutex);
        return 0U;
    }

    osMutexRelease(g_bms_mutex);
    return 1U;
}

/* ============================================================
 *  辅助：写DID数据（加锁）
 * ============================================================ */
static uint8_t uds_write_did(uint16_t did, const uint8_t *buf, uint8_t len)
{
    osMutexAcquire(g_bms_mutex, osWaitForever);

    switch (did)
    {
    case UDS_DID_VOLTAGE:
        if (len != 2U) { osMutexRelease(g_bms_mutex); return 0U; }
        g_bms_data.voltage = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
        break;

    case UDS_DID_CURRENT:
        if (len != 2U) { osMutexRelease(g_bms_mutex); return 0U; }
        g_bms_data.current = (int16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
        break;

    case UDS_DID_TEMPERATURE:
        if (len != 1U) { osMutexRelease(g_bms_mutex); return 0U; }
        g_bms_data.temperature = (int8_t)buf[0];
        break;

    case UDS_DID_FAULT:
        /* 故障码不允许通过UDS写（只读），返回失败 */
        osMutexRelease(g_bms_mutex);
        return 0U;

    default:
        osMutexRelease(g_bms_mutex);
        return 0U;
    }

    osMutexRelease(g_bms_mutex);
    return 1U;
}

/* ============================================================
 *  获取当前会话
 * ============================================================ */
UDS_Session_t UDS_GetSession(void)
{
    return g_current_session;
}

/* ============================================================
 *  会话超时检测
 *  扩展会话下，超过UDS_SESSION_TIMEOUT_MS无诊断请求，自动退回默认会话
 *  应在Monitor任务中周期调用
 * ============================================================ */
void UDS_TickSessionTimeout(uint32_t current_tick)
{
    if (g_current_session == UDS_SESSION_STATE_EXTENDED)
    {
        if (g_last_diag_tick != 0U &&
            (current_tick - g_last_diag_tick) > UDS_SESSION_TIMEOUT_MS)
        {
            g_current_session = UDS_SESSION_STATE_DEFAULT;
        }
    }
}
