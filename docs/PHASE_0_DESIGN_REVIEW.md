# EdgeSense Phase 0 — Design Review

**EdgeSense: Fault-Aware Adaptive Sensing and AI-Assisted Diagnosis for Resource-Constrained IoT Networks**

审计日期：2026-09-27
审计范围：本地工作区 + GitHub `Sver0411` 全部相关仓库
审计性质：只读审查。未修改任何被审计仓库的代码，未创建 EdgeSense 运行代码。
本机验证：所有"已本地验证"结论均在本机实际执行（详见附录 A）。

> **⚠️ 本文件已被后续两份文档部分取代（2026-09-27）。**
> 被取代的章节：**§8（主 RQ 收窄）、§9（H1 判据）、§10（"Edge vs Backend detection" 对照已重新定位为 Engineering Evaluation E1）、
> §11（指标报告要求）、§12.1 修改 1（链路质量字段）、
> §12.2（架构图：跨节点融合位置）、§12.3（分层表）、§12.4（数据流）、§12.5（通信流）、§13（节点职责与 Gateway 理由）、
> §14（模块表）、§15.1（时间同步）、§15.2（`network_metrics` / `anomaly_events` 字段）、§16.2（融合层位置）、
> §17（仓库结构）、§18 Phase 5/6 与 **Phase 11 的 EXP-008 定位**、§19 R4、§23（四项决断：已全部批准）、附录 B（模块裁决中 fusion 路径已拆分）**。
> 其余章节（§1–§7、§20–§22、附录 A）仍然有效。
>
> ### 最终冲突优先级：**Phase 0.2 ＞ Phase 0.1 ＞ Phase 0**
> 依序为 `PHASE_0_2_FINAL_DESIGN_FREEZE.md` ＞ `PHASE_0_1_ARCHITECTURE_CORRECTION.md` ＞ 本文件。
> 任何冲突以序号大者为准。
>
> **关于"阻塞"字样**：本文件中所有标记为阻塞的项目（第二套传感器、`adaptive-lora-iot` 归属、主 RQ 收窄、
> 频段表述）**已在 Phase 0.2 全部批准**。因此 **§7 G2、§13 的阻塞提示、§19 R1、§23 中的"阻塞"字样一律作废**，
> 以 Phase 0.2 §0 的批准表为准。

---

## 1. Executive Summary

**结论：EdgeSense 不必从零开始，但它现在的最大风险不是"缺代码"，而是"已有资产太多、研究问题会互相打架"。**

三个关键发现：

**（一）你的已有工程质量显著高于一般作品集项目，可以直接作为 EdgeSense 的基座。**

- **SensorTrust**：C11 可移植核心，语义已冻结；本机复跑 **27 个 pytest 全通过**、**21 个 C 测试 / 404 个断言全通过** 且满足 `-Wall -Wextra -Werror`。已有真实 ESP32-S3 + SHT30 的 30 分钟干净基线与 5 类故障注入证据管线。
- **AdaptiveSense**：离线回放仿真 + 真机固件 **双实现**，并用 parity 测试保证两者决策逐步一致。本机验证其 6 个策略源文件在 `-Werror` 下全部零警告。
- **EventGuard-LoRa**：已有冻结规范、8000 次主机仿真、**96 次真机运行**与 96/96 通过的离线审计。这是极高标准的科研工程实践，**但它已经占掉了一个研究问题**。

**（二）你漏报了一个最相关、也最接近 EdgeSense 的项目。**

`~/Documents/ChatGPT/paper/adaptive-lora-iot` —— 它已经跑通了 **真实 SHT30 + BH1750 + 土壤传感器经 LoRa 上传的 5 秒固定参考流（150 个真实样本）**，并且有 **6 次物理干预标注**（遮光、补光、呼气加湿）。数据里能直接看到湿度 55.6% → 90.6% → 73.8% 的真实事件形态——这正是你 prompt 里描述的那个场景。

也就是说：**EdgeSense 的 Phase 1–2（多传感器节点 + LoRa 数据帧 + 真实参考流采集 + 事件标注）已经存在了**，只是它没有被发表、也没有被命名进 EdgeSense。这个项目当前 **未推送到 GitHub**，是本轮审计发现的最大资产浪费。

**（三）真正的空白只有一处，而它恰好是最有价值的研究点。**

已有资产各自的覆盖：

| 已有能力 | 归属 | 是否构成研究贡献 |
|---|---|---|
| 单通道传感器故障检测 | SensorTrust | 已占（v0.1 语义冻结） |
| 变化感知自适应采样 vs 定频 | AdaptiveSense | 已占（RQ 与你候选的 RQ2 基本重合） |
| 冗余预算 vs 关键事件投递 | EventGuard-LoRa | 已占（且得出否定性结论） |
| **跨通道 + 跨节点证据区分"传感器故障"与"真实环境事件"** | **无人做** | **空白** |

因此 Phase 0 的核心建议是：**主 RQ 必须落在第三行，而不是 RQ1 或 RQ2。** RQ1 是 SensorTrust 的延伸，RQ2 是 AdaptiveSense 的重做——把它们当主 RQ，等于用 EdgeSense 覆盖自己的两个已完成项目，研究增量会被评审直接质疑。

**裁决倾向：本审计对 15 个模块给出 GO/REFACTOR/DROP，其中 4 个 GO、6 个 REFACTOR、5 个 DROP。** 详见第 18 节与文末汇总表。

**一个必须先解决的阻塞项：** 你只有 **1 颗 SHT30 + 1 颗 BH1750**。跨节点证据要求两个节点测量**同一物理量**。当前硬件下，"跨节点"这一半的 RQ **无法做**。加购 1 颗 SHT30 + 1 颗 BH1750（约 ¥30–60）是整份路线图里性价比最高的投入。详见第 12、19 节。

---

## 2. Existing Repository Audit

### 2.1 已审计清单

| # | 项目 | 路径 | 是否有 git | 是否已推送 GitHub |
|---|---|---|---|---|
| 1 | SensorTrust | `~/Documents/SensorTrust` | ✔ | ✔ `Sver0411/SensorTrust` |
| 2 | AdaptiveSense | `~/Documents/AdaptiveSense` | ✔ | ✔ `Sver0411/AdaptiveSense` |
| 3 | TinyEdgeBench | `~/WorkBuddy/TinyEdgeBench` | ✔ | ✔ `Sver0411/TinyEdgeBench` |
| 4 | EventGuard-LoRa | `~/Documents/ChatGPT/EventGuard-LoRa` | ✔ | ✔ `Sver0411/EventGuard-LoRa` |
| 5 | lora-p2p | `~/Documents/ChatGPT/paper/lora-p2p` | ✔（父仓库） | ✘ |
| 6 | **adaptive-lora-iot** | `~/Documents/ChatGPT/paper/adaptive-lora-iot` | ✔（父仓库） | ✘ **未发表** |
| 7 | EdgeSense-Fusion | `~/Documents/ESP32/EdgeSense-Fusion` | ✘ **无 git** | ✘ |
| 8 | Smart-Agriculture-Edge-AI | `~/Documents/ChatGPT/Smart-Agriculture-Edge-AI` | ✔ | ✔ `Sver0411/Smart-Agriculture-Edge-AI` |
| 9 | EdgeFaultLab | 仅 GitHub | — | ✔ `Sver0411/EdgeFaultLab` |
| 10 | esp32-agri-node | `~/Documents/静冈大学/esp32-agri-node` | ✔（父仓库） | ✘ |
| 11 | `ChatGPT/edge/esp32_e220_*` ×5 | `~/Documents/ChatGPT/edge/` | ✔（父仓库） | ✘ |

---

### 2.2 SensorTrust

- **项目目的**：判断"某条传感读数是否值得信任"。不决定何时采样、何时上传。
- **技术栈**：C11（可移植核心）+ ESP-IDF v5.4 固件 + Python 证据管线（capture / evaluate / plot / render_readme）
- **当前完成度**：v0.2。v0.1 的 5 类故障语义已冻结（RANGE / STUCK / SPIKE / DRIFT / MISSING）。
- **可复用模块**：`core/sensor_trust.c`（569 行）与 `core/sensor_trust.h`（284 行）——无动态内存、无 RTOS、无硬件头文件，同一份代码在 ESP32 与 PC 上运行；`firmware/main/fault_injector.c`；`hardware/` 证据管线（这是它最有价值的部分，因为它把"读数 → 注入真值 → 指标"整条链固化成了可重跑脚本）。
- **不值得复用**：只能吃单通道（`sensor_trust` 的输入契约是"一条 channel 的样本流"）；`OFFSET` 是明确记录的盲区（恒定偏置检测不到）。
- **技术债**：低。评价通道只覆盖 `temperature_C`，`humidity_percent` 由驱动读取但未评价，BH1750 未测——README 已明确写出，属于诚实标注而非隐瞒。
- **测试情况**：**本机实测通过**。`pytest tests/` → 27 passed in 3.55s；`cc -std=c11 -Wall -Wextra -Werror core/sensor_trust.c tests/test_core.c` → 21 tests / 404 checks 全通过；`test_injector.c` → "7 injection modes and invalid-read preservation passed"。
- **真实硬件验证**：有。ESP32-S3 + SHT30，1810 样本 / 30 分 09 秒干净基线，5 类故障 × 5 次共 25/25 episode 检出；固件 commit、配置 SHA-256、原始日志 SHA-256 全部落盘。
- **README 声明是否夸大**：**否，是本轮审计中措辞最克制的项目**。它主动写"这只描述本次观测，不构成普适零误报率"、"health_score 是启发式严重度分数，不是校准概率"、"检测到故障意味着数据可疑，不等于传感器损坏"。这类表述方式应当成为 EdgeSense 的书写标准。

---

### 2.3 AdaptiveSense

- **项目目的**：变化感知的自适应采样能否在**不牺牲事件检出**的前提下降低感知与通信开销。README 明确表态"并报告答案不是完全肯定的地方"。
- **技术栈**：ESP-IDF v5.4 C 固件（传感器抽象层支持 SHT30/BME280/BH1750/土壤 ADC + 变化检测 + 调度器 + Wi-Fi/MQTT 通信 + 电源管理）+ Python 离线仿真（replay / metrics / analysis）
- **当前完成度**：v0.2，有 CI 徽章（Python tests + ESP-IDF build 两条流水线）。
- **可复用模块**：
  - `simulator/`（replay / metrics / adaptive / scoring / events / config）——**这就是 EdgeSense 需要的离线评测框架**，且已有 7 个标注场景 / 26,400 秒 1 Hz 信号 / 24 个通道级标签 / 6 条 baseline。
  - `change_detector.c` + `adaptive_scheduler.c`——策略本体，可移植、可主机编译。
  - `scripts/check_config_parity.py`——YAML 与 `config.example.h` 不一致就 CI 失败，防止调参只改一边。
  - `tests/test_parity_python_c.py`——保证 C 与 Python 实现决策逐步一致。
  - `sensor_supervisor.c`——"传感器停止应答后重新探测"的可用性状态机，这正是 EdgeSense 的 I²C 故障恢复要的。
- **不值得复用**：`communication.c`（Wi-Fi + MQTT）——EdgeSense 走 LoRa。
- **技术债**：
  1. **本机不可复现**：缺少 venv，`import yaml` 失败，`tests/host_build.py` 因无 pytest 无法运行。这意味着"按 README 克隆即可复现"在本机并不成立（CI 上成立）。
  2. 无 `requirements.txt` 级别的锁定证据核查（有 `requirements.txt`，但本机未装）。
- **测试情况**：CI 上有；**本机未跑通**（依赖缺失）。**本机已独立验证的部分**：`change_detector.c`、`adaptive_scheduler.c`、`sht30_proto.c`、`bme280_math.c`、`soil_moisture_math.c`、`communication_payload.c` 六个文件在 `-std=c11 -Wall -Wextra -Werror` 下**全部零警告**。
- **真实硬件验证**：有（`results/live/MQTT-longrun.csv`、`MQTT-net.csv` 记录完整网络链）。但**已发布的数字全部是合成仿真**，README 对此区分得非常清楚。
- **README 声明是否夸大**：**否**。README 主动纠正读者可能的误读，例如"application upload reduction 是应用层指标，不是无线电流量指标；上传减少 97% 不等于总无线电流量减少 97%，因为 keepalive 本身就有背景流量"。
- **与研究问题的关系（重要）**：AdaptiveSense 的 RQ = "变化感知自适应采样能否降低开销同时保持事件检出"，**与你候选的 RQ2 几乎是同一个问题**。把 RQ2 定为 EdgeSense 主 RQ 会造成自我重复。

---

### 2.4 TinyEdgeBench

- **项目目的**：同一传感器任务上，复杂度递增的 4 种轻量分类方法（手写规则 / 逻辑回归 / 决策树 / 微型 MLP）各自买回多少精度、付出多少内存。
- **技术栈**：Python（scikit-learn）+ C 代码导出 + ESP-IDF（真实编译测 flash）
- **当前完成度**：v0.1，已定稿。
- **可复用模块**：
  - `benchmark/measure_flash.py`——**这是真货**：用 `-DTINYEDGEBENCH_MODEL=...` 分别构建 5 次，从 ELF 段大小读**真实** flash 增量（基线 189,492 字节），不是估算。
  - `training/export_models.py`——把模型导出为纯 C 的浮点推理代码。
  - `tests/` 含 Python/C parity 检查。
- **不值得复用**：合成数据集与"哪个模型更准"的结论。它的数据集是合成的（20,000 行 / 4 类），而 EdgeSense 的判别问题是"故障 vs 事件"，任务定义不同。
- **技术债**：低。README 有一处非常好的自我约束——明确区分"原始常数字节数"与"编译后 flash 增量"两列的不同含义，并指出决策树根本没有参数数组、不能和其他行并列比较。
- **测试情况**：本机未跑（需要 ESP-IDF 环境 + 未装 pytest）。
- **真实硬件验证**：**无**。README 明确列表：flash 是构建期测量（不需要硬件），**硬件延迟未测 / 真实 RAM 未测 / 功耗未测 / 单模型 RAM 未分离**。
- **README 声明是否夸大**：否。它主动标注 `firmware/main/main.c` 里的 `measure_latency()` 只是"留给有板子之后用的钩子"，在此之前不发布任何延迟、RAM、功耗数字。
- **客观判断**：它回答的是"在 ESP32 上哪种轻量方法值得用"，这是**部署评估**问题，不是**运行时组件**。EdgeSense 需要它的测量方法，不需要它进数据通路。

---

### 2.5 lora-p2p

- **项目目的**：两块 ESP32-S3 + E220-400T22D 的最小点对点通信验证。
- **技术栈**：ESP-IDF v5.4，驱动 `e220.c`（118 行）+ node_s（113 行）+ node_g（177 行）+ 两个主机侧串口统计脚本。
- **当前完成度**：完成，有 TEST_REPORT.md。
- **可复用模块**：E220 的**引脚映射与只读配置探测**（地址 0000、REG0 0x62、信道 0x17，`E220_READY` 前 5 秒重试）与 `monitor_gateway.py` 的统计口径。
- **不值得复用**：`e220.c` 本体。它只有 4 个 API（init / probe / wait_aux_high / event_queue），没有流式解析器；而 EventGuard 的 `firmware/common/e220.c` + `e220_stream_parser.c` 已经是同一硬件上**更成熟、已被 96 次真机运行验证**的版本。保留两份驱动是纯负债。
- **技术债**：无重大技术债，但作为独立项目已到生命周期终点。
- **测试情况**：无自动化测试（它是实验项目，不是库）。
- **真实硬件验证**：**有，且本机独立复核通过**。我直接解析 `logs/20260925-011325-gateway.log`：RX 行 602 条，序号 30–631 **完全连续**，重复 0，panic/abort 0 次。TEST_REPORT 的数字站得住。
- **README 声明是否夸大**：**否，且写法值得学习**。它主动写了"计数边界"一节：Node S 未接电脑，发送数是由首尾序号差推定的，而不是独立采集 TX 日志；并明确"测试证明的是两块 E220 在此摆放、供电和无线环境下连续运行 10 分钟；未测试更远距离或其他信道条件"。这正是你要求的"不能写成 reliability = 100%"的正确写法。

---

### 2.6 adaptive-lora-iot（**审计新发现，你未在 prompt 中提及**）

- **项目目的**：`lora-p2p` 的直接续作。Phase 2 从 SHT30 + BH1750 + 电容土壤探头采集 5 秒参考流，经 LoRa 发送 `DATA,<seq>,<uptime_ms>,<temp>,<hum>,<lux>,<soil_raw>`；主机侧落 CSV，并用 `add_event.py` 在物理干预发生的**当下**记录事件。
- **技术栈**：ESP-IDF v5.4（`firmware/node_s` 含 `adaptive_policy.c`/`sensors.c`，`firmware/node_g` 为网关）+ Python 采集与离线分析（`analysis/`：load_data / preprocess / baseline / adaptive_sampling / metrics / statistics / plot_results / run_all）+ LaTeX 论文骨架（`paper/sections/*.tex` 8 节已分文件）+ `docs/LITERATURE_REVIEW.md` / `HARDWARE_REVIEW.md` / `LINK_ADAPTATION.md`。
- **当前完成度**：Phase 2 已产出**真实数据**。
- **真实数据实况（本机直接读取）**：
  - `data/raw/20260925-phase2-main.csv`：**150 个样本**，5 秒间隔，字段 `host_timestamp,seq,node_timestamp_ms,temperature_C,humidity_pct,light_lux,soil_raw,valid`。
  - 事件标注 `20260925-phase2-main-events.csv`：6 条 —— `shade_start` / `shade_end` / `light_boost_start` / `light_boost_end` / `humidity_change_start`，以及一条"节点上电、无扰动"。
  - 数据形态真实可信：湿度从 55.6% 升到 **90.6%**（呼气加湿事件）再回落到 73.8%；光照在遮光/补光期间同步变化。
- **可复用模块**：**几乎全部**。`firmware/node_s` 的多传感器节点 + LoRa DATA 帧 + `adaptive_policy.c` 骨架；`experiments/collect.py`（不可变时间戳落盘）；`experiments/add_event.py`（物理干预即时标注）；`analysis/`（在真实参考流上评测定频 vs 自适应）；`REPRODUCIBILITY.md`。
- **技术债（我实测发现的一处真实缺陷）**：**`soil_raw` 恒为 4095 且 `valid=1`**。土壤探头无介质时输出轨到轨满值，但当前的 `valid` 是**整帧级**标志，不是**逐通道**标志，所以一个事实上已失效的通道被上报为"有效"。这是个具体、可修、且能写进 limitations 的问题。AdaptiveSense 的 `sensor.h` 是逐通道 validity（不能测的通道上报 `null` + 显式 valid map），EdgeSense 应当采用后者。
- **测试情况**：`analysis/` 有模块划分但未见测试目录。
- **README 声明是否夸大**：否。`EXPERIMENTS.md` 明确写"5 秒流是密集参考，不是校准过的物理真值"，并明确"PING 的 602 次发送是由序号推定的，不是在 Node S 独立记录的"。
- **审计结论**：**这是 EdgeSense 最直接的基座，也是最严重的沉默资产——它没有 GitHub 仓库，只有一个未发表的父仓库。** 它的 Phase 2 就是 EdgeSense 的 Phase 1–2。

---

### 2.7 EdgeSense-Fusion

- **项目目的**：多模态传感融合边缘 AI——DHT22 + MPU6050 + BH1750 → ESP32-S3 → Wi-Fi/MQTT → MacBook 边缘服务器 → 校验/持久化/滑窗特征/Isolation Forest/Web 可视化。
- **技术栈**：FastAPI + SQLAlchemy + SQLite + paho-mqtt + ECharts + PlatformIO/Arduino 固件 + docker-compose。
- **当前完成度**：软件链路完整（含 4 个测试文件），但**无 git 仓库**。
- **可复用模块**：`backend/app/` 的分层（api / database / schemas / services / mqtt / ai / fusion）、Pydantic schema 校验思路、`docker-compose.yml`、`tools/seed_demo_data.py` 与"演示数据必须显式隔离"的做法。这是你**唯一的后端 + Dashboard 现成资产**。
- **不值得复用**：
  - **Isolation Forest 异常检测**——与 EdgeSense 的确定性判别 RQ 冲突，且它训练在演示数据上。
  - **DHT22 / MPU6050 硬件路线**——与 EdgeSense 硬件无关。
  - **Wi-Fi/MQTT 传输**——EdgeSense 走 LoRa。
  - 现有 SQLite schema——需要按 trust / sampling / 事件模型重新设计。
- **技术债**：**高**。① 无版本控制（连 `git init` 都没有），这在一个"科研工程"项目里是必须立刻修的问题；② 仓库内已提交 `data/database.db` + `-shm` + `-wal`（3.2 MB WAL）与训练好的 `.joblib` 模型，属于不该进版本库的产物。
- **测试情况**：有 4 个 pytest 文件（`test_api` / `test_ingestion` / `test_mqtt_handler` / `test_feature_extraction`），本机未跑。
- **真实硬件验证**：**无。** 我直接读了它的数据库：`devices` 只有 1 行 `demo_esp32_001`，`sensor_data` 110 行，数值是 24.0 → 24.03 → 24.06 这种**平滑合成的斜坡**，时间戳集中在 4 分钟内。这是 `seed_demo_data.py` 的产物，不是真实采集。
- **README 声明是否夸大**：**部分是。** README 写"EdgeSense Fusion 是一个可运行的科研型 IoT + AI 工程……ESP32-S3 采集 DHT22、MPU6050 与 BH1750 的物理数据"，而仓库内**没有任何真实设备数据**，也没有硬件运行日志或证据文件。它确实做了正确的诚实防护（"未接入硬件时数据库保持为空、AI 状态为 collecting_baseline"），但开头的"物理数据"表述与实际状态不符。**这一条必须在 EdgeSense 里修正措辞，否则它会成为整个作品集里最容易被审稿人抓的破口。**
- **附加风险**：它已经叫 "EdgeSense Fusion"，与你新建的 EdgeSense 同名。两个 EdgeSense 并存会造成严重的作品集混乱。

---

### 2.8 Smart-Agriculture-Edge-AI

- **项目目的**：四类节点（云端服务器 / 边缘网关 A1,A2 / 传感器节点 B1,B2 / 控制器节点 C1,C2）的多节点链路原型，v0.2 主题是工程加固：节点注册与归属、网关故障接管、ACK + 重试、幂等执行 + epoch 校验、断网本地排队。
- **技术栈**：纯 Python asyncio，节点间本机 TCP。**README 明确写"当前版本全部为软件模拟，不连接任何真实硬件"。**
- **当前完成度**：v0.2 完成，有 4 个可复现场景与指标汇总。
- **可复用模块（概念层）**：消息协议词汇（`message_id` 去重、ACK 与 `CONTROL_RESULT` 分离、`policy_version`、origin epoch）、7 个互不阻塞的 asyncio 任务分解方式、故障场景清单的写法。
- **不值得复用（代码层）**：整个运行时。它的研究重心是**网关故障接管 + 执行器安全**，而 EdgeSense 明确禁止任何控制/执行器路径，二者的核心命题不同。照搬会引入大量与 EdgeSense RQ 无关的复杂度。
- **技术债**：无（作为独立的仿真项目它自洽）。
- **真实硬件验证**：无，且已明确声明。
- **README 声明是否夸大**：否，声明与实际完全一致。
- **客观判断**：**它是一个好的独立作品，但它不是 EdgeSense 的一部分。** 它的节点框图里已经把 SensorTrust 和 AdaptiveSense 画进了 B1/B2——这说明你此前已经在概念上把它们拼接起来了，而 EdgeSense 要做的是给这个拼接一个**真的硬件通路和一个真的研究问题**，而不是复用这套仿真骨架。

---

### 2.9 EventGuard-LoRa

- **项目目的**：在精确的 DATA-copy 预算下，按事件重要性与链路状态分配冗余，能否比预算匹配的策略投递更多关键事件。
- **技术栈**：Python 参考实现 + 主机仿真器 + 真机运行编排 + ESP-IDF 固件（`firmware/common/`：`e220.c` / `e220_stream_parser.c` / `protocol.c` / `faults.c` / `importance.c` / `strategy.c` / `sensor_drivers.c`）+ LaTeX 手稿。
- **当前完成度**：v1.0 研究原型完成，已冻结规范。
- **可复用模块（高价值）**：
  - **传输层**：`firmware/common/e220.c` + `e220_stream_parser.c` + `protocol.c`——带 CRC 的紧凑二进制帧、去重、ACK、UART 流式解析。这是 EdgeSense 通信层的正确来源（而不是 lora-p2p 的 118 行驱动）。
  - **实验编排**：`tools/run_all_experiments.py` / `run_hardware_validation.py` / `finalize_hardware_dataset.py` / `archive_stage1_failed_attempt.py`——两个角色经 USB 同时连主机、捕获原始串口日志、保存 run manifest 与固件哈希。
  - **审计范式**：`results/final_hardware_v1/audit_report.md`——96/96 通过，逐项复核 manifest/raw SHA256、冻结固件哈希、trace 与丢包日历哈希、逐副本 DATA/ACK 结果、重要性/副本/链路策略回放、END 计数器。**这套审计写法就是 EdgeSense 的 experiments 框架应当达到的标准。**
  - **故障注入**：`firmware/common/faults.c` + `eventguard/faults.py`——确定性的应用层丢包计划（按 seed/frame kind/sequence/copy index 索引），并**明确区分应用层注入丢包与物理 RF 丢包**。
- **不值得复用**：`importance.c` / `strategy.c` 及其 RQ。它是"哪些样本值得多花副本"，与 EdgeSense 的"读数可不可信"是**两个不同的问题轴**，混在一起会同时污染两边的研究叙事。
- **技术债**：`.venv` 已损坏（本机 `import pytest` 直接 Traceback），所以 README 里"克隆即可复跑分析"在本机不成立。
- **测试情况**：有 6 个测试文件 + 一个 C 侧 `e220_stream_parser_test.c`；本机未跑（venv 坏）。
- **真实硬件验证**：有，96 次真机运行 + 离线审计 96/96 + 保留失败案例（run103 receive-path stall 的原始证据保留且**明确声明原因未确认**）。
- **README 声明是否夸大**：**否，且是本轮审计中自我否定最彻底的一份**。它主动报告了**对自己不利**的结果：`IMPORTANCE_ONLY` 在全部 6 个配对种子上都与 `EVENTGUARD` 达到相同的关键事件投递率，而后者平均多用了 21–33 个 DATA 副本；EventGuard 在 10 条 Pareto 前沿里占据 **0 条**。并且明确写"六个配对种子下这些发现是方向性的、条件特定的……既不支持确证性显著性声明"。这种"保留否定结果"的能力，在修士申请材料里比任何正面数字都有说服力。

---

### 2.10 EdgeFaultLab

- **项目目的**：位于系统节点之间、注入**语义级**故障（丢一条 `CONTROL_COMMAND`、重复命令、陈旧时间戳、网关中途死亡），然后断言"系统是否仍然遵守它的承诺"，不满足就非零退出，从而可以卡 CI。
- **技术栈**：纯 Python 3.10+，**零运行时依赖**，不碰内核网络。
- **当前完成度**：139 KB，创建于 2026-09-16，之后未再更新。
- **可复用模块（概念层）**：断言引擎与"scenario → inject → observe → assert → report"的范式；"把『我们处理了重复』从设计文档里的一句话变成一个测试结果"这个立意非常好。
- **不适合 EdgeSense 的原因**：它是 TCP 代理 + 分布式软件语义。EdgeSense 的故障绝大部分发生在**固件内部（I²C）与真实无线电链路上**，两者不在同一层。它的价值在 EdgeSense 的**后端/网关服务**那一侧（Phase 7 之后），而不是固件侧。
- **真实硬件验证**：不适用。
- **README 声明是否夸大**：否，且对自己的定位很克制（明确说"不是 Toxiproxy / tc / netem 的替代品，工作层次更高"）。

---

### 2.11 esp32-agri-node

- **项目目的**：农业节点上的"跨场景迁移 + 少样本适配 + 不确定性评测"闭环，Arduino 固件 + 主机采集/土壤两点校准/实时打标 + HTML 报告。
- **可复用模块**：**传感器接线与校准流程**（`docs/wiring.md`、`host/calibrate_soil.py`——土壤探头两点校准，这个 EdgeSense 用得上）；`host/label_session.py` 的实时打标思路。
- **不适合 EdgeSense 的原因**：它的研究问题是"跨场景迁移与少样本适配"，与 EdgeSense 的"故障 vs 事件判别"正交。这是**另一条研究线**（且明显是为静冈大学方向准备的）。
- **真实硬件验证**：声明为全部真实硬件采集。
- **结论**：保留独立，不并入。但土壤校准与接线文档应被 EdgeSense 引用。

---

### 2.12 早期 bring-up 项目

`~/Documents/ChatGPT/edge/` 下的 `esp32_e220_test`、`esp32_e220_mode0_test`、`esp32_e220_aux_test`、`esp32_e220_receiver`、`esp32_sensor_e220_node`——E220 各阶段的探路工程。全部已被 `lora-p2p` → `adaptive-lora-iot` → `EventGuard-LoRa` 覆盖。**归档，不再维护。**

---

## 3. Reusable Components

按"直接可用 / 需重构后可用 / 仅概念可用"三档：

**A. 可直接复用（本机已独立验证）**

| 组件 | 来源 | 验证证据 |
|---|---|---|
| `core/sensor_trust.c` + `.h`（C11 可移植核心） | SensorTrust | 本机 `-Wall -Wextra -Werror` 编译通过，21 tests / 404 checks 全过 |
| `firmware/main/fault_injector.c` | SensorTrust | 本机 "7 injection modes and invalid-read preservation passed" |
| `hardware/{capture,evaluate,plot_results,render_readme}.py` | SensorTrust | 目录与脚本齐备（本轮未执行，需串口） |
| `change_detector.c` / `adaptive_scheduler.c` | AdaptiveSense | 本机 `-Werror` 六文件零警告 |
| `simulator/` 全套（replay/metrics/adaptive/scoring/events） | AdaptiveSense | 7 场景 + 24 标签 + 6 baseline 结构完整 |
| `scripts/check_config_parity.py` 与其 CI 思路 | AdaptiveSense | 读代码确认逻辑（YAML ↔ C 头文件不一致即失败） |
| E220 引脚映射与只读配置探测约定 | lora-p2p / adaptive-lora-iot / EventGuard | 三个项目互相一致；lora-p2p 日志本机复核 602/602 |

**B. 需重构后可用**

| 组件 | 来源 | 需要的重构 |
|---|---|---|
| `firmware/common/{e220,e220_stream_parser,protocol}.c` | EventGuard-LoRa | 剥离 importance/strategy；保留 CRC 帧 + 去重 + ACK + 流解析；扩展 payload 为多通道 + trust 字段 |
| `tools/run_hardware_validation.py` 系列 | EventGuard-LoRa | 泛化为 EdgeSense 的 `experiments/` 运行器：manifest + 固件哈希 + 原始日志 + 审计 |
| `firmware/node_s`（多传感器 + LoRa DATA 帧 + `adaptive_policy.c`） | adaptive-lora-iot | 逐通道 validity 修复；接入 SensorTrust；节点身份可配置（B/C 同一镜像） |
| `experiments/{collect,add_event}.py` | adaptive-lora-iot | 保留不可变落盘与即时标注的设计，纳入 EXP-xxx 目录规范 |
| `analysis/`（定频 vs 自适应离线评测） | adaptive-lora-iot | 与 AdaptiveSense 的 simulator 合并去重（两者功能重叠） |
| `backend/app/` 分层 + Pydantic schema + docker-compose | EdgeSense-Fusion | 传输改 LoRa 网关；SQLite → PostgreSQL；schema 按 trust/sampling/事件模型重设计；先 `git init` |
| `dashboard/`（原生 HTML/CSS/JS + ECharts） | EdgeSense-Fusion | 从"演示首页"改为"工程状态首页"（节点在线/告警/投递率/当前采样率） |
| `benchmark/measure_flash.py` + `training/export_models.py` | TinyEdgeBench | 目标换成 EdgeSense 的判别器候选，而非 4 类合成分类任务 |
| `sensor_supervisor.c`（可用性状态机） | AdaptiveSense | 从"单传感器重探测"扩展到"多通道 × 多故障类型" |
| `host/calibrate_soil.py` + `docs/wiring.md` | esp32-agri-node | 直接引用，无需改写 |

**C. 仅概念可用**

- Smart-Agriculture-Edge-AI 的消息协议词汇（`message_id` 去重、ACK 与结果分离、`policy_version`）与故障场景写法。
- EdgeFaultLab 的"断言 + 非零退出卡 CI"范式（用于后端侧集成测试）。
- SensorTrust 的**证据写法**：把"读原始日志 → 生成指标 → 渲染 README"做成脚本（`render_readme.py`），使人无法手改数字。**这个方法本身是 EdgeSense 应当继承的最重要资产。**

---

## 4. Components That Should Not Be Reused

| 组件 | 理由 |
|---|---|
| `lora-p2p/firmware/**/e220.c` | 无流式解析器，已被 EventGuard 在同硬件上验证过的版本取代。两份驱动 = 双份维护成本 |
| `AdaptiveSense/firmware/main/communication.c`（Wi-Fi + MQTT） | EdgeSense v1 无 Wi-Fi；保留会让节点功耗与复杂度无谓上升，且重复 EdgeSense-Fusion 已走过的路 |
| `EdgeSense-Fusion` 的 Isolation Forest 检测器 | 非确定性、训练在演示数据上，与"算法必须 deterministic / statistically defined / testable"的要求冲突 |
| `EdgeSense-Fusion` 的 DHT22 / MPU6050 固件路线 | 与 EdgeSense 硬件清单无关 |
| `EdgeSense-Fusion` 的 SQLite schema 与已提交的 `database.db*` / `models/*.joblib` | schema 需重设计；二进制产物不应进版本库 |
| `TinyEdgeBench` 的合成数据集与其 4 类分类结论 | 任务定义不同（窗口行为分类 vs 故障/事件判别） |
| `Smart-Agriculture-Edge-AI` 的全部运行时代码 | 研究命题不同（网关接管 + 执行器安全），而 EdgeSense 明确禁止控制路径 |
| `EventGuard-LoRa` 的 `importance.c` / `strategy.c` 及其 RQ | 会与 SensorTrust 的"可信度"混淆成同一个轴，两边叙事同时受损 |
| `EdgeFaultLab` 作为固件故障注入器 | 层次不匹配（TCP 语义代理 vs 片内 I²C 与 RF 链路） |
| `esp32-agri-node` 的分析管线 | 研究问题正交，属于另一条线 |
| `ChatGPT/edge/esp32_e220_*` ×5 | 已被后续三层项目完全覆盖 |

---

## 5. Current Technical Debt

按严重度排序，**前三条是必须在本轮之后立刻处理的**：

| # | 严重度 | 债务 | 位置 | 建议 |
|---|---|---|---|---|
| 1 | **高** | **`adaptive-lora-iot` 是 EdgeSense 最直接的基座，却完全没有 GitHub 仓库、未发布。** 你最有价值的真机数据（150 个真实多传感器样本 + 6 次物理事件标注）目前只存在于本地 | `~/Documents/ChatGPT/paper/adaptive-lora-iot` | 拆成独立仓库并发布，或作为 EdgeSense 的首个模块迁入 |
| 2 | **高** | **`EdgeSense-Fusion` 没有版本控制**，且已把 `database.db`（3.2 MB WAL）与 `.joblib` 模型提交进目录 | `~/Documents/ESP32/EdgeSense-Fusion` | `git init` + 补 `.gitignore`；清理二进制产物 |
| 3 | **高** | **`EdgeSense-Fusion` 的 README 声称采到了"物理数据"，但仓库内只有 `seed_demo_data.py` 生成的 `demo_esp32_001` 合成斜坡** | 同上 | 修正措辞为"软件链路已验证，硬件接入待完成" |
| 4 | 中 | **三个最重要仓库在"克隆即复现"这一点上本机都不成立**：AdaptiveSense 无 venv 且缺 PyYAML、EventGuard 的 `.venv` 已损坏、TinyEdgeBench 未装 pytest | 三个仓库 | 统一补 `python -m venv` + `pip install -r` 的预检脚本；CI 绿≠本机可跑 |
| 5 | 中 | **`adaptive-lora-iot` 的 `valid` 是整帧级而非逐通道级**，导致恒为 4095 的死土壤通道被上报为有效 | `data/raw/*.csv` 实测 | 改为逐通道 validity（采用 AdaptiveSense 的 `sensor.h` 契约） |
| 6 | 中 | **`adaptive-lora-iot/analysis/` 与 `AdaptiveSense/simulator/` 功能重叠**（都在做定频 vs 自适应的离线评测），两套 metrics 实现会算出两个数 | 两处 | 合并为一套，另一套删除并留迁移说明 |
| 7 | 中 | **存在两条并行的"adaptive + LoRa"研究线**（`adaptive-lora-iot` 的 Phase 2+ 与 `AdaptiveSense`），RQ 高度重合 | 两处 | Phase 0 决议：`adaptive-lora-iot` 转为 EdgeSense 的系统基座，`AdaptiveSense` 转为被引用的算法库与 baseline 来源 |
| 8 | 低 | 命名冲突：`EdgeSense-Fusion` 与新建 `EdgeSense` 同名 | 两处 | EdgeSense-Fusion 归档改名（如 `EdgeSense-Fusion-ARCHIVED`） |
| 9 | 低 | SensorTrust 的评价通道只覆盖 `temperature_C`，`humidity_percent` 未评价、BH1750 未测 | SensorTrust | 已在 README 声明，EdgeSense 需补 humidity 通道 |
| 10 | 低 | 早期 bring-up 项目 ×5 仍散落在 `ChatGPT/edge/` | 该目录 | 归档 |

**一个重要的正面事实**：SensorTrust 与 EventGuard-LoRa 的技术债**主要由"环境不可复现"而非"代码质量"构成**。它们的代码写得比大多数已发表的研究原型更规范。所以 EdgeSense 的风险不在"继承烂代码"，而在**"把两个好项目的研究问题搞重叠"**。

---

## 6. Capability Map

| Capability | Existing Project | Current Status | Reusable? | Required Refactor | EdgeSense Role |
|---|---|---|---|---|---|
| 单通道传感器健康判定（5 类故障） | SensorTrust | 真机验证，语义冻结，本机测试全过 | ✔ GO | 扩展为 N 通道 + 接受跨通道证据输入 | Sensor Reliability Engine 的内核 |
| 物理故障注入（可由真值独立标注） | SensorTrust | 真机验证，25/25 episode 检出 | ✔ GO | 从 1 通道泛化到 N 通道 + 跨节点 | Fault Injection Framework |
| 原始读数→指标→图表的证据管线 | SensorTrust | 可用 | ✔ GO | 泛化目录与命名 | `experiments/analyze_experiment.py` |
| 变化感知自适应采样（策略） | AdaptiveSense | 真机 + CI，本机编译零警告 | ✔ GO | 接受 trust 输入；输出改走 LoRa | Adaptive Sampling Engine |
| 离线回放评测框架（baseline vs 自适应） | AdaptiveSense | 7 场景 / 24 标签 / 6 baseline | ✔ GO | 与 adaptive-lora-iot 的 analysis 合并 | 评测框架骨架 |
| C↔Python 决策一致性 parity 测试 | AdaptiveSense | CI 中 | ✔ GO | 扩展到 trust + fusion 决策 | 防止固件与仿真分叉 |
| 配置一致性检查（YAML ↔ C 头） | AdaptiveSense | CI 中 | ✔ GO | 增加节点角色与引脚配置 | 配置治理 |
| 多传感器节点 + LoRa DATA 帧 + 真机参考流 | adaptive-lora-iot | **真机 150 样本已采到** | ✔ GO | 逐通道 validity；接入 trust；节点 ID 可配 | Sensor Node (B/C) 起点 |
| 物理干预即时标注流程 | adaptive-lora-iot | 已用于 6 次真实事件 | ✔ GO | 纳入 EXP-xxx 规范 | Ground Truth 采集流程 |
| E220 传输层（CRC 帧 / 去重 / ACK / 流解析） | EventGuard-LoRa | 96 次真机验证 | ✔ REFACTOR | 剥离重要性策略；扩展 payload | Communication Layer |
| 双板运行编排 + 原始日志捕获 | EventGuard-LoRa | 已验证 | ✔ REFACTOR | 泛化到 3 板（A/B/C）多角色 | 实验运行器 |
| 数据集离线审计（SHA/计数器/回放） | EventGuard-LoRa | 96/96 通过 | ✔ REFACTOR | 适配 EdgeSense 的 manifest 字段 | 结果可信度保障 |
| 确定性应用层丢包/延迟/重复注入 | EventGuard-LoRa `faults.c` | 已验证 | ✔ REFACTOR | 加入"陈旧数据"与"节点临时离线" | Communication Fault Injection |
| 边缘算法的真实 flash 成本测量 | TinyEdgeBench | 构建期实测（基线 189,492 B） | ✔ REFACTOR | 目标换成 EdgeSense 判别器候选 | Cost Evaluation |
| 后端分层 + Pydantic 校验 + docker-compose | EdgeSense-Fusion | 软件链路可用，无 git，无真数据 | ✔ REFACTOR | LoRa 接入；PostgreSQL；schema 重设计 | Backend |
| Dashboard 骨架 | EdgeSense-Fusion | 存在（演示风格） | ✔ REFACTOR | 改为工程状态首页 | Dashboard |
| 消息协议语义（去重 / ACK 分离 / epoch） | Smart-Agriculture-Edge-AI | 仅仿真 | ◐ 概念 | 不迁代码，只取设计 | Gateway↔Backend 协议参考 |
| 断言式故障测试（CI 门禁） | EdgeFaultLab | 完成但未续维护 | ◐ 概念 | 延后到后端阶段 | （Phase 7+ 可选） |
| 土壤探头两点校准 | esp32-agri-node | 真机流程 | ◇ 引用 | 无 | 土壤通道标定 |
| **跨通道 + 跨节点证据融合** | **无** | **不存在** | — | — | **← 这是 EdgeSense 的新贡献** |
| **Gateway + 后端 + DB + 可观测性** | 无（Fusion 仅为替代品） | 不存在 | — | — | EdgeSense 新建 |
| **只读工具层的 AI 诊断 Copilot** | 无 | 不存在 | — | — | EdgeSense 新建 |

---

## 7. Gap Analysis

**已存在（无需重建）**：单通道可靠性判定、自适应采样策略、LoRa 传输层、多传感器节点固件、真机采样与事件标注流程、离线评测框架与 baseline、故障注入框架、边缘成本测量方法、后端与 Dashboard 骨架。

**真正缺失，且必须在 EdgeSense 里新建的**：

| # | 缺口 | 为什么必须新做 | 依赖 |
|---|---|---|---|
| G1 | **跨通道证据融合层** | SensorTrust 只看单通道；本机的多传感器数据里，湿度骤升同时温度不变、光照不变——这个"同时性"信息目前没有任何代码在使用 | SensorTrust core |
| G2 | **跨节点证据融合层** | 完全不存在。这是主 RQ 的核心 | **需要第 2 颗 SHT30/BH1750（阻塞）** |
| G3 | **Gateway 角色** | `adaptive-lora-iot` 的 node_g 只做格式校验与打印，没有 node 注册、心跳、超时、链路质量统计、事件聚合、主机上行 | EventGuard 传输层 |
| G4 | **持久化与会话级时间基** | 无 DB；更关键的是**没有跨节点统一时间基**（无 RTC、LoRa 上无 NTP），跨节点相关性没有时间对齐就无法计算 | — |
| G5 | **后端 + API** | 无（Fusion 是 SQLite 演示品） | — |
| G6 | **可观测性** | 无。EventGuard 有 UART_DIAG 计数器，但没有 heartbeat / API 错误 / tool call 延迟这一层 | — |
| G7 | **Dashboard（工程状态视图）** | 无（Fusion 的是演示首页） | — |
| G8 | **只读工具层 + Copilot** | 无 | 后端 |
| G9 | **EXP-xxx 实验目录规范与自动分析脚本** | SensorTrust 与 EventGuard 各有一套私有的证据管线，EdgeSense 需要一套统一的、跨项目复用的 | — |
| G10 | **"故障 vs 事件"标注真值机制** | SensorTrust 有"注入故障的独立可观测真值"，adaptive-lora-iot 有"物理干预标注"，但没有**同一数据集上同时含故障与事件、且真值来源明确区分物理/软件**的机制 | G1, G2 |
| G11 | **claim↔evidence 的 README 生成机制** | SensorTrust 有 `render_readme.py`（好），但其余项目靠手写 | — |

**明确不需要做的（避免范围蔓延）**：Mesh、多网关接管、执行器控制、Kafka/ClickHouse/K8s、Prometheus+Grafana、Wi-Fi 传感器节点、深度学习异常检测。

---

## 8. Primary Research Question

### 主 RQ（唯一）

> **在资源受限的多节点环境感知网络中，融合"节点内跨通道证据"与"跨节点（互为冗余的邻节点）证据"，能否把【单个节点的传感器故障】与【空间共享的环境事件】区分开——即降低"把传感器故障误判为空间共享环境事件"的错误率，优于仅使用单通道读数的做法，且不显著牺牲空间共享环境事件的召回？**

英文版（写研究计划书时用）：

> In a resource-constrained multi-node sensing network, can fusion of within-node cross-channel evidence and across-node (redundant neighbour) evidence distinguish a **single-node sensor fault** from a **spatially shared environmental event** — reducing the rate at which sensor faults are misclassified as environmental events — compared with single-channel readings, without materially reducing recall on spatially shared environmental events?

**术语收窄（Phase 0.1 修订的核心）**：主 RQ 里的 "genuine environmental event" **仅指 spatially shared environmental event**——即两个互为冗余的节点所处空间内**同时发生**的环境变化（整区开/关灯、同一区域湿度或温度整体变化）。

**只作用于单个节点的局部扰动**（例如只对 Node B 的 SHT30 呼气、只遮挡 Node B 的 BH1750）**不是主 RQ 的正样本**。它们与"传感器故障"在**单节点视角下不可分辨**，因此 v1 不把它们混入 shared-event 正样本；它们归入事件分类法（见 Phase 0.1 的 Revised Event Taxonomy），只作为 limitation 与 secondary stress test 使用，**并不因此把 v1 扩展成四分类复杂系统**。

**为什么是这个**：

1. **它是唯一未被占用的空白**（第 1 节表）：SensorTrust 占了"单通道故障检测"，AdaptiveSense 占了"变化感知自适应采样"，EventGuard 占了"冗余预算"。三者都不回答"这条异常到底来自传感器还是环境"。
2. **它可被证伪**：核心指标是"fault→event 误判率"，baseline 是单通道 SensorTrust。如果跨证据不降低这个率，假设被否定——但**否定结果本身仍然有价值**（你的 EventGuard 已经证明你能诚实处理否定结果）。
3. **它天然需要一个真机系统**：跨节点证据在单机仿真里没有意义。这正好把你现有硬件的价值最大化。
4. **它把"AI 诊断"放在正确的位置**：LLM 只解释已经算出的证据，而"证据"本身是确定性算法产物。

### 次级 RQ（最多 2 个，都直接服务主 RQ）

**SQ1（下游收益）**

> 把可信度信号反馈进自适应采样策略，相比"仅按变化率自适应"的策略，能否在不降低真实事件召回的前提下减少误报与通信量？

为什么需要它：主 RQ 回答"能不能分辨"，SQ1 回答"分辨出来有什么用"。如果分辨结果不改变系统行为，工程价值就不完整。同时它天然构成与 AdaptiveSense（纯变化率）的直接对比——**把 AdaptiveSense 从"重复"变成"baseline"**，这是把重叠问题转化为研究资产的正确做法。

**SQ2（边缘代价）**

> 在 ESP32-S3 上运行该判别逻辑的代价（flash / RAM / 单次判定延迟）是多少，与"只上传原始读数、在后端判定"相比，端到端检出延迟相差多少？

为什么需要它：EdgeSense 的全部合法性建立在"算法放在边缘"这一前提上。如果边缘判别的代价高到不如传后端，整个系统设计就要改。这是**必须自己测量的设计依据**，不能引用文献糊过去。

### 明确不作为 RQ 的部分

候选 RQ4（采样频率 / 通信开销 / 检出延迟 / 召回 / 数据保真度的 trade-off）**不是研究问题，而是评测框架**。它必须被测量、被制表、被画图，但不该占用一个 RQ 名额——因为它是手段而不是问题，把它升格为 RQ 只会让范围失控。

---

## 9. Hypothesis

| 编号 | 假设 | 判据 | 若被否定的后果 |
|---|---|---|---|
| **H1** | 同房间部署的两个互为冗余的节点，跨节点证据能把"单节点传感器故障"与"空间共享环境事件"区分开 | **不设预先冻结的百分比门槛。** 报告 fault→event 与 event→fault 误判率的绝对值、绝对差、相对差、置信区间与逐场景结果。方向性预期：跨节点证据（B3）相对单通道（B1）的 fault→event 误判率**更低** | 主 RQ 得到否定答案；转向"跨通道证据足够、跨节点无增益"这一同样可发表（且与 SensorTrust 不重复）的结论 |
| **H2** | 跨通道证据（同节点内 temp/hum/light 同时性）对区分"单通道自激漂移"与"空间共享环境事件"有独立贡献 | 在**无第二节点**的条件下，跨通道融合相对单通道的 fault→event 误判率**更低**（同样不设门槛，只报告上述量） | 说明多通道同节点融合没有价值，主 RQ 必须完全依赖跨节点，硬件需求升级 |
| **H3** | 把 trust 信号喂给自适应采样，能减少误报且不降低事件召回（对比纯变化率策略） | 相对 AdaptiveSense 现状：误报不升、召回不降、通信量不显著上升 | SQ1 否定；回到"纯变化率自适应已足够"的结论（这本身是 AdaptiveSense 的延伸证据） |
| **H4** | 边缘判别相对后端判别的端到端检出延迟优势显著，且代价可接受 | 边缘判别的 flash 增量与单次延迟可接受，且检出延迟明显低于 LoRa 往返 | SQ2 否定；架构需改为"边缘只做轻量筛选、重判定在后端"，这是一个**重要的设计发现**而非失败 |

**关于成功门槛（Phase 0.1 已删除）**：Phase 0 曾为 H1 冻结"fault→event 误判率相对下降 ≥ 50%"。**该门槛已删除**——它没有先验依据，属于任意数字，且会诱导在结果出来后反向挑选口径。EdgeSense 改为**报告量**而不是**比对门槛**：绝对误判率、绝对差、相对差、置信区间、逐场景结果；样本量允许时再选统计检验（且在实验设计阶段**预先声明**用哪个检验）。**主假设得到否定结果是被允许的、且必须如实报告的正常结局。**

**H4 特别说明**：LoRa 往返（UART 9600 + 空口）本身就是秒级，"后端判定"的延迟劣势非常可能真实存在。因此 H4 大概率成立，但**必须实测**，不能假设。

---

## 10. Baselines

Baseline 必须是"别人也会用的做法"，而不是"你自己做弱的版本"。以下为冻结的 baseline 阶梯（**算法侧逐级叠加，构成消融**）：

| ID | 名称 | 输入 | 说明 |
|---|---|---|---|
| **B0** | Raw + fixed threshold | 单通道原始读数 | 最朴素的"超阈值即异常"。这是所有论文都会有的对照 |
| **B1** | SensorTrust (single-channel) | 单通道样本流 | **现成**。5 类故障语义冻结，本机测试全过。这是 EdgeSense 主 RQ 的**直接对照** |
| **B2** | Cross-channel only | 同节点多通道 | H2 的判据。只加通道内同时性，不加邻节点 |
| **B3** | Cross-channel + cross-node（= **EdgeSense**） | 同节点多通道 + 邻节点同物理量 | 主 RQ 的待验方法 |
| **S0–S4** | Fixed-5s / 10s / 20s / 40s / 60s | — | **现成**（AdaptiveSense）。采样侧 baseline |
| **S5** | AdaptiveSense（纯变化率） | — | **现成**。SQ1 的直接对照 |
| **S6** | Trust-aware adaptive（= **EdgeSense sampling**） | SensorTrust/融合的 trust | SQ1 的待验方法 |

对应关系：`B0 ⊂ B1 ⊂ B2 ⊂ B3`（判别消融）；`S0–S4 ⊂ S5 ⊂ S6`（采样消融）。

**额外必须包含的对照**：**Edge detection vs Backend detection**（SQ2）。这是唯一一个"架构级"对照，且它是必需的，因为它决定系统分层是否成立。

**禁止的做法**：
- 不允许把 B0/B1 实现得比它应有的水平弱（例如给固定阈值选一个明显不利的阈值）。阈值要么取文献惯例值，要么由同样的校准流程选出，并在实验设计里写清。
- 不允许在测试集上挑选 baseline 参数。选择集与测试集必须不相交（`adaptive-lora-iot/EXPERIMENTS.md` 已经写下了这条规则，EdgeSense 直接继承）。

---

## 11. Evaluation Metrics

**判别类（对应主 RQ）**

| 指标 | 定义 | 备注 |
|---|---|---|
| **fault→event 误判率** ★ | 真值为"传感器故障"的 episode 中被判为"环境事件"的比例 | **主 RQ 的核心指标**。这是本项目的"signature metric" |
| event→fault 误判率 | 真值为"环境事件"的 episode 中被判为"传感器故障"的比例 | 与上一条必须同时报告；只报一条会掩盖 trade-off |
| Precision / Recall / F1 | 逐类（fault / event / normal） | 必须逐类给，不能只给 macro |
| 干净基线误报率 | 无故障无事件的时段里，每小时误报次数 | 必须给**时长与样本数**，不能只给百分比 |

**检测时效**

| 指标 | 定义 |
|---|---|
| 逐 episode 检出延迟 | 故障/事件起始 → 首次正确判定的时间 |
| 中位延迟 + 观测范围 | SensorTrust 的写法：中位数 + min–max，不给"平均"了事 |
| p95 延迟 | 用于与 AdaptiveSense 的表对齐 |

**代价类**

| 指标 | 定义 | 必须带的限定 |
|---|---|---|
| 采样数 / 秒 | — | — |
| LoRa 帧数 / 秒、字节 / 秒 | 物理帧与 payload 分别计 | — |
| **UART 时间代理** | `(DATA bytes + ACK bytes) × 10 / 9600` 秒 | **必须标注为 UART 时间代理，不是 E220 空口时间，不是焦耳**（继承 EventGuard 的限定） |
| 采样削减率、通信削减率 | 相对定频 baseline | 必须说明是**应用层**指标还是**无线层**指标（AdaptiveSense 已踩过这个坑） |
| flash 增量 | 编译后 ELF 段差值，含基线构建对照 | 继承 TinyEdgeBench 的方法与其"含脚手架代码"的限定 |
| 静态 RAM、单次判定延迟 | 运行时 profile | 若未测，写 `not measured`，不写估计值 |

**规则（继承已有项目的正确做法）**
1. 所有指标必须由 `experiments/analyze_experiment.py` 从原始数据自动算出，**禁止手算后填进 README**。
2. 聚合方式必须写明：micro（合并计数）还是 macro（先按场景再平均）。AdaptiveSense 明确选了 micro，EdgeSense 沿用并在方法文档里写明。
3. 每个数字必须能追溯到：EXP id + 原始日志 SHA + 固件 commit + 配置哈希。
4. **两个误判率必须连同不确定性一起报告**：绝对值、绝对差、相对差、置信区间，以及**逐场景结果**。不报单一门槛，不报"达标/未达标"。
5. 样本量允许时选择统计检验，并在实验设计阶段**预先声明**用哪个检验、α 与配对方式；样本量不足时明确写 `insufficient n for a confirmatory test`，只给描述性统计。
6. **逐场景结果必须与汇总结果同时给出**：micro 汇总会掩盖某一场景的完全失败（见 §19 R9），因此禁止只报汇总。
7. 主 RQ 的指标只统计 **`sensor_fault` vs `shared_event`** 两类；`localized_event` 单列报告，**不计入主 RQ 指标**（见 Phase 0.1 的 Revised Event Taxonomy）。

---

## 12. Proposed Architecture

### 12.1 对你候选架构的裁决

你的候选架构**方向正确**，我建议 **4 处修改**，每处都有理由：

**修改 1：网关不做传感器，但必须挂"链路质量观测"（字段已按 Phase 0.1 修订）。**
把 A 纯粹化为网关（Node→Gateway 星型 + 主机上行），避免"网关既转发又采样"带来的职责混乱与调试困难。v1 的链路质量**只记录可被现有 E220-400T22D 配置与真机日志直接证实**的量：包计数、序号缺口、重复计数、CRC 错误、到达间隔 / jitter。**RSSI 与 SNR 都不在 v1 的承诺范围内**——RSSI 仅在实测确认"当前模块配置 + 真机日志"可读之后才允许进入指标表，SNR 不承诺。任何 RF 指标在真实 E220 配置与真机日志验证之前**不得进入 README claim**。

**修改 2：B 与 C 必须"同场景冗余部署"，不是"不同场景部署"。**

这是整份审计中最关键的一个设计决定。

- 若 B 放在房间 1、C 放在房间 2，那么 B、C 的差异**主要来自环境差异**，你无法区分"B 的传感器坏了"和"房间 1 就是比房间 2 湿"。跨节点证据在这种情况下**完全失效**。
- 若 B、C **同房间相隔 30–50 cm 部署，测量同一组物理量**，它们就互为冗余见证：开窗 → 两节点湿度同升（真实事件）；B 的 SHT30 漂移 → 只有 B 变（传感器故障）。**这才是主 RQ 唯一可做的部署形态。**
- 不同场景的部署应作为一个**独立的演示/迁移实验**（对应静冈大学那条线），不作为主 RQ 的证据。

**修改 3：故障注入与实验框架前移。**

你原路线把 Fault Injection 放在 Phase 6、Experiment Framework 放在 Phase 7，都在算法之后。这是错的顺序：**没有实验框架和故障注入，你无法评估 Phase 4/5 的算法，只能靠肉眼和手写数字**——而这正是你在第 9 节明令禁止的。所以注入能力必须与节点固件同期（Phase 3），实验脚手架必须早于算法集成（Phase 4）。

**修改 4：显式声明"控制路径不存在"。**

在架构与代码里都不留任何 actuator / 下发命令的入口。不是"先不做"，而是**结构上不存在**——Copilot 的工具表里没有写操作，后端 API 只提供 `GET`，数据库只被网关写入。把"Agent 只读"从一个口头约束变成**架构约束**，这本身就是 FDE 材料里值得写进 `design_decisions.md` 的一条。

### 12.2 硬件架构

```
                 Physical Environment
        (B 与 C 同房间相隔 30–50 cm 的"同一环境")
                          │
              ┌───────────┴───────────┐
              │                       │
         Sensor Node B           Sensor Node C
         ESP32-S3                ESP32-S3
         SHT30 T/H  ──── 冗余 ────  SHT30 T/H
         BH1750 lux ──── 冗余 ────  BH1750 lux
         (soil ADC)              (soil ADC)
         │                       │
         │  SensorTrust (单通道)   │
         │  + Cross-channel fusion │
         │  + Local change detect  │
         │  + Adaptive sampling    │
         │  ── 不做跨节点融合 ──    │
         │                       │
         └──────── LoRa ─────────┘
           (E220-400T22D, star)
                   │
              Gateway A
              ESP32-S3 + E220
              node/session mgmt / beacon /
              timestamp alignment /
              cross-node fusion /
              final decision / link quality
                   │
             Serial (USB) → 主机
                   │
        ┌──────────┴──────────┐
        │   EdgeSense Backend │
        │   FastAPI + Postgres│
        │   写入通路：仅网关    │
        │   读通路：只读 API    │
        └──────────┬──────────┘
                   │
        ┌──────────┼───────────┐
        │          │           │
   Observability  Events   Analytics
     (指标表)     (事件表)  (trust/采样历史)
        │          │           │
        └──────────┼───────────┘
                   │
       Read-only Copilot (Tool Layer)
       仅 GET 工具；数值只来自工具结果
                   │
              Dashboard
        （工程状态视图，非营销首页）
```

**被故意删除的**：Mesh、多网关、任何执行器/控制路径、云侧推理。

### 12.3 软件架构与分层边界

| 层 | 运行位置 | 职责 | 禁止 |
|---|---|---|---|
| Sensing | Node B/C | 传感器读取、逐通道 validity、I²C 错误上报 | 不做判定 |
| Reliability (node-local) | Node B/C | SensorTrust 单通道 + **跨通道融合** → node-local trust（同时也是自适应采样的唯一依据） | **不做跨节点融合**——星型拓扑下节点看不到邻节点数据；不做采样决策 |
| Sampling | Node B/C | 自适应采样策略（消费本节点 trust + 变化分） | 不直接操作传感器寄存器（只通过 sensor 抽象） |
| Transport | Node B/C + A | CRC 帧、序号、去重、重传、心跳、session id | 不做业务语义 |
| Reliability (cross-node) | **Node A（Gateway）** | **跨节点证据融合** + 时间对齐 + 最终 `{normal \| sensor_fault \| environmental_event}` 判定 + 证据聚合 | 不做传感器级故障检测本身（那是节点职责）；不做采样决策 |
| Gateway (session) | Node A | 节点/会话管理、信标广播、心跳、超时、链路质量、主机上行 | 不缓存原始样本超过 N 分钟 |
| Backend | 主机 | 校验、会话级时间对齐、持久化、只读 API、可观测性 | 不提供任何写 API 给外部 |
| Copilot | 主机 | 只读工具 + 证据组装 + 解释 | 不得生成工具结果以外的数值；不得展示 CoT |
| Dashboard | 浏览器 | 状态、曲线、事件时间线、诊断 | 无写操作 |

**分层的可测试性要求（继承 AdaptiveSense 的成败关键）**：Reliability 与 Sampling 两层必须**不持有任何传感器或无线电驱动的引用**，从而能被主机编译、被单元测试、能与 Python 实现做 parity 检查。这是 AdaptiveSense 已经证明有效的做法，EdgeSense 必须沿用——否则算法只能在真机上"看日志调"。

### 12.4 数据流（一次采样闭环）

```
【节点侧（B/C）—— 闭环在设备上完成，不依赖邻节点】
1. Sensor read             → 每通道 value + valid + node_ts_ms（本地单调时钟）
2. Per-channel trust       → SensorTrust core（现成）→ flags + health_score + state
3. Cross-channel fusion     → 同节点同时性证据（新）→ 通道间一致性分
4. Local change detection   → 本节点变化分（AdaptiveSense change_detector，现成）
5. Node-local decision      → node-local trust + 变化分 → {node_trust, change_score}
6. Sampling policy          → 下一采样间隔 + 是否上传（不依赖邻节点）
7. Transport                → CRC 帧 + seq + node_id + session_id + node_ts_ms
                              + payload(读值, 逐通道 valid, node_trust, change_score)

【网关侧（A）—— 跨节点融合在这里，不在节点上】
8. Receive                  → 校验 / 去重 / 序号连续性 / session 管理 / 链路质量
9. Timestamp alignment      → node_ts_ms → gateway timeline
                              （v1：beacon + offset + tolerance；不做漂移拟合）
10. Cross-node fusion       → 互为冗余节点的同物理量对比 → 节点间一致性分
11. Final decision          → {normal | sensor_fault | environmental_event}
                              + confidence + reason_codes
                              （对齐不可用 → insufficient_alignment，拒绝跨节点融合，
                                不勉强给出判定）
12. Evidence aggregation    → 节点侧 (node_trust, change_score)
                              + 网关侧 (cross_node_consistency, link_quality)
                              → 一条可被 Copilot 引用的完整证据

【主机侧】
13. Serial uplink → Backend → 校验 → 持久化（**不做重新判定**）
14. Observability           → 指标表（延迟、错误、工具调用）
15. API → Dashboard / Copilot（均为只读）
16. Copilot → 工具调用 → 证据 → 诊断（所有数值来自工具结果，带 node/timestamp/metric 引用）
```

### 12.5 通信流与失败流

**通信流（星型）**

```
Node B ──DATA(seq,node_id,session_id,node_ts_ms,payload,node_trust)──▶ Gateway A ──▶ 主机
Node C ──DATA(...)────────────────────────────────────────────────▶
Gateway A ──BEACON(epoch, session_id)（低速率，仅用于时间对齐）────▶ Nodes
```
**v1 不存在 Gateway → Node 的邻节点数据广播**（见 §13 的理由）。

**失败流（必须在固件里真实实现，不能只写 Happy Path）**

| 失败 | 检测者 | 行为 | 可观测证据 |
|---|---|---|---|
| I²C 超时 / 传感器不应答 | Node | 标记通道 invalid，supervisor 重探测；连续 N 次失败 → 该通道进入 unavailable | `device_events` |
| 传感器断电/拔出（物理） | Node | 同上，但**标注为 physical** | `device_events` + 实验日志 |
| 读取值越界/卡死/漂移 | SensorTrust | fault flags + health_score 下降 | `trust_scores` |
| 帧损坏 / CRC 错 | Gateway | 丢弃 + 计数 | `network_metrics` |
| 序号缺口 | Gateway | 记录 lost range，不推断内容 | `network_metrics` |
| 重复帧 | Gateway | 按 (node_id, seq) 去重 | `network_metrics` |
| 节点静默 | Gateway | 心跳超时 → node offline 事件；恢复时判定 SENDER_RESTART | `device_events` |
| 网关重启 | 主机 | 主机侧识别会话断裂，时间基重建 | `backend` 事件 |
| 后端/DB 不可用 | 主机 | 网关侧（或主机采集侧）本地排队，恢复后按序回放 | `backend` 错误日志 |
| 后端 API 错误 / AI 超时 | Backend | 结构化错误 + 指标；Copilot 明确报"证据不足"而非编造 | `observability` |
| **时间基漂移** | Gateway | 节点上报本地单调时钟 + 与信标偏移，后端做对齐 | 需要专门设计（见 Risk R4） |

---

## 13. Hardware Roles

**推荐分配（3 × ESP32-S3 + 3 × E220）**

| 角色 | 硬件 | 传感器 | 职责 |
|---|---|---|---|
| **Gateway A** | ESP32-S3 + E220 | 无（可选：OLED 作状态显示） | LoRa 汇聚、节点/会话管理、信标广播、**时间对齐**、**跨节点证据融合**、最终 `{normal \| sensor_fault \| environmental_event}` 判定、证据聚合、链路质量、USB 串口上行 |
| **Sensor Node B** | ESP32-S3 + E220 | SHT30 + BH1750 + 土壤 ADC | 感知 → 逐通道 validity → SensorTrust → **跨通道融合** → 本地变化检测 → 自适应采样 → LoRa 发送（**不做跨节点融合**） |
| **Sensor Node C** | ESP32-S3 + E220 | SHT30 + BH1750 +（土壤 ADC 可选） | 同 B（**同一固件镜像，节点 ID 来自配置**） |

**为什么跨节点融合放在 Gateway A，而不是节点上（Phase 0.1 修正，这是一处架构矛盾）**

B/C 是**星型拓扑**，只向 A 发送、不接收邻节点数据，因此节点在物理上无法获得另一节点的实时读数。把跨节点融合画在节点上（Phase 0 §12.2 曾如此）是**架构矛盾**，已修正。

v1 **不引入 Gateway→Node 的邻节点数据广播**，理由有三：
1. 它需要一条反向链路与额外的空口预算，而空口是 EdgeSense 的稀缺资源；
2. 它让节点的**采样行为依赖邻节点可用性**——邻节点掉线会牵连本节点的采样策略，把一个"数据可信度"问题扩散成"系统可用性"问题；
3. 节点侧的采样闭环只依赖本节点信息，才能保证**节点独立可运行**（单节点掉网时仍能自主工作）。

**只有在实验数据证明节点侧融合有不可替代的必要性时**（例如 SQ2 实测出网关侧判定的端到端检出延迟无法满足要求），才重新评估这一决定——那时必须在 `design_decisions.md` 里写清触发条件与实测依据。

**为什么 Gateway 不挂传感器**：一是职责纯化，二是把"第三见证点"的位置让给 B/C 的冗余关系更干净。若日后需要第三个见证点，可让 A 在空闲时也挂一颗 SHT30——但那会破坏"A 是网关"的清晰性，不建议在 v1 引入。

**节点 ID 与镜像**：B 与 C 必须烧**同一份固件**，差异只在配置（`node_id`、可选引脚）。否则两个节点会随开发逐渐分叉，而跨节点实验要求两节点行为对称——分叉的固件会让"差异来自传感器还是来自固件版本"无法区分。这一点必须写进 `design_decisions.md`。

**⚠️ 阻塞项：跨节点证据需要第二套同型传感器。**

当前你只有 1 颗 SHT30 与 1 颗 BH1750。B 与 C 要互为冗余见证，**必须各有一组**。三个选项：

| 选项 | 内容 | 代价 | 后果 |
|---|---|---|---|
| **(a) 加购（推荐）** | 再买 1 颗 SHT30（0x44）+ 1 颗 GY-302/BH1750（0x23） | 约 ¥30–60 | 主 RQ 完整可做；这是整份路线图性价比最高的一笔支出 |
| **(b) 不加购，降级主 RQ** | 只做**跨通道**证据（同节点内 temp/hum/light 同时性），跨节点部分推迟 | ¥0 | 主 RQ 缩到 H2；研究新意下降明显（跨通道在同节点上是较弱的判据），但**仍然不是 SensorTrust 的重复** |
| **(c) 替代冗余** | 用 B 的 SHT30 与 C 的土壤/光照做"异构跨节点对比" | ¥0 | **不成立**：不同物理量的跨节点对比无法区分故障与事件 |

**建议：选 (a)。** 并在 Phase 1 之前完成加购，否则 Phase 5 会整体阻塞。若短期无法采购，则先用 (b) 推进 Phase 1–4，把主 RQ 标注为"跨通道版"，并在 limitations 里明确写出跨节点部分待硬件到位后补做——**不要在没有第二套传感器的情况下声称验证了跨节点证据**。

---

## 14. Software Modules

| 模块 | 层 | 来源 | 新建 / 复用 | 可独立测试 |
|---|---|---|---|---|
| `firmware/common/` 传感器抽象（`sensor.c/h`、逐通道 validity） | Sensing | AdaptiveSense（`sensor.c`/`sensor.h`/`sensor_bus.c`） | 复用 + 小改 | ✔ 主机编译 |
| `firmware/common/sht30_proto.c` / `bh1750_proto.c` / `soil_moisture_math.c` | Sensing | AdaptiveSense | **GO 直接复用** | ✔ 已有单测 |
| `firmware/common/sensor_supervisor.c` | Sensing | AdaptiveSense | 复用 + 扩展到多通道 | ✔ |
| `firmware/common/fault_injector.c` | Fault Injection | SensorTrust | 复用 + 扩展到多通道/跨节点 | ✔ 本机已验证 |
| `firmware/common/e220.c` / `e220_stream_parser.c` | Transport | EventGuard-LoRa | REFACTOR（剥离重要性策略） | ✔ |
| `firmware/common/protocol.c` | Transport | EventGuard-LoRa | REFACTOR（扩展 payload：多通道 + trust + 事件） | ✔ + C/Python parity |
| `firmware/common/faults.c` | Comm Fault Injection | EventGuard-LoRa | REFACTOR（加陈旧数据、节点临时离线） | ✔ |
| `edge/sensor_trust/` （vendored `sensor_trust.c` + 薄封装） | Reliability | SensorTrust | **GO（pin SHA）** | ✔ 已有 21 个 C 测试 |
| `edge/fusion_channel/`（**跨通道**，节点侧） | Reliability (node-local) | **新建** | 新 | ✔ 主机可编译 + 与 Python parity |
| `edge/fusion_node/`（**跨节点**，网关侧） | Reliability (cross-node) | **新建** | 新 | ✔ 主机可编译 + 与 Python parity（网关侧逻辑同样必须能脱机测） |
| `edge/adaptive_sampling/` | Sampling | AdaptiveSense（`change_detector.c`/`adaptive_scheduler.c`） | REFACTOR（消费 trust） | ✔ 已有 parity 测试 |
| `edge/anomaly_detection/` | Reliability | 新建（规则/统计为主，必要时 LR） | 新 | ✔ |
| `firmware/sensor_node/` | — | adaptive-lora-iot `node_s` | REFACTOR（同镜像、可配 ID） | 构建 + 主机侧逻辑测试 |
| `firmware/gateway/` | — | adaptive-lora-iot `node_g` + EventGuard gateway | REFACTOR（注册/心跳/超时/链路质量/聚合） | 同上 |
| `backend/` | Backend | EdgeSense-Fusion（分层 + schema + compose） | REFACTOR 到 FastAPI + PostgreSQL | ✔ pytest（改真实数据） |
| `dashboard/` | UI | EdgeSense-Fusion（骨架） | REFACTOR（工程状态首页） | 手工 + 端到端 |
| `copilot/` | AI | 新建 | 新 | ✔ 工具契约测试 + 工具调用落库 |
| `experiments/` | 实验 | EventGuard（编排/审计）+ SensorTrust（证据管线）+ adaptive-lora-iot（采集/标注） | REFACTOR 合并 | ✔ 分析脚本可对固件 fixture 跑 |
| `scripts/` | 治理 | AdaptiveSense（config parity）+ 新建（vendored 哈希校验） | REFACTOR | ✔ |

**每个模块的硬性要求（沿用你第 24–25 节，与已有项目一致的部分我直接确认可行）**：C 侧 `-Wall -Wextra -Werror` 零警告（SensorTrust 与 AdaptiveSense 已做到，EdgeSense 不得放松）；Python 侧 type hints；模块边界清晰；结构化日志；每个模块有单测；能独立测试。`hardware validation required` 与 software simulation 必须分开记录。

---

## 15. Data Flow

### 15.1 时间同步（Phase 0.1 已简化：先测量，再决定是否需要漂移拟合）

跨节点证据的唯一前提是**时间可比**。约束：节点无 RTC、LoRa 上无 NTP、节点可能各自重启。

**v1 只做四件事，不做拟合**：

1. **本地单调时钟**：节点用 `esp_timer` 维护单调时间，每个样本带 `node_ts_ms`（绝不用 RTC 墙上时间）。
2. **Gateway beacon / epoch**：网关周期性（例如每 60 s）广播 `BEACON(gateway_epoch, session_id)`。
3. **session id**：每次网关启动生成新 session；节点记录它加入的 session。**跨 session 的样本不参与同一窗口的跨节点对比**（否则会把重启前后的时间混在一起）。
4. **sample / window id**：节点在 frame 里带自增 `seq`；网关按 `(session_id, node_id, window_index)` 把两个节点的样本配对。**配对以窗口索引为主、时间戳为辅**——这样即使存在固定 offset，只要 offset 稳定小于窗口长度，配对依然正确。窗口长度（例如 30 s 或 60 s）必须显著大于预期的 offset 与 jitter。
5. **timestamp tolerance**：为每对节点定义一个容忍阈值。对齐残差超过 `tolerance` 的窗口标记为 `insufficient_alignment`，**不参与跨节点融合**，而不是用错误对齐去融合。

**并且必须实测以下三项，作为"是否需要漂移拟合"的判据**（写入 EXP，不能靠猜）：

| 测量项 | 含义 | 观测手段 |
|---|---|---|
| **offset** | 节点本地时钟与网关时间基之间的**固定偏差** | beacon 交换时的差值，多次取中位数 |
| **jitter** | 单次 offset 估计的离散程度 | 差值序列的分位数范围 |
| **drift** | offset 随时间的变化率（ppm） | 长时（≥ 数小时）offset 序列的回归斜率 |

**升级条件（engineering decision，有数据依据）**：只有当实测表明"offset + jitter 在目标窗口长度内**不足以**支持跨节点配对"（例如 drift 使 offset 在一小时内漂移超过 tolerance）时，才引入**线性时钟漂移拟合**（对每个节点做 `gateway_time ≈ a·local_time + b` 的拟合，并上报残差）。**在拿到数据之前，不把拟合写成架构要求。**

这个设计的价值：它把"时间对齐"从一个被忽略的隐含假设，变成一个有**明确测量项、明确升级条件**的工程判断。这正是 SensorTrust 与 EventGuard 的处理风格。

### 15.2 数据模型（按架构必要性筛选，不机械建表）

**必须有（v1）**

| 表 | 关键字段 | 为什么需要 |
|---|---|---|
| `nodes` | node_id, role, fw_commit, first_seen, last_seen, status | 节点注册与在线状态；诊断回答必须能引用 node |
| `sensor_readings` | node_id, channel, value, valid, node_ts_ms, gateway_ts | 唯一的高频表；必须保留 `valid`（逐通道） |
| `trust_scores` | node_id, channel, health_score, state, fault_flags, reason_codes, ts | 主 RQ 的主证据表；Copilot 的 `query_trust_history` 直接读它 |
| `anomaly_events` | node_id, kind(`sensor_fault` / `shared_event` / `localized_event`), decision_source(node/gateway), evidence_json, confidence, ts | 融合判别结果。**`sensor_fault` 与 `shared_event` 必须是同一张表的两种取值**，否则算不出混淆矩阵；`localized_event` 单列但**不计入主 RQ 指标** |
| `sampling_changes` | node_id, prev_interval, new_interval, trigger, ts | 时间线要显示 "20m → 1m"，也用于算采样削减 |
| `network_metrics` | node_id, session_id, seq, seq_gap_count, duplicates, crc_errors, inter_arrival_ms, jitter_ms, rssi**（仅当实测确认可读，否则 NULL）**, ts | "投递正常"这条证据的来源。**v1 无 SNR 字段** |
| `session_alignment` | session_id, node_id, offset_ms, jitter_ms, drift_ppm, residual_max_ms, quality, ts | 时间对齐的**可测量质量指标**；为"是否需要漂移拟合"提供数据依据（§15.1） |
| `device_events` | node_id, event_type, detail, physical_or_injected, ts | I²C 超时、断电拔出、重启、offline；**必须带 physical/injected 标注** |
| `experiments` | exp_id, date, fw_commit, config_sha, fault_type, injection_method, duration, expected_event | 可复现性的锚点 |

**AI 相关（v1 必须有，但只两张）**

| 表 | 关键字段 | 为什么 |
|---|---|---|
| `agent_queries` | query_id, question, ts, latency_ms, status | 可观测性 + 审计 |
| `tool_calls` | query_id, tool_name, args_json, result_ref, latency_ms, ok | **"Agent 不生成无法追溯的数据"的强制机制**：每个回答里的数值都必须能指回一条 tool_call |

**明确推迟（v1 不建）**：`sampling_policies` 版本表、`firmware_releases`、多租户/用户表、告警订阅表、任何写操作审计表（因为没有写操作）。

**存储选型裁决**：PostgreSQL。理由不是"更高级"，而是 EdgeSense 的事件模型里 `evidence_json` / `reason_codes` 是结构可变字段，JSONB + 单容器 docker-compose 比 SQLite 的 JSON 处理更顺手，且它避免了日后迁移的时间成本。同时明确：**时序数据量在第一版远未到需要 TimescaleDB / ClickHouse 的程度**，这个判断要写进 `design_decisions.md`。

---

## 16. Integration Strategy

### 16.1 核心原则：**"研究资产"与"运行组件"分离**

你的三个仓库是**研究资产**（各自有冻结的语义、证据与结论）。EdgeSense 是**运行系统**。二者的整合方式必须是：**研究资产保持独立且可引用，运行组件以固定版本引入。**

理由（逐条对应你要求的四个分析维度）：

| 维度 | 复制进 monorepo | 改为 library/component | **保持独立 repo + 固定版本引入（推荐）** |
|---|---|---|---|
| 依赖复杂度 | 最简 | 中（需打包/发布） | 中（需 vendor + 校验） |
| 未来可维护性 | 最差（改动要同步两处） | 好 | 好（上游可独立演进） |
| Git history | **丢失**（最严重） | 保留 | 保留 |
| 研究清晰性 | **最差**：无法区分"这是 SensorTrust 的既有结论"还是"EdgeSense 的新结论" | 好 | **最好**：vendor 的 SHA 就是基线出处 |
| 作品集呈现 | 单一巨大仓库，看不出分工 | 清楚 | **最清楚**：三个方法类仓库 + 一个系统级仓库，正好对应"研究能力 + 系统能力"两条叙事 |

**结论：采用方案 C —— 保持独立 repo，EdgeSense 以固定 commit 引入，并用 `docs/vendored/*.md` 记录来源 SHA 与理由。**

具体的 vendor 机制建议：用 **git subtree**（而非 submodule）。submodule 在"clone 后忘记 `--init`"时会静默缺失文件，对需要长期复现的实验项目是隐患；subtree 把代码物理复制进来，配合 `scripts/check_vendored_hashes.py`（对比记录的 SHA 与实际文件哈希）就能在 CI 里发现"有人偷偷改了 vendor 的代码"。

### 16.2 三个项目分别怎么进来

**SensorTrust → `edge/sensor_trust/`（vendored 核心 + EdgeSense 侧薄封装）**

- **进来什么**：`core/sensor_trust.c` + `.h`（固定 commit）；`firmware/main/fault_injector.c`；`hardware/` 证据管线的**方法论**。
- **不进来什么**：`firmware/main/experiment.c`（那是它的 v0.2 单通道实验，EdgeSense 有自己的 EXP 体系）。
- **关键裁决：融合层必须写在 EdgeSense 侧（`edge/fusion_channel/` 与 `edge/fusion_node/`），绝不能改进 SensorTrust 内部。**
  理由有三：① SensorTrust 的 v0.1 语义是**已冻结**的，改它等于毁掉它的可引用性；② 把它当**未来论文里的 prior work / baseline**，比当成自己的一部分更有价值；③ 一旦融合逻辑进了 SensorTrust，你就再也无法干净地回答"B1（单通道）vs B3（融合）"的差别到底来自哪里。
- **接口**：`sensor_trust_evaluate(sample) → {health_score, state, fault_flags}`（沿用现有契约，不改），上层 `fusion` 消费多个通道的这份输出。

**AdaptiveSense → `edge/adaptive_sampling/`（vendored 策略 + 消费 trust 的新策略）**

- **进来什么**：`change_detector.c` + `adaptive_scheduler.c`（固定 commit）；`simulator/` 作为离线回放框架的基础；parity 测试范式。
- **不进来什么**：`communication.c`（Wi-Fi/MQTT）、`power_mgmt.c`（v1 不做功耗声明；若日后要能耗指标再引，并且只能作为"UART 时间代理"而非焦耳）。
- **关键裁决：EdgeSense 增加一条"trust-aware"策略，与 AdaptiveSense 的原策略**并存**，二者都保留。** 前者是 S6（待验方法），后者是 S5（baseline）。**不要修改 AdaptiveSense 的策略本体去加 trust**——那样 S5 与 S6 就分不开了，SQ1 也就无法回答。
- **重叠处理**：`adaptive-lora-iot/analysis/` 与 AdaptiveSense 的 `simulator/` 功能重叠（技术债 #6）。裁决：以 AdaptiveSense 的 `simulator/` 为准（它更完整、有 parity 测试、有 CI），把 `adaptive-lora-iot/analysis/` 中**独有**的部分（真实参考流加载、物理事件标注的对齐逻辑）迁入，然后删除重复实现并留迁移说明。

**把 AdaptiveSense 从"重复"变成"baseline"，这是本轮审计最重要的一个策略转换。**

**adaptive-lora-iot → EdgeSense 的系统基座（迁入，不再是独立项目）**

- **进来什么**：`firmware/node_s`（多传感器节点 + `adaptive_policy.c` 骨架）、`firmware/node_g`、`experiments/{collect,add_event}.py`、`docs/wiring.md`、`REPRODUCIBILITY.md` 的规则、以及**那 150 个真实样本 + 6 条事件标注作为 EdgeSense 的第一个真实数据集**。
- **修什么**：逐通道 validity（技术债 #5）；节点 ID 可配置；接入 SensorTrust 与 fusion。
- **裁决**：它**不再作为独立项目继续发展**，其 LaTeX 论文骨架（`paper/`）暂时保留但不推进（Phase 0 明确"先不做论文"）。这条要跟你确认——见第 23 节。

**EventGuard-LoRa → 只取传输层与实验编排，不取 RQ**

- **进来什么**：`firmware/common/{e220,e220_stream_parser,protocol,faults}.c`、`tools/run_hardware_validation.py` 系列的编排与审计设计、`results/final_hardware_v1/audit_report.md` 的审计范式。
- **不进来什么**：`importance.c`、`strategy.c`、它的试验矩阵与结论。
- **必须做的区隔声明**（写进 README 与 `design_decisions.md`）：

  > EventGuard-LoRa 的 importance 回答"哪些样本值得多花副本"；EdgeSense 的 trust 回答"这条读数是否可信"。前者是**投递优化**，后者是**数据可信度**。EdgeSense 复用其传输与实验编排，**不复用其研究问题**。

**TinyEdgeBench → 保持独立 repo，作为 companion 而非运行时**

- **裁决：不进 EdgeSense 仓库，不进固件。**
- **怎么用**：当你要回答"EdgeSense 的判别器该用规则还是逻辑回归"时，用它的 `measure_flash.py` 方法学，在 EdgeSense 的 `experiments/` 里做一次针对**EdgeSense 自己的候选判别器**的测量。
- **依据**：它自己的结果显示 LR（0.8283）与 MLP（0.8250）在精度上落在噪声内，而 LR 更便宜——这是"v1 用规则/统计判别器、只有证据不足时才上 LR"的一个**可引用的量化理由**。这条理由比"轻量算法更适合嵌入式"这种空话强得多。

**lora-p2p → 归档（Archive）**

- **不继续维护。** 它的传输代码被 EventGuard 取代，它的实验结论被保留为**"P2P 链路可行性"的既有证据**（本机已复核 602/602）。
- 在归档 README 顶部加一行：`Superseded by EventGuard-LoRa transport; retained as the P2P feasibility evidence.` 并在 EdgeSense 的 `docs/engineering/protocol.md` 里引用它，避免把"0% 丢包"重述为可靠性结论。

**EdgeSense-Fusion → 归档，后端与 Dashboard 骨架迁入**

- ① 修正 README 的"物理数据"措辞；② `git init` + `.gitignore`（排除 `*.db*`、`*.joblib`）；③ 目录改名加 `-ARCHIVED`；④ 其 `backend/app/` 分层与 `dashboard/` 骨架以 **attribution 方式**迁入 EdgeSense 并重构。
- **不做**：不把它的 Isolation Forest、DHT22/MPU6050 固件路线带进来。

**Smart-Agriculture-Edge-AI → 保持独立，不并入**

- 取它的消息协议词汇与故障场景写法作为**设计参考**（在 `docs/engineering/protocol.md` 里注明来源）；不迁任何运行代码。

**EdgeFaultLab → 保持独立，暂不接入**

- 它的层次（TCP 语义代理）对应 EdgeSense 的**网关↔后端**段。等 Phase 7 后端成形后再评估是否用它做后端侧韧性测试。现在不引入。

**esp32-agri-node → 保持独立（另一条研究线）**

- 只引用 `docs/wiring.md` 与 `host/calibrate_soil.py`。

### 16.3 一句话总结整合策略

> **SensorTrust 与 AdaptiveSense 作为"被引用的方法与 baseline"以固定 commit vendor 进来；adaptive-lora-iot 作为"系统基座"整体迁入并重构；EventGuard-LoRa 只贡献传输层与实验编排；TinyEdgeBench 留在外部作为成本测量工具；其余全部归档或不参与。新贡献集中在 `edge/fusion_channel/`（节点侧跨通道）、`edge/fusion_node/`（网关侧跨节点）、`backend/`、`copilot/` 四处，且必须是全新的。**

---

## 17. Final Repository Structure

基于审计结果设计（**本轮不创建任何代码或目录**）：

```
EdgeSense/
├── README.md                      # 只写 claim ↔ evidence；未测的一律写 Evaluation planned
├── LICENSE
├── .gitignore                     # 排除 *.db*、*.joblib、build/、.venv/、raw 大文件
│
├── firmware/
│   ├── common/                    # 两角色共享
│   │   ├── sensor/                # 传感器抽象 + 逐通道 validity（源自 AdaptiveSense）
│   │   ├── drivers/               # sht30_proto / bh1750_proto / soil_moisture_math
│   │   ├── supervisor/            # 可用性状态机（源自 AdaptiveSense sensor_supervisor）
│   │   ├── fault_injector/        # 源自 SensorTrust，扩展到多通道
│   │   ├── transport/             # e220 + stream parser + protocol（源自 EventGuard，剥离策略）
│   │   ├── comm_faults/           # 丢包/延迟/重复/陈旧/离线（源自 EventGuard faults.c）
│   │   └── vendor/                # 上游代码原样存放 + VENDORED.md（SHA）
│   ├── sensor_node/               # B 与 C 同一镜像，node_id 来自配置
│   ├── gateway/                   # A：会话管理/信标/时间对齐/跨节点融合/链路质量/聚合
│   └── CMakeLists.txt
│
├── edge/                          # 主机侧算法镜像 —— 必须与固件保持 parity
│   ├── sensor_trust/              # vendored SensorTrust core（pin SHA）+ 薄封装
│   ├── fusion_channel/            # ★ 新贡献：跨通道融合（节点侧，与 firmware/sensor_node 同源）
│   ├── fusion_node/               # ★ 新贡献：跨节点融合（网关侧，与 firmware/gateway 同源）
│   ├── adaptive_sampling/         # vendored AdaptiveSense 策略（baseline S5）
│   │   └── trust_aware/           # ★ 新贡献：消费 trust 的策略（S6）
│   └── anomaly_detection/         # 规则/统计判别器
│
├── backend/
│   ├── app/{api,database,schemas,services,ingest,observability}/
│   └── tests/
│
├── dashboard/                     # 静态；首页 = 系统状态，不是营销页
│
├── copilot/                       # 只读工具层
│   ├── tools/                     # get_node_status / query_* 全部只读
│   ├── contracts/                 # 工具 JSON Schema
│   └── tests/                     # 契约测试：禁止非 GET；数值必须可追溯到 tool_call
│
├── experiments/
│   ├── EXP-000-template/          # manifest.json + raw/ + results/ 的空白模板
│   ├── EXP-001-.../               # 每个实验一个目录，含 manifest + 原始日志 + 结果
│   ├── analyze_experiment.py      # 唯一的指标计算入口
│   └── README.md                  # EXP 编号规则 + physical/injected 标注规则
│
├── datasets/
│   ├── real/                      # 只放真机采集（含 adaptive-lora-iot 迁入的 150 样本）
│   ├── injected/                  # 真机采集 + 软件注入（必须与 real 分开）
│   ├── synthetic/                 # 仅用于离线框架自检，禁止与 real 混合评测
│   └── README.md                  # 每个数据集的来源、SHA、限制
│
├── tests/                         # 跨模块集成测试
│
├── scripts/
│   ├── check_config_parity.py     # YAML ↔ C 头（源自 AdaptiveSense）
│   ├── check_vendored_hashes.py   # vendor 目录不得被静默修改
│   ├── preflight.py               # 环境自检（含 venv/依赖/串口/IDF）
│   └── render_readme_claims.py    # 从实验结果渲染 README 数字（源自 SensorTrust）
│
├── docs/
│   ├── research/
│   │   ├── research_question.md
│   │   ├── methodology.md
│   │   ├── experiment_design.md
│   │   ├── results.md
│   │   └── limitations.md
│   ├── engineering/
│   │   ├── architecture.md
│   │   ├── data_flow.md
│   │   ├── protocol.md            # 引用 lora-p2p / EventGuard 的既有帧与实验证据
│   │   ├── deployment.md
│   │   ├── troubleshooting.md
│   │   ├── failure_modes.md
│   │   └── design_decisions.md    # ★ 工程判断的记录地
│   ├── hardware/
│   │   ├── wiring.md              # 三项目一致的引脚，引用 esp32-agri-node 校准流程
│   │   └── calibration.md
│   └── vendored/                  # 每个上游一份：来源、SHA、为何 vendor、边界
│       ├── SensorTrust.md
│       ├── AdaptiveSense.md
│       ├── EventGuard-LoRa.md
│       └── adaptive-lora-iot.md
│
├── docker/
│   └── docker-compose.yml         # backend + postgres（仅两个服务）
│
└── .workbuddy/memory/
```

**与你的候选结构的差异及理由**：

| 差异 | 理由 |
|---|---|
| 新增 `firmware/common/vendor/` 与 `docs/vendored/` | 没有 provenance，半年后无法回答"这段代码是哪个版本、为什么在这" |
| 新增 `edge/` 的明确定位（与固件 parity 的算法镜像） | 这是 SensorTrust / AdaptiveSense 已经验证成功的关键做法；不写明会被误当成"重复实现" |
| `datasets/` 分 `real` / `injected` / `synthetic` 三档 | 你的第 9、24 节要求严格区分物理故障与软件注入；数据层就要分开，不能只在文档里区分 |
| 新增 `scripts/check_vendored_hashes.py` 与 `preflight.py` | 分别治理技术债 #4（环境不可复现）与 vendor 静默漂移 |
| 新增 `docs/hardware/` | 三个项目引脚已一致但分散在各自 README；EdgeSense 需要单一权威来源 |
| 新增 `docs/engineering/design_decisions.md` 的位置提升为 v1 必写 | 这是 FDE 材料中权重最高的一份文档 |
| `copilot/contracts/` 独立于 `tools/` | "Agent 只读"要靠契约测试强制，不能只靠约定 |
| **不放** `paper/` | Phase 0 明确"先不做论文"；且 `adaptive-lora-iot` 的 LaTeX 骨架应留在原处 |
| **不放** `firmware/fault_injection/` 作为独立顶层 | 它属于 common（要被两个角色共享） |

---

## 18. Development Roadmap

**相对你原路线的两处重排**：
1. **Fault Injection 从 Phase 6 前移为 Phase 3 的一部分**（没有注入能力就无法评估任何算法）；
2. **Experiment Framework 从 Phase 7 前移为 Phase 4**（同上）。
3. 新增 **Phase 1 的"逐通道 validity 修复"与"节点同镜像"要求**——这两条是跨节点实验的**前提条件**，不是可选项。

---

### Phase 0 — Audit（已完成）
- **Goal**：在写任何代码之前，确定已有资产、空白与主 RQ。
- **Deliverables**：本文件。
- **Exit Criteria**：`docs/PHASE_0_DESIGN_REVIEW.md` 完成；主 RQ 冻结；GO/REFACTOR/DROP 表通过你的审阅；阻塞项（第二套传感器）已决断。

---

### Phase 1 — Sensor Nodes（B / C 同镜像可稳定运行）
- **Goal**：两个节点以同一固件镜像稳定运行，产出**逐通道**有效的传感器帧。
- **Deliverables**：`firmware/sensor_node/`；`firmware/common/sensor/`（逐通道 validity）；`firmware/common/supervisor/`；`docs/hardware/wiring.md`。
- **Tests**：传感器抽象与驱动的主机侧单测（沿用 AdaptiveSense 已有单测）；上电/重启/传感器拔插的手工验证记录。
- **Exit Criteria**：
  - Node B 与 Node C 各**连续运行 30 分钟无崩溃**，无看门狗复位，产出有效传感器帧。
  - 拔掉 SHT30 后，**仅** `temperature`/`humidity` 两通道变为 invalid，`light` 仍有效（逐通道 validity 生效）。
  - 土壤探头无介质时 `soil` 通道标记为 invalid 或明确标注未标定——**不得出现 `soil_raw=4095` 且 `valid=1`**（修复技术债 #5）。
  - `scripts/check_config_parity.py` 在 CI 中通过。

---

### Phase 2 — LoRa Transport + Gateway
- **Goal**：Node→Gateway 星型链路稳定，网关具备注册/心跳/超时/链路质量。
- **Deliverables**：`firmware/common/transport/`；`firmware/gateway/`；`docs/engineering/protocol.md`。
- **Tests**：帧协议 C/Python parity；流式解析器对**畸形帧**的健壮性测试（截断/超长/CRC 错/UART 噪声）；序号缺口与重复的判定测试。
- **Exit Criteria**：
  - B→A 与 C→A **同时**连续运行 30 分钟，两路序号各自连续，丢包数与重复数被计数（不推断）。
  - 注入 10 个畸形帧后网关**不崩溃**且计数正确。
  - 网关能识别"节点静默 > 阈值"并产生 `node_offline` 事件；节点恢复后能识别重启并重置序号预期。
  - 链路质量指标（RSSI 或退化代理）落盘。
  - **禁止**把本次结果表述为"可靠性高"；只能写"在本次摆放与环境下的观测结果"。

---

### Phase 3 — Fault Injection Framework（前移）
- **Goal**：具备**可复现、可标注、并明确区分物理与注入**的故障注入能力。
- **Deliverables**：`firmware/common/fault_injector/`（多通道版）；`firmware/common/comm_faults/`；`docs/research/experiment_design.md` 的故障分类表。
- **Tests**：注入器的单测（本机可跑，SensorTrust 已有 7 模式测试可扩展）；每个故障类型的"独立可观测真值"生成逻辑测试。
- **Exit Criteria**：
  - 传感器侧 6 类故障（spike / drift / stuck-at / missing / bias / noise-increase）与通信侧 5 类故障（丢包 / 延迟 / 重复 / 陈旧 / 临时离线）**逐一在真机上演示成功**。
  - 每条 `device_events` 记录都带 `physical_or_injected` 标注，且**没有任何一条缺失标注**。
  - 真值来源与检测器输出**无耦合**（沿用 SensorTrust 的原则：生成真值时不查检测器输出）。

---

### Phase 4 — Experiment Framework（前移）+ SensorTrust 集成
- **Goal**：先把"能测量"这件事做扎实，再谈算法。
- **Deliverables**：`experiments/EXP-000-template/`；`experiments/analyze_experiment.py`；`scripts/render_readme_claims.py`；`edge/sensor_trust/`（vendored）；`docs/vendored/SensorTrust.md`。
- **Tests**：分析脚本对**已知答案的 fixture** 给出正确指标（这是防"手算填数"的关键）；vendor 哈希校验。
- **Exit Criteria**：
  - 跑通 **EXP-001**（真机干净基线，≥30 分钟）与 **EXP-002**（单通道故障注入，复用 SensorTrust 的 5 类）。
  - `analyze_experiment.py` 自动输出 precision / recall / F1 / 逐 episode 延迟 / 丢包 / 采样削减 / 通信削减，**无任何手工填入的数字**。
  - README 中所有性能数字均来自 `render_readme_claims.py` 的输出（CI 可校验）。
  - 干净基线的误报率报告**必须带时长与样本数**。

---

### Phase 5 — Cross-Channel Fusion（`edge/fusion_channel/` v1，**节点侧**）
- **Goal**：同节点多通道证据能区分"单通道异常"与"多通道同时变化"。
- **Deliverables**：`edge/fusion_channel/`；`firmware/common/` 内对应实现；`docs/research/methodology.md`。
- **Tests**：主机侧单测 + 与 Python 的 parity 测试；针对两种**人工构造 fixture** 的判定测试：① 湿度骤升而温度/光照不变（预期：单通道可疑）；② 多通道同向变化（预期：环境变化）。
- **Exit Criteria**：
  - **B2 vs B1 的对比实验（EXP-003）完成**：在固定数据集上，报告跨通道融合相对单通道的 **fault→event 与 event→fault 误判率**（绝对值、差值、相对差、置信区间、逐场景），**不设门槛**（上升即 H2 被否定，同样如实报告）。
  - 融合决策在固件与 Python 上**逐步一致**（parity 测试通过）。
  - 决策输出包含 `reason_codes` 且与实际触发的判据一致。
  - **本阶段的判决只依赖本节点数据**，不需要第二个节点、不需要任何邻节点信息。

---

### Phase 6 — Cross-Node Fusion（主 RQ 的核心，**网关侧**）
- **Goal**：完成主 RQ 的验证。
- **前置**：**第二套 SHT30 + BH1750 已到位**（否则本阶段无法开始）。
- **Deliverables**：`edge/fusion_node/` + `firmware/gateway/` 内的跨节点融合；session / beacon / 窗口配对机制；`docs/research/results.md` 的第一版。
- **执行顺序（内部两步，不可跳）**：
  1. **先测量时间同步**（EXP-004）：在两节点真实运行时测出 offset / jitter / drift。**只有当数据表明简单同步不足时**，才引入线性漂移拟合（§15.1 的升级条件）。这一步的结论要写进 `design_decisions.md`。
  2. **再做跨节点融合与对比**（EXP-005 / EXP-006）。
- **Tests**：时间对齐与窗口配对的单元测试（含"数据不足时拒绝融合"）；两节点同镜像的行为对称性检查；`insufficient_alignment` 路径的覆盖测试。
- **Exit Criteria**：
  - **B 与 C 同房间（30–50 cm）**部署，完成 **EXP-005**：① **空间共享事件**（整区开/关灯、同一区域湿度整体变化）下两节点**同向**变化；② **单节点传感器故障**时另一节点**不受影响**。**这两个 case 必须都真实演示出来**，且各自有独立的真值标注。
  - **局部扰动**（只对 B 呼气、只遮挡 B 的 BH1750）作为 **secondary stress test** 单独执行，其结果**不计入主 RQ 指标**，只写入 limitations。
  - **B3 vs B1 的对比（EXP-006）完成**：fault→event 与 event→fault 误判率**同时报告**，含绝对差、相对差、置信区间、逐场景结果；样本量允许时给出预先声明的统计检验。
  - `session_alignment` 落库（offset / jitter / drift 三项实测值）；`insufficient_alignment` 的节点不参与融合（有测试覆盖）。
  - 主 RQ 得到书面回答（成立或**不成立**），写入 `docs/research/results.md`，**不修饰**。

---

### Phase 7 — Adaptive Sampling 集成（SQ1）
- **Goal**：验证 trust 信号是否改善采样策略。
- **Deliverables**：`edge/adaptive_sampling/`（vendored，S5）；`edge/adaptive_sampling/trust_aware/`（S6）。
- **Tests**：沿用 AdaptiveSense 的 parity 测试范式；策略在"传感器自激漂移"场景下**不应**提高采样率（这是 trust-aware 相对纯变化率的预期差异点）。
- **Exit Criteria**：
  - **EXP-007 完成**：S5 vs S6，同时报 误报率、事件召回、采样数、通信字节数。
  - 明确回答 SQ1（含否定情形）。
  - 通信指标**必须标注为应用层指标**（沿用 AdaptiveSense 的限定写法）。

---

### Phase 8 — Backend + Database + Observability
- **Goal**：真机数据端到端落库并具备可观测性。
- **Deliverables**：`backend/`（FastAPI + PostgreSQL）；`docker/docker-compose.yml`；`docs/engineering/data_flow.md`；可观测性指标表与 `/metrics` 端点。
- **Tests**：ingest 校验测试；重复帧幂等写入；后端不可用时主机采集侧的排队与回放；DB 错误处理。
- **Exit Criteria**：
  - 真机运行数据端到端落库，可从 API 查回，且**能与原始日志核对一致**（逐条可比）。
  - **后端停止 5 分钟后恢复，不丢数据**（排队回放成功）。
  - 记到：节点心跳、丢包、API 错误、DB 错误、设备错误。**不引入 Prometheus/Grafana**。
  - API 层**只有 GET**（写通路仅存在于网关 ingest 内部）。

---

### Phase 9 — Dashboard
- **Goal**：工程状态视图，一眼看懂系统当前状态。
- **Deliverables**：`dashboard/`。
- **Tests**：端到端手工验证清单；无 console error。
- **Exit Criteria**：
  - 首页首屏即：节点在线数 / 活跃告警 / 包投递率 / 当前采样率。
  - 三个节点的实时曲线，且图上**标注** anomaly / sampling change / sensor fault 三类事件。
  - 事件时间线可显示如 `14:32 anomaly detected → trust 下降 → 采样 20m→1m → I²C timeout → 14:40 恢复`。
  - 视觉：无渐变堆叠、无玻璃拟态、无赛博朋克；工程系统风格。

---

### Phase 10 — AI Diagnosis Copilot（只读）
- **Goal**：用真实工具回答"发生了什么"。
- **Deliverables**：`copilot/tools/`；`copilot/contracts/`；`agent_queries` / `tool_calls` 落库。
- **Tests**：**契约测试——任何写操作工具的存在都导致测试失败**；"回答中的每个数值必须能在 tool_calls 结果里找到"的追溯测试；工具超时/后端不可用时的降级回答。
- **Exit Criteria**：
  - 回答 `"Why did Node B report abnormal humidity this afternoon?"` 时，**调用真实工具**并给出：what happened / evidence / likely explanation / confidence / recommended checks。
  - 每条证据带 node + timestamp + metric/event 引用。
  - **回答中不出现任何工具结果之外的数值**（有测试强制）。
  - UI 只展示 action / tool / evidence / result 轨迹，**不展示模型内部思维链**。
  - 工具调用与延迟落库。

---

### Phase 11 — Evaluation（完整 EXP 矩阵）
- **Goal**：产出主 RQ + SQ1 + SQ2 的完整答案。
- **Deliverables**：全部 EXP 的 `manifest.json` + 原始日志 + 分析输出；`docs/research/results.md` / `limitations.md` 定稿。
- **Tests**：所有 EXP 可被一条命令重跑分析（不需要硬件）。
- **Exit Criteria**：
  - 每个 EXP 都能给出一行"EXP id + 数据集 + n + 主要指标 + 限定条件"。
  - **SQ2 的代价测量完成**（flash 增量 / RAM / 单次判定延迟 / 边缘 vs 后端的检出延迟）。
  - `limitations.md` 包含：n=2 见证节点的统计功效限制、单一摆放与单一信道的链路限制、物理故障与注入故障的分别结论、trust 是启发式分数非校准概率、以及**400 MHz 频段在日本的可部署性未验证**。
  - 未测项一律写 `Evaluation planned` 或 `not measured`。

---

### Phase 12 — Documentation + Demo
- **Goal**：把系统变成一个能被陌生人复现、被面试官追问的项目。
- **Deliverables**：`docs/engineering/design_decisions.md`（v1 必写）；`troubleshooting.md`；`failure_modes.md`；demo 剧本；一封可用的教授联系邮件材料与面试 PPT 骨架。
- **Tests**：`scripts/preflight.py` 能在一台干净机器上检出所有缺失依赖；从零 clone 到跑通离线评测。
- **Exit Criteria**：
  - 按第 27 节的 15 步 demo 剧本**完整跑通一次**（含故障注入与恢复）。
  - `design_decisions.md` 至少包含你要求的 5 条判断（为什么 LoRa / 为什么 Agent 只读 / 为什么不做 autonomous remediation / 为什么不做复杂微服务 / 为什么算力放在边缘的哪一层）+ 本审计新增的 2 条（为什么 B/C 同场景冗余部署 / 为什么 vendor 而不是复制）。
  - README 里每个数字都能指回一个 EXP id。

---

## 19. Key Risks

| # | 风险 | 影响 | 缓解 |
|---|---|---|---|
| **R1** | **只有 1 套 SHT30/BH1750，跨节点证据无法做** | **阻塞主 RQ（Phase 6 整段）** | 加购约 ¥30–60；或先降级为跨通道版并把跨节点明确标注为待补（第 13 节三选项） |
| **R2** | **与已有三个项目的研究问题重叠**，评审认为增量不足 | 修士申请与 FDE 叙事的核心风险 | 主 RQ 锁定在"故障 vs 事件判别"；SensorTrust/AdaptiveSense 明确作为 baseline 引用而非覆盖；EventGuard 只取传输层并在文档里写明区隔 |
| **R3** | **`adaptive-lora-iot` 与 AdaptiveSense 的离线分析重复**，两套 metrics 会算出不同数字 | 结果自相矛盾，最伤可信度 | 以 AdaptiveSense 的 `simulator/` 为唯一实现，迁入独有部分后删除另一套（技术债 #6） |
| **R4** | **无跨节点统一时间基**，跨节点相关性算不出来 | 主 RQ 的机制失效 | v1 用 §15.1 的 beacon + session id + 窗口配对 + tolerance，**先实测 offset / jitter / drift**；只有数据表明简单同步不足时，才加线性漂移拟合；无法对齐时**拒绝融合**而非勉强融合；对齐质量写入 `session_alignment` 表 |
| **R5** | **跨节点部署形态选错**（B、C 放在不同房间） | 跨节点证据完全失效，且会得出"跨节点无用"的错误结论 | 强制同房间 30–50 cm 冗余部署；不同场景部署只作为独立演示实验 |
| **R6** | **LoRa 吞吐上限**（UART 9600 + 空口速率）撑不住多通道 + 事件 + 心跳的帧率 | 采样策略被迫牺牲数据保真度 | 复用 EventGuard 的紧凑二进制帧与 CRC（已被 96 次真机验证）；先算清每帧字节预算再定采样档位；必要时把高频细节留在节点本地缓存、只上传事件与摘要（并把这一点作为明确的工程取舍写进 design_decisions） |
| **R7** | **400 MHz 频段在日本的可部署性未验证** | 若申请材料声称"可在日本部署"会被直接质疑 | 明确写为"实验室频段验证，部署频段需按 ARIB 规范另行确认"；不要声称日本可部署。若未来需要日本演示，需重新评估频段选择 |
| **R8** | **环境不可复现**（AdaptiveSense 无 venv、EventGuard venv 损坏、TinyEdgeBench 缺 pytest） | 评审或面试官 clone 后跑不起来，直接损失可信度 | `scripts/preflight.py` 统一自检；每个仓库补一次干净的 `venv + requirements` 验证并记录（技术债 #4） |
| **R9** | **实验规模失控**：真机运行是分钟到小时级的，EXP 数量一多就做不完 | 时间预算爆炸 | 每类故障的 episode 次数预先冻结；先冻结分析规则再跑测试集（继承 `adaptive-lora-iot/EXPERIMENTS.md` 的规则）；分析脚本必须能离线重跑 |
| **R10** | **Copilot 生成不可追溯的数值** | 直接违背项目核心约束，且最容易被抓到 | 契约测试强制"回答中每个数值必须能指回 tool_call 结果"；工具层只读；证据不足时明确说"证据不足" |
| **R11** | **EdgeSense-Fusion 的 README 夸大**被误当作 EdgeSense 的一部分 | 作品集可信度受损 | 归档改名 + 修正措辞（技术债 #3） |
| **R12** | **范围蔓延到执行器/控制** | 与"Agent 只读"的核心立场冲突 | 架构上不留控制入口；Copilot 工具表无写操作；契约测试强制 |
| **R13** | **两节点固件分叉** | "差异来自传感器还是固件"无法回答，跨节点实验作废 | B/C 强制同一镜像，差异只在配置；CI 检查两节点构建产物一致 |
| **R14** | **土壤通道未标定却被用于"环境事件"** | 把未标定的噪声当作环境变化 | Phase 1 强制逐通道 validity；土壤未标定即标为 invalid；沿用 esp32-agri-node 的两点校准流程 |
| **R15** | **局部扰动被当作 shared-event 正样本**（只对 B 呼气、只遮挡 B 的 BH1750），污染主 RQ 的类别定义，使"传感器故障 vs 环境事件"退化为不可分 | **主 RQ 失去意义**，且会得出"跨节点无用"的错误结论 | 事件分类法严格区分 `sensor_fault` / `shared_event` / `localized_event`（见 Phase 0.1）；`localized_event` 单列、**不计入主 RQ 指标**，只作 stress test 与 limitation 报告 |
| **R16** | **跨节点融合被放在节点上**（架构矛盾：星型拓扑下节点拿不到邻节点数据） | 设计自相矛盾，实现时必然返工 | 已在 Phase 0.1 修正：跨节点融合固定放在 Gateway A（`firmware/gateway/` 与 `edge/fusion_node/` 同源）；节点侧采样闭环不依赖邻节点 |

---

## 20. Features That Should Be Cut

按"绝对不做 / 暂不做 / 降级做"三档：

**绝对不做（做了会伤项目）**

| 特性 | 理由 |
|---|---|
| Mesh / 多跳网络 | 3 个节点做 Mesh 只增加复杂度，不增加研究价值。星型足够 |
| 任何自主修复：重启设备、改固件、改数据库、控制执行器 | 你已明确禁止。**架构上不留入口**，而非"暂时不做" |
| 多网关接管 / split-brain / epoch 治理 | 那是 Smart-Agriculture-Edge-AI 的 RQ，已有人做 |
| 深度学习异常检测 | 与"算法必须确定性/统计可定义/可测试"的要求冲突；TinyEdgeBench 已量化证明复杂模型买回的精度在噪声内 |
| Wi-Fi / MQTT 传感器节点 | 与 LoRa 路线冲突；增加功耗与复杂度；且 AdaptiveSense 已走过 |
| Isolation Forest / 无监督模型作为主判别器 | 不可解释、不可逐条追溯，与"证据必须可引用"冲突 |

**暂不做（v1 之后按需）**

| 特性 | 触发条件 |
|---|---|
| Kafka / ClickHouse / Elasticsearch / Kubernetes | 数据量与部署复杂度真正到瓶颈时（第一版远未到） |
| Prometheus + Grafana | 结构化日志 + 指标表 + `/metrics` 端点不够用时 |
| OTA 固件升级 | 不是研究问题；v1 用串口烧录 |
| 漂移检测模型（Node 级长期 baseline 自适应） | 主 RQ 完成后若有剩余价值 |
| EdgeFaultLab 式的后端韧性断言测试 | Phase 8 后端成形后 |

**降级做（做，但明确降低权重）**

| 特性 | 降级方式 |
|---|---|
| OLED 显示 | 仅作为 demo 的可视化点缀，**不作为任何 Exit Criteria** |
| 能耗指标 | 不测电流，不上报焦耳；只报 **UART 时间代理**，并使用 EventGuard 已确立的限定措辞 |
| 移动端适配 | Dashboard 只在桌面浏览器保证 |
| 后端用户/权限系统 | 单用户本地部署，不做认证体系 |
| 论文/研究计划书写作 | Phase 0 明确暂缓；先有系统与实验，再有材料 |

---

## 21. What Makes This Strong for a Master's Application

**（一）你已经具备大多数申请人没有的科研纪律。**
本审计发现的三个证据：SensorTrust 主动写"这不构成普适零误报率"；AdaptiveSense 主动纠正"上传减少 97% ≠ 无线电流量减少 97%"；EventGuard-LoRa 主动报告"EventGuard 在 10 条 Pareto 前沿中占据 0 条"并保留失败案例的原始证据。**"保留否定结果"的能力，比任何正面数字都更能说明一个人能不能做研究。** 这一点必须被写进材料，而不是埋在两个仓库的 README 深处。

**（二）主 RQ 是干净的、单一的、可证伪的。**
"跨通道 + 跨节点证据能否区分传感器故障与环境事件"——它有明确 baseline（SensorTrust 单通道）、明确的核心指标（fault→event 误判率）、明确的消融阶梯（B0 ⊂ B1 ⊂ B2 ⊂ B3）。**一个 RQ + 一条 baseline 阶梯，比十个 RQ 更有说服力。**

**（三）有真实硬件、真实数据、可复现的证据链。**
150 个真实多传感器样本 + 6 次物理干预标注，加上 ESP32-S3/SHT30 的真机故障注入，加上 96 次 LoRa 真机运行的审计范式。**"我有真机数据和 SHA 可追溯的原始日志"这句话，在修士面试里的分量远超"我实现了某某算法"。**

**（四）limitations 是预先声明的，不是事后补的。**
n=2 见证节点的统计功效限制、单一摆放与信道的链路限制、物理故障与注入故障的分别结论、trust 不是校准概率、频段可部署性未验证。**能列出自己的局限，说明你知道自己的结论边界在哪** —— 这恰恰是导师最想看到的品质。

**（五）与实验室方向的衔接点具体。**
传感器可靠性、边缘计算、资源受限网络上的异常检测——每一项都能在 EdgeSense 里找到对应的、有数据支撑的具体工作，而不是泛泛的"我对 IoT 感兴趣"。

**需要补的短板**：`adaptive-lora-iot` 未发表（技术债 #1）。**你最有价值的真机资产目前无法被别人看到**，这是申请材料上最不应存在的浪费。

---

## 22. What Makes This Strong for an FDE Portfolio

FDE 考核的不是"用了多少技术"，而是**面对一个混乱的真实系统，能否理解它、定位问题、接入数据、设计工具、部署、测试、Debug、并解释取舍**。逐条对照：

| FDE 能力 | EdgeSense 里对应什么 | 已有基础 |
|---|---|---|
| 理解系统 | 从物理量到数据库到 UI 的完整链路，含每一层的失败模式 | 已有 5 层散落资产 |
| 定位问题 | 故障注入 + 断言式观测：把"我们处理了 X"从文档变成测试结果 | EventGuard 的 faults.c + SensorTrust 的注入真值 |
| 接入数据 | 真机传感器 → I²C → LoRa → 网关 → 串口 → 后端 → DB | adaptive-lora-iot 已跑通前段 |
| 设计工具 | Copilot 的只读工具契约 + JSON Schema + 追溯性强制 | **全新**（这是最需要补的） |
| 部署 | docker-compose 两个服务、`.env.example`、deployment.md | EdgeSense-Fusion 骨架 |
| 测试 | C 单测 + Python 单测 + C/Python parity + 契约测试 + 端到端 | 前两项已有极高标准 |
| Debug | device_events + network_metrics + UART_DIAG + 可观测性指标 | 部分已有 |
| 解释取舍 | `design_decisions.md` + limitations.md + 保留否定结果 | EventGuard 已示范 |

**EdgeSense 相对普通 FDE 作品集的三处差异化优势**：

1. **不是 CRUD + 调 API，而是有真实物理链路**：I²C 时序、LoRa 空口、UART 流解析、看门狗、时间基对齐——这些是"真实系统才会遇到"的问题。
2. **"Agent 只读"是一个被架构强制的设计立场**，不是遗漏。有契约测试、有数据库写通路隔离、有工具表白名单。**能说清"我为什么不让 Agent 写"的人，比"让 Agent 什么都能干"的人更可信。**
3. **有可审计的实验体系**：EXP id + manifest + 固件哈希 + 原始日志 SHA + 自动分析脚本 + 渲染生成的 README 数字。**这套东西证明的是"这个人交付的东西可以被别人验证"，这是 FDE 最核心的交付品质。**

---

## 23. Immediate Next Step

> **状态更新（2026-09-27，Phase 0.2）：本节 4 项决断已全部批准，不再是阻塞项。以下为历史记录。**
> 权威版本见 `PHASE_0_2_FINAL_DESIGN_FREEZE.md` §0（批准表）与 §14（Phase 1 准入条件）。
> - 决断 1 → ✅ **选 (a) 加购**：第二套 SHT30 + BH1750；soil **不作** shared-event 通道。
> - 决断 2 → ✅ **选项 A 变体**：整体迁入，**但不先创建独立公开 repo**；freeze 原版并保留 provenance。
> - 决断 3 → ✅ **采纳**：主 RQ = `sensor_fault` vs `spatially shared environmental event`。
> - 决断 4 → ✅ **接受保守表述**：只声称实验室结果，禁止"适合日本部署"。

Phase 0 到此停止。**在开始 Phase 1 之前，需要你决断 4 件事**（都是阻塞级或方向级）：

### 决断 1（阻塞）：第二套传感器
是否加购 1 颗 SHT30 + 1 颗 BH1750（约 ¥30–60）？这是主 RQ 能否成立的前提。
- **选 (a) 加购** → 按完整路线图推进，Phase 6 可做。
- **选 (b) 不加购** → 主 RQ 降级为跨通道版，跨节点部分明确标注待补；我会相应调整 Phase 5/6 的 Exit Criteria。

### 决断 2（阻塞）：`adaptive-lora-iot` 的归属
它的 Phase 2 就是 EdgeSense 的 Phase 1–2，且带 150 个真实样本。
- **选项 A（推荐）**：整体迁入 EdgeSense 重构，其仓库归档；`paper/` 骨架留在原处不推进。
- **选项 B**：先在原处补 `git init` + 发布，再作为独立上游 vendor 进 EdgeSense（保留更清楚的 provenance，但多一层维护）。
- **选项 C**：让它继续独立发展，EdgeSense 从零重建节点固件（**不推荐**——等于放弃你最好的真机资产，并制造两套并行固件）。

### 决断 3（方向）：主 RQ 是否采纳
我建议的主 RQ（跨通道 + 跨节点证据区分传感器故障与环境事件）会**把你原本候选的 RQ1/RQ2 降级为 baseline 与次级问题**。请确认这个调整是否接受，或你有其他偏好。

### 决断 4（合规）：频段表述
是否接受"实验室频段验证、部署频段需按 ARIB 规范另行确认，不声称日本可部署"这一限定？若你希望材料里出现日本可部署的表述，需要另做频段合规核查（这会增加一个工作项）。

### 决断完成后，Phase 1 的第一步（不改任何已有仓库）

1. 新建 `EdgeSense/` 骨架目录（只建目录与文档，不建代码）。
2. 写 `docs/vendored/*.md`：记录每个上游仓库的 SHA、vendor 范围、边界声明（含 `adaptive-lora-iot`，共 5 份）。
3. 写 `docs/engineering/design_decisions.md` 的前两条：**为什么 B/C 同场景冗余部署**、**为什么 vendor 而不是复制**。
4. 写 `docs/hardware/wiring.md`：把三个项目里已一致、但分散的引脚定义收敛为单一权威来源（SHT30 0x44 / SDA GPIO8 / SCL GPIO9；BH1750 0x23；土壤 ADC GPIO1；E220 M0/M1/AUX GPIO13/14/15，TXD/RXD GPIO17/16，地址 0x0000 / REG0 0x62 / 信道 0x17 / 9600 8N1）——**并在真机上逐项复核后才标注为"已验证"**。

**然后停下。** Phase 1 的准入条件以 `PHASE_0_2_FINAL_DESIGN_FREEZE.md` §14 的 9 条为准（其中 C3 = 第二套传感器已在真机上验证，是唯一的物理阻塞项）。

---

## 附录 A：本机独立验证记录

以下结论在本次审计中**实际执行**得到，非引用 README：

| 验证项 | 命令 | 结果 |
|---|---|---|
| SensorTrust Python 测试 | `./.venv/bin/python -m pytest tests/ -q` | **27 passed in 3.55s** |
| SensorTrust C 核心测试 | `cc -std=c11 -Wall -Wextra -Werror core/sensor_trust.c tests/test_core.c -lm` | **21 tests / 404 checks 全过，零警告** |
| SensorTrust 注入器测试 | `cc -Wall -Wextra -Werror -I core -I firmware/main ... test_injector.c` | **7 injection modes and invalid-read preservation passed** |
| AdaptiveSense 策略编译 | 6 个 .c 文件 `cc -std=c11 -Wall -Wextra -Werror -fsyntax-only` | **全部零警告** |
| AdaptiveSense Python 管线 | `python -m simulator.replay` | **失败：`ModuleNotFoundError: No module named 'yaml'`（环境缺失，非代码缺陷）** |
| AdaptiveSense C 主机测试 | `python tests/host_build.py` | **失败：无 pytest（环境缺失）** |
| lora-p2p 实验日志复核 | 解析 `logs/20260925-011325-gateway.log` | **RX 602 条；序号 30–631 完全连续；重复 0；panic/abort 0 → TEST_REPORT 数字成立** |
| adaptive-lora-iot 真实数据 | 直接读 CSV | **150 样本 / 5 s；湿度 55.6%→90.6%→73.8%；6 条物理事件标注；土壤通道恒 4095 且 valid=1（缺陷）** |
| EdgeSense-Fusion 数据库 | `sqlite3` 直读 | **仅 `demo_esp32_001`，110 行平滑合成斜坡 → 无真实硬件数据** |
| 引脚配置一致性 | 跨 3 个项目 grep | **SHT30 0x44 / SDA 8 / SCL 9 / BH1750 0x23 / soil ADC 1 —— 三处一致** |
| 本机 ESP-IDF | 目录检查 | **存在于 `~/esp/esp-idf`（版本号待 Phase 1 用 `idf.py --version` 确认）** |

---

## 附录 B：GO / REFACTOR / DROP 汇总

| 项目 / 模块 | 裁决 | 一句话理由 |
|---|---|---|
| SensorTrust `core/sensor_trust.c/.h` | **GO** | C11 可移植、语义冻结、本机 21 tests/404 checks 全过；作为单通道 baseline 与信任内核 |
| SensorTrust `firmware/main/fault_injector.c` | **GO** | 本机已验证；扩展到多通道即可 |
| SensorTrust `hardware/` 证据管线 | **GO** | "原始日志→指标→渲染 README"的机制应当成为 EdgeSense 标准 |
| SensorTrust v0.2 单通道实验矩阵 | **DROP** | EdgeSense 有自己的 EXP 体系；不重复其单通道实验 |
| AdaptiveSense `change_detector.c` / `adaptive_scheduler.c` | **GO** | `-Werror` 零警告；作为 S5 baseline 直接引用 |
| AdaptiveSense `simulator/` | **GO** | 即 EdgeSense 的离线评测框架（7 场景 / 24 标签 / 6 baseline） |
| AdaptiveSense parity 测试与 config parity 检查 | **GO** | 防止固件与仿真分叉的机制，沿用 |
| AdaptiveSense `sensor_supervisor.c` | **REFACTOR** | 单传感器重探测 → 多通道 × 多故障类型 |
| AdaptiveSense `communication.c`（Wi-Fi/MQTT） | **DROP** | EdgeSense 走 LoRa |
| AdaptiveSense `power_mgmt.c` | **DROP（v1）** | 不做功耗声明；若日后需要，只能作为时间代理 |
| TinyEdgeBench 测量方法（`measure_flash.py` / `export_models.py`） | **REFACTOR** | 目标换成 EdgeSense 自己的判别器候选；保持独立 repo |
| TinyEdgeBench 合成数据集与 4 类分类结论 | **DROP** | 任务定义不同 |
| lora-p2p `e220.c` 驱动 | **DROP** | 已被 EventGuard 更成熟的传输层取代 |
| lora-p2p 实验与 TEST_REPORT | **GO（归档为证据）** | 本机复核成立；作为 P2P 可行性证据被引用，不继续维护 |
| **adaptive-lora-iot 整个节点固件 + 采集/标注流程** | **GO（整体迁入）** | **EdgeSense 的系统基座；150 真实样本 + 6 次物理事件标注** |
| adaptive-lora-iot 的逐帧级 `valid` | **REFACTOR（必修）** | 恒 4095 的死通道被上报为有效（实测缺陷） |
| adaptive-lora-iot `analysis/` | **REFACTOR → 合并** | 与 AdaptiveSense `simulator/` 重叠，保留独有部分后删除重复 |
| EventGuard-LoRa 传输层（e220 / stream parser / protocol） | **REFACTOR** | 剥离 importance/strategy；96 次真机验证过的帧格式 |
| EventGuard-LoRa 运行编排与审计范式 | **REFACTOR** | 泛化为 EdgeSense 的 `experiments/` 运行器与审计 |
| EventGuard-LoRa `faults.c` | **REFACTOR** | 加入陈旧数据与节点临时离线 |
| EventGuard-LoRa `importance.c` / `strategy.c` 与其 RQ | **DROP** | 研究问题轴不同（投递优化 vs 数据可信度）；保留为独立研究线 |
| EdgeSense-Fusion `backend/app/` 分层 + Pydantic + compose | **REFACTOR** | 唯一现成后端骨架；改 LoRa 接入 + PostgreSQL + schema 重设计 |
| EdgeSense-Fusion `dashboard/` 骨架 | **REFACTOR** | 由演示首页改为工程状态页 |
| EdgeSense-Fusion Isolation Forest | **DROP** | 非确定性、训练在演示数据上 |
| EdgeSense-Fusion DHT22/MPU6050 固件路线 | **DROP** | 与 EdgeSense 硬件无关 |
| EdgeSense-Fusion 仓库本体 | **REFACTOR → 归档改名** | 无 git、README 夸大、与 EdgeSense 同名；须先修再归档 |
| Smart-Agriculture-Edge-AI 运行时代码 | **DROP** | 研究命题不同（网关接管 + 执行器安全） |
| Smart-Agriculture-Edge-AI 协议词汇与场景写法 | **GO（概念引用）** | 作为 `protocol.md` 的设计参考 |
| EdgeFaultLab | **DROP（暂缓）** | 层次不匹配；Phase 8 后可选用于后端韧性测试 |
| esp32-agri-node 分析管线 | **DROP** | 研究问题正交，属另一条线 |
| esp32-agri-node 接线与土壤两点校准 | **GO（引用）** | 直接引用其 `wiring.md` 与 `calibrate_soil.py` |
| `ChatGPT/edge/esp32_e220_*` ×5 | **DROP（归档）** | 已被后续三层项目完全覆盖 |
| **跨通道 + 跨节点证据融合** | **新建（无 GO/REFACTOR 可依）** | **这是 EdgeSense 的唯一实质新贡献** |
| **Backend / DB / 可观测性 / Dashboard / 只读 Copilot** | **新建** | 无合格现成资产 |

**统计：GO 11 项、REFACTOR 11 项、DROP 14 项、新建 2 大块。**

---

*Phase 0 结束。等待第 23 节四项决断后进入 Phase 1。*
