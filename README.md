# STM32 VCU-BMS 车载仿真项目

基于STM32F103的VCU-BMS车载仿真项目，使用FreeRTOS + CAN总线，实现动力电池数据采集、SOC估算、功率限制、故障管理等功能。

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

## 开发进度
- [x] 阶段0：裸机外设验证（LED、USART1、OLED）
- [x] 阶段1：VCU FreeRTOS框架（4任务+消息队列+串口模拟CAN）
- [x] 阶段2：BMS模拟器 FreeRTOS
- [x] 阶段3：业务逻辑打磨与模块化
- [ ] 阶段4：接入bxCAN硬件，替换串口通信层
- [ ] 阶段5：CAN总线容错与自测
- [ ] 阶段6：动力电池算法（SOC安时积分、功率计算与限幅）
- [ ] 阶段7：故障管理器FM（故障防抖、DTC、降级策略）
- [ ] 阶段8：环形缓冲区与三层软件解耦
- [ ] 阶段9：简易UDS诊断子集（可选）
- [ ] 阶段10：全场景测试、文档与面试材料

## 硬件接线
### CAN总线（阶段4启用）
- ZET6 PA11(CAN_RX) → TJA1050 TX
- ZET6 PA12(CAN_TX) → TJA1050 RX
- C8T6 PA11(CAN_RX) → TJA1050 TX
- C8T6 PA12(CAN_TX) → TJA1050 RX
- 两块板TJA1050的CANH接CANH，CANL接CANL
- 两块板GND必须共地
- ZET6端TJA1050插120Ω终端电阻，C8T6端不插

### 串口模拟（阶段0-3）
- ZET6 PA9(TX) → C8T6 PA10(RX)
- ZET6 PA10(RX) → C8T6 PA9(TX)
- GND共地
