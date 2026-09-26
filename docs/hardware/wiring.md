# Hardware Wiring — EdgeSense

> **本文件是接线配置的单一权威来源。** Phase 0 的审计发现同一组引脚分散在三个项目的 README 里且一致，因此在这里收敛为一份。
>
> **状态标记说明（重要，不得混淆）**
> - `documented` = 有**既往其他项目**的真机日志/报告记录该配置可用（来自 SensorTrust v0.2、lora-p2p、adaptive-lora-iot 的实测）。
> - `verified` = **本项目 EdgeSense** 在真机上逐项确认过。
>
> **当前所有条目的状态都是 `documented`，没有任何一项是 `verified`。**
> 这不是缺陷，而是 Phase 1 的一项待办（见文末的复核流程）。**在完成复核前，不得把任何条目标注为 `verified`。**

---

## 1. 传感器与 I²C 总线

| 信号 | 配置 | 状态 | 依据 |
|---|---|---|---|
| I²C 总线 | `I2C_NUM_0` | documented | SensorTrust `firmware/main/sensor_reader.c`（`i2c_master` v5.4 驱动） |
| I²C SDA | **GPIO8** | documented | 三处一致：AdaptiveSense `config.h`、SensorTrust `experiment_config.json`、adaptive-lora-iot `EXPERIMENTS.md` |
| I²C SCL | **GPIO9** | documented | 同上三处 |
| **SHT30 地址** | **0x44**（ADDR→GND） | documented | 同上三处；SensorTrust 记 `sensor_address: 68` |
| **BH1750 地址** | **0x23** | documented | AdaptiveSense `CONFIG_AS_BH1750_I2C_ADDR` |
| 土壤湿度 ADC | **GPIO1**（ADC1） | documented | AdaptiveSense `CONFIG_AS_SOIL_ADC_GPIO`；**注意 ADC2 不可用**，且通道必须由 `adc_oneshot_io_to_channel()` 运行时推导 |
| OLED SSD1306（可选） | 共享同一 I²C 总线 | documented | adaptive-lora-iot 硬件清单 |

**引脚避让**：ESP32-S3 上避开 `0 / 3 / 45 / 46`（strapping 与 USB-JTAG 相关）。
**SHT30 另一可选地址**：0x45（ADDR→VCC）。本项目使用 **0x44 / ADDR 接 GND**。

## 2. LoRa E220-400T22D

| 信号 | 配置 | 状态 | 依据 |
|---|---|---|---|
| 模块 | E220-400T22D × 3 | documented | 三个项目的硬件清单 |
| UART 端口 | `UART_NUM_1` | documented | lora-p2p `e220.h`（`#define E220_UART UART_NUM_1`） |
| 波特率 / 格式 | **9600 8N1** | documented | lora-p2p README 与 TEST_REPORT |
| M0 | GPIO13 | documented | 三处一致 |
| M1 | GPIO14 | documented | 三处一致 |
| AUX | GPIO15 | documented | 三处一致 |
| TXD（模块 → ESP RX） | GPIO16 | documented | 三处一致 |
| RXD（ESP TX → 模块） | GPIO17 | documented | 三处一致 |
| VCC / GND | 3V3 / GND | documented | 三处一致 |
| 模块地址 | `0x0000` | documented | 三处一致 |
| REG0 | `0x62` | documented | 三处一致 |
| 信道寄存器 | `0x17`（十进制 23） | documented | 三处一致 |

**纪律（三处一致，不得违反）**
- 启动时对 E220 只做**只读**配置检查（地址 / REG0 / 信道），不一致则判定无线就绪失败；**不写入**模块配置。
- `E220_READY` 的判定条件是"配置读取正确 **且** AUX 已回到空闲高电平"；失败按固定间隔重试。

## 3. 角色分配

| 角色 | 传感器 | 状态 |
|---|---|---|
| Gateway A | 无（可选 OLED 作状态显示） | 规划 |
| Sensor Node B | SHT30 + BH1750 | 规划 |
| Sensor Node C | SHT30 + BH1750（**与 B 同一固件镜像**，差异只在 `node_id` 配置） | 规划 |

**B 与 C 同房间、相隔 30–50 cm**（见 `design_decisions.md` D-01）。

**当前硬件缺口（Phase 1 准入条件 C3）**

只现有 1 颗 SHT30 + 1 颗 BH1750。要同时装备 B 与 C，需**第二套**（B1 已批准购置）。
在第二套到位并验证之前，**C 节点没有传感器，跨节点实验在结构上无法开始**。

## 4. 单节点 I²C 扫描复核流程（Phase 1 执行，完成后才可标 `verified`）

对 **B 与 C 各做一次**，记录原始输出：

```text
1. 烧录探测固件 / 运行 I²C 扫描
2. 期望看到：
     0x44  ACK   ← SHT30
     0x23  ACK   ← BH1750
3. 记录：节点 role / node_id / MAC / 扫描输出 / SHT30 读数 / BH1750 读数 / 时间
4. 逐项对照本文件第 1 节的表，把对应条目的状态从 documented 改为 verified 并附日志路径
```

**未通过的条目保持 `documented` 或改为 `unverified`——不得为了"完成"而标注 verified。**

## 5. 现有真实数据对本表的部分印证

以下**不能替代**上面的复核，但可作为配置可信度的旁证：

| 证据 | 内容 |
|---|---|
| lora-p2p `TEST_REPORT.md` | 两块 ESP32-S3 + E220 启动后读到地址 0000 / REG0 62 / 信道 17，输出 `E220_READY`；600.985 s 内 602 包、序号 30–631 连续、0 丢包（**已由本项目独立复核原始日志**） |
| adaptive-lora-iot Phase 2 | 用上述引脚成功采集 **150 个真实样本**（SHT30 + BH1750 + 土壤经 LoRa），并有 6 条物理干预标注 |
| SensorTrust v0.2 | ESP32-S3 + SHT30（0x44 / SDA8 / SCL9 / 1 Hz）完成 1810 样本 / 30m09s 干净基线与 5 类故障注入 |

**土壤通道的已知问题（迁移时必须修）**：adaptive-lora-iot 的原始数据里 `soil_raw` **恒为 4095 且 `valid=1`**——无介质时探头输出轨到轨满值，但当时 `valid` 是**整帧级**而非逐通道级。EdgeSense 采用**逐通道 validity**，土壤未标定即标为 `valid=false`。土壤校准流程引用 `~/Documents/静冈大学/esp32-agri-node/host/calibrate_soil.py` 的两点校准法（空气 / 水中）。
