# STM32 VCU-BMS 车载仿真项目

基于STM32F103的整车控制器(VCU)与电池管理系统(BMS)仿真平台，实现双板CAN通信、故障检测、Bus-Off自动恢复、UDS诊断协议栈。

## 硬件平台

| 模块 | 型号 | 说明 |
|---|---|---|
| VCU主控 | STM32F103ZET6 | 整车控制器，CAN接收+故障判断+OLED显示+UDS服务端 |
| BMS模拟器 | STM32F103C8T6 | 模拟BMS发送电池数据，CAN发送+Bus-Off恢复 |
| CAN收发器 | TJA1050 ×2 | 板载120Ω终端电阻，无跳线 |
| 显示 | 0.96寸OLED | PB8/PB9软件I2C，显示BMS数据与状态 |
| 调试器 | ST-LINK V2 | 烧录与调试 |

## 硬件接线

### CAN总线（双板互联）
VCU TJA1050 CANH ←→ C8T6 TJA1050 CANH
VCU TJA1050 CANL ←→ C8T6 TJA1050 CANL
VCU GND ←→ C8T6 GND （共地，必须接）

### 终端电阻规则
- 2节点通信：两端各保留120Ω，共2个终端电阻并联=60Ω
- 3节点及以上：仅物理总线两端节点保留120Ω，中间节点断开板载电阻

### TJA1050与MCU接线
- MCU PA12(CAN_TX) → TJA1050 TXD（模块丝印"TX"）
- TJA1050 RXD（模块丝印"RX"）→ MCU PA11(CAN_RX)

## CAN配置

- 模式：Normal（正常模式）
- 波特率：500Kbps
- 位时序：Prescaler=4, BS1=15, BS2=2, 采样点88.9%
- 帧格式：扩展帧（29位ID）
- 过滤器：FIFO0全通配

位时序计算：Sync_Seg(1tq) + BS1(15tq) + BS2(2tq) = 18tq
CAN时钟36MHz(APB1)，36M / (4 × 18) = 500Kbps

## CAN报文定义

| CAN ID | 方向 | 说明 | 周期 |
|---|---|---|---|
| 0x18000501 | BMS→VCU | BMS数据帧（电压/电流/温度） | 100ms |
| 0x000007E0 | 诊断仪→VCU | UDS诊断请求 | 事件触发 |
| 0x000007E8 | VCU→诊断仪 | UDS诊断响应 | 事件触发 |

### BMS数据帧格式（0x18000501，小端）
| 字节 | 含义 | 类型 | 单位 |
|---|---|---|---|
| data[0-1] | 电压 | uint16_t | 0.1V |
| data[2-3] | 电流 | int16_t | 0.1A |
| data[4] | 温度 | int8_t | ℃ |
| data[5-7] | 预留 | - | - |

## 软件架构

### 分层设计
┌─────────────────────────────────┐
│ App 层：freertos.c               │
│  任务调度、业务流程、CAN ID 分发  │
├─────────────────────────────────┤
│ Service 层：                     │
│  bms_app.c  BMS 业务 + 状态机      │
│  uds.c      UDS 诊断协议栈       │
│  can_driver.c Bus-Off 恢复       │
├─────────────────────────────────┤
│ Driver 层：                      │
│  can_driver.c CAN 收发驱动       │
│  oled.c       OLED 显示驱动      │
│  HAL 库        STM32 外设抽象     │
└─────────────────────────────────┘

### FreeRTOS任务（VCU）
| 任务 | 优先级 | 周期 | 说明 |
|---|---|---|---|
| Task_Monitor | Normal | 500ms | CAN初始化、Bus-Off检测、OLED刷新、通信超时判断、UDS会话超时 |
| Task_RecvMsg | Normal | 2ms | 串口环形缓冲解析（备用通道） |
| Task_BMS_Process | High | 事件驱动 | 从队列取CAN帧，BMS数据解析/故障检测，UDS请求分发 |
| Task_Resp | Normal | 1s | 串口打印BMS状态与故障告警 |
| defaultTask | Normal | 1s | 空任务（保留） |

## BMS状态机
上电 → INIT（未收到数据）
↓ 收到第一帧 BMS 数据
NORMAL（正常运行）
↓ 温度 > 55℃          ↓ 故障触发（防抖 3 帧）
WARNING（预警）      FAULT（故障）
↓ 温度≤55℃          ↓ 故障全部清除（防抖 3 帧）
NORMAL                NORMAL

任意状态 → 通信超时 3 秒 → SLEEP（休眠）
SLEEP → 通信恢复 → NORMAL

## 故障检测

| 故障 | 阈值 | fault_flag位 | 说明 |
|---|---|---|---|
| 过压(OVP) | >400V | 0x01 | 防抖3帧（300ms）置位 |
| 欠压(UVP) | <200V | 0x02 | 防抖3帧置位 |
| 过温(OTP) | >60℃ | 0x04 | 防抖3帧置位 |
| 过流(OCP) | >30A | 0x08 | 防抖3帧置位 |
| 通信超时 | >3s无数据 | 0x10 | 立即置位 |

故障防抖：连续3帧异常才置位故障，连续3帧正常才清除，过滤总线毛刺。

## Bus-Off软件恢复

- 检测方式：直接读 `hcan.Instance->ESR & CAN_ESR_BOFF` 寄存器位（老版HAL无HAL_CAN_STATE_BUS_OFF枚举）
- 恢复流程：HAL_CAN_Stop → 重配过滤器 → HAL_CAN_Start → 重新激活接收中断
- 检测周期：VCU 500ms，C8T6 100ms
- 硬件机制：重启后CAN控制器自动等待128次11个隐性位后退出Bus-Off

## UDS诊断子集

实现ISO 14229精简版UDS诊断协议栈（单帧传输，未实现ISO-TP多帧）。

### 支持的服务
| SID | 服务 | 说明 |
|---|---|---|
| 0x10 | 诊断会话控制 | 默认会话(0x01)/扩展会话(0x03)，5秒超时自动退回默认 |
| 0x22 | 按DID读数据 | 读电压/电流/温度/故障码 |
| 0x2E | 按DID写数据 | 仅扩展会话允许，默认会话拒绝(NRC 0x7E) |

### DID定义
| DID | 含义 | 类型 | 字节序 | 单位 | 读写 |
|---|---|---|---|---|---|
| 0xF190 | 电池电压 | uint16_t | 小端 | 0.1V | 读/写 |
| 0xF191 | 电池电流 | int16_t | 小端 | 0.1A | 只读 |
| 0xF192 | 电池温度 | int8_t | - | ℃ | 只读 |
| 0xF193 | 故障码 | uint8_t | - | - | 只读 |

### NRC否定响应码
| NRC | 含义 |
|---|---|
| 0x11 | 服务不支持 |
| 0x12 | 子功能不支持 |
| 0x13 | 报文格式错误 |
| 0x31 | 请求超出范围（DID不存在） |
| 0x7E | 子功能在当前会话不支持 |

### 字节序说明
- DID字段：大端（高字节在前），遵循UDS行业惯例
- 数据值：小端（低字节在前），与BMS数据帧保持一致
- 每个DID的数据类型、字节序、单位在DID定义表中明确约定

## 开发环境

- IDE：Keil MDK5 (ARMCC V5.06)
- 配置工具：STM32CubeMX
- 操作系统：FreeRTOS CMSIS-V2
- 固件库：STM32 HAL库
- 版本控制：Git

## 工程结构
STM32_VCU/          # VCU 工程（ZET6）
├── Core/
│   ├── Inc/
│   │   ├── uds.h          # UDS 诊断头文件
│   │   ├── bms_app.h      # BMS 业务头文件
│   │   ├── bms_config.h   # BMS 配置（阈值、超时）
│   │   └── can_driver.h   # CAN 驱动头文件
│   └── Src/
│       ├── uds.c          # UDS 诊断实现
│       ├── bms_app.c      # BMS 业务 + 状态机
│       ├── can_driver.c   # CAN 驱动 + Bus-Off 恢复
│       └── freertos.c     # 任务与业务流程
STM32_BMS/          # BMS 模拟器工程（C8T6）
└── Core/Src/
├── can_driver.c   # CAN 发送 + Bus-Off 恢复
└── freertos.c     # BMS 数据模拟发送

## 项目阶段

- [x] 阶段0：工程搭建与硬件接线
- [x] 阶段1：双板CAN通信
- [x] 阶段2：BMS数据解析与OLED显示
- [x] 阶段3：故障检测（过压/欠压/过温/过流）
- [x] 阶段4：通信超时检测
- [x] 阶段5：故障防抖（3帧计数器）
- [x] 阶段6：Bus-Off软件恢复 + UDS诊断子集
- [x] 阶段7：BMS状态机 + 项目收尾

