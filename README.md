# STM32 VCU-BMS 车载仿真项目

基于STM32F103的VCU-BMS车载仿真项目，使用FreeRTOS + CAN总线，实现动力电池数据采集、故障管理、车载诊断（UDS子集）、CAN总线容错等功能。

## 硬件平台

- VCU主控制器：STM32F103ZET6
- BMS模拟器：STM32F103C8T6
- CAN收发器：TJA1050 ×2
- 显示：0.96寸OLED（PB8/PB9软件模拟I2C）
- 下载器：ST-LINK

## 工程结构

STM32-VCU-BMS/
├── STM32_VCU/      # ZET6 VCU 主控制器工程
└── STM32_BMS/      # C8T6 BMS 模拟器工程

## 开发环境

- STM32CubeMX + HAL库
- FreeRTOS CMSIS-V2
- Keil MDK 5

## 模式切换说明

项目通过宏定义一键切换两种CAN工作模式，上层业务代码零改动：

| 模式 | 宏配置 | 用途 |
|---|---|---|
| 回环自测模式 | `CAN_LOOPBACK_TEST_MODE = 1U` | 阶段4开发自测，无需外部TJA1050硬件，ZET6内部模拟BMS发送CAN报文 |
| 真实硬件模式 | `CAN_LOOPBACK_TEST_MODE = 0U` | 阶段5实物联调，关闭模拟任务，报文来自C8T6真实CAN硬件 |

> 设计思路：软件先行验证，软硬件解耦。回环模式下可完成90%的驱动与业务逻辑开发，硬件问题不阻塞软件进度。

## 开发进度

- [x] 阶段0：裸机外设验证（LED、USART1、OLED）
- [x] 阶段1：VCU FreeRTOS框架（4任务+消息队列+串口模拟CAN）
- [x] 阶段2：BMS模拟器 FreeRTOS
- [x] 阶段3：业务逻辑打磨与模块化
- [x] 阶段4：CAN回环模式全链路验证
  - CAN驱动、过滤器配置、中断接收、消息队列无丢包
  - 5类BMS故障全部回环自测通过（过压/欠压/过温/过流/CAN通信超时）
  - 修复通信超时故障位粘连bug：超时直接赋值fault_flag=0x10，收到有效报文清除0x10位
  - Task_BMS_Simulate：ZET6内部模拟BMS发送CAN报文，用于回环自测
- [x] 阶段5：真实CAN硬件联调 — 软件框架完成
  - 增加 `CAN_LOOPBACK_TEST_MODE` 宏，一键切换回环自测 / 真实硬件CAN模式
  - 条件编译包裹模拟BMS任务，真实硬件模式下自动屏蔽
  - 上层BMS业务逻辑零改动，底层CAN驱动与业务层解耦
  - 实物硬件联调计划：秋招面试结束后执行（TJA1050 + 杜邦线点对点）
- [ ] 阶段6：车载诊断与可靠性增强（进行中）
  - 故障防抖：计数器机制防止信号毛刺误报
  - UDS诊断子集：0x10会话控制、0x22读数据、0x2E写数据、NRC否定响应
  - Bus-Off软件恢复：错误中断+定时复位CAN控制器
- [ ] 阶段7：BMS状态机与项目收尾
  - 状态机：INIT → NORMAL → FAULT，故障联动与恢复
  - 代码清理、文档完善、Git归档

## 硬件接线

### CAN总线（阶段5真实硬件模式启用）

- ZET6 PA11(CAN_RX) → TJA1050 TX
- ZET6 PA12(CAN_TX) → TJA1050 RX
- C8T6 PA11(CAN_RX) → TJA1050 TX
- C8T6 PA12(CAN_TX) → TJA1050 RX
- 两块板TJA1050的CANH接CANH，CANL接CANL
- 两块板GND必须共地（重中之重）
- **终端电阻说明**：所用TJA1050模块板载焊死120Ω电阻，无跳线无法断开
  - 点对点双模块并联等效60Ω，短距离（<15cm）500Kbps可正常通信
  - 若扩展≥3个CAN节点，需电烙铁拆除其中一块模块的板载120Ω电阻
  - 阶段4回环模式下不使用外部CAN引脚，无需接线、无需终端电阻

### 串口模拟（阶段0-3）

- ZET6 PA9(TX) → C8T6 PA10(RX)
- ZET6 PA10(RX) → C8T6 PA9(TX)
- GND共地

## 后续规划

- SOC安时积分算法（原理已梳理，项目框架预留接口）
- 功率计算与限幅策略
- UDS完整服务扩展（0x27安全访问、0x34/0x36/0x37下载序列、0x19读DTC）
- ISO-TP多帧传输（当前UDS基于单帧，超8字节数据需ISO-TP分包）
- 真实CAN硬件联调与多节点总线测试
