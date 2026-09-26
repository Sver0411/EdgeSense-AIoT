# EdgeSense Phase 0.2 — Final Experimental Design Freeze

**EdgeSense: Fault-Aware Adaptive Sensing and AI-Assisted Diagnosis for Resource-Constrained IoT Networks**

冻结日期：2026-09-27
上游文档：`PHASE_0_DESIGN_REVIEW.md`（Phase 0）、`PHASE_0_1_ARCHITECTURE_CORRECTION.md`（Phase 0.1）
本轮性质：**最后一次设计修订。本轮结束后 EdgeSense v1 架构冻结。**
本轮未创建任何运行代码，未开始 Phase 1，未修改任何已有仓库。

---

## 冲突优先级（三份文档的最终裁决顺序）

> **Phase 0.2 ＞ Phase 0.1 ＞ Phase 0**
>
> 任何冲突以序号大者为准。本文件的声明同时作用于前两份文档。已在前两份文档的开头写入同样的优先级声明。

**冻结声明**：本文件第 12 节所载架构自 2026-09-27 起冻结。
**除非后续真实实验数据证明某个设计不可行**，否则不得再做宏观架构重设计。
任何设计变更必须：① 引用触发它的 EXP id 与具体数据；② 以"修正案"形式追加，不原地改写历史结论。

---

## 0. 本轮解决的五个缺陷

| # | 缺陷 | 性质 | 处置 |
|---|---|---|---|
| **F1** | **EXP-003 数据集逻辑不成立**：B2 vs B1 用的是 fault-only 数据集，却报告 fault→event 与 event→fault 两个误判率。fault-only 数据里**没有 event 样本**，event→fault 无从计算 | **指标无真值支撑，是硬错误** | 重建 EXP-003 数据集：fault 来自受控注入，event 来自真实物理干预；并明确该数据集**不是** Primary RQ 的 shared-event 数据集（第 5 节） |
| **F2** | **Cross-Channel Fusion 的 claim 过宽**：隐含假设"单通道变化 = 故障、多通道同时变化 = 环境事件"。但真实环境事件可能只影响一个物理量（光照事件主要影响 BH1750、湿度事件主要影响 humidity） | **结论不成立**，且该假设可被 I²C 总线级故障反例直接推翻 | B2 重新定位为"提供额外 contextual evidence"，**不是通用 fault/event 判决器**；逐 family 报告；允许"无改善"（第 3、5 节） |
| **F3** | **自适应采样下跨节点配对失效**：用 `(session_id, window_index)` 且隐含 `B.seq == C.seq`，但自适应采样下两节点 cadence 可能不同，序号不再是同一时间窗 | **配对机制在目标场景下不成立** | 改为 gateway 定义的 fusion window + nearest-valid-sample + freshness 检查；**明令禁止**把 seq 相等当同步依据；定义 `max_sample_age`；Primary RQ 阶段用固定密集参考采样隔离混淆变量（第 7、8 节） |
| **F4** | **SQ2 定位错误**：把它当成研究问题，并把"gateway-side 避开 LoRa 延迟"作为论据——但 gateway 与 backend 判定**都在 LoRa 数据已到达 gateway 之后**，差异不在 LoRa | **论据错误** | **删除 SQ2**，降级为 Engineering Evaluation E1，并**禁止**再写"gateway-side avoids LoRa latency"（第 9、10 节） |
| **F5** | **Fault Taxonomy 混装**：把 network fault 与 device unavailable 全塞进 `sensor_fault` | **类别污染**，指标失去含义 | 拆为正交三分类 `sensor_fault` / `network_fault` / `device_unavailable`；真值侧用 `truth_category` + `event_source` + `ground_truth_source`，**运行侧只有 `decision_label`**（Amendment 3），避免一个字段同时承担 sensing / network / availability 语义 |

### 本轮批准的 4 项用户决策（原 blocker 全部解除）

| ID | 决策 | 生效内容 |
|---|---|---|
| **B1** | ✅ 批准 | 购置第二套 **1 × SHT30 + 1 × BH1750**。**暂不要求第二个 soil moisture**——土壤湿度空间局部性强，**不作为 spatially shared event 的核心研究通道** |
| **B2** | ✅ 批准 | `adaptive-lora-iot` 作为 EdgeSense 的**直接系统基座迁入**。**不先创建独立公开 GitHub repo**。处理方式：freeze 原版 → 记录 source commit/hash/path → 保留原始真实实验数据与 provenance → 选择性迁入 → **不修改历史实验结果** |
| **B3** | ✅ 批准 | Primary RQ = `sensor_fault` vs `spatially shared environmental event`；`localized_event` 只作 stress test + limitation |
| **B4** | ✅ 批准 | 保守表述。只声称当前硬件与实验配置下的**实验室结果**。**不声称**"适合日本部署"/"符合日本无线法规"/"可直接在日本使用" |

---

## 1. Final Research Scope

**EdgeSense v1 只允许以下研究范围。不再增加 RQ，不再把工程功能升级为研究问题。**

### 1.1 Primary RQ

> **跨通道 + 跨节点证据能否把【单个节点的传感器故障】与【空间共享环境事件】区分开，相比单通道读数，它对空间共享事件召回引入了什么 trade-off？**

English（**冻结措辞**）：

> Can fusion of within-node cross-channel evidence and across-node (redundant neighbour) evidence distinguish a **single-node sensor fault** from a **spatially shared environmental event**, compared with single-channel readings, **and what trade-off does it introduce in shared-event recall**?

**必须报告的全部量**（缺一即视为未完成）：

| # | 报告项 |
|---|---|
| 1 | `fault→event` rate |
| 2 | `event→fault` rate |
| 3 | `shared-event recall` |
| 4 | absolute delta |
| 5 | relative delta |
| 6 | confidence interval |
| 7 | per-scenario results |

> **⚠️ 不预设任何"material"阈值。**
> 原措辞中的 "without materially reducing recall" 已删除——"materially" 没有可操作定义，会变成变相的成功门槛（与 Phase 0.1 删除"≥50%"同一个理由）。**召回的变化以第 3–6 项的量呈现，而不是以"是否显著降低"判定。** 主假设允许被否定。

### 1.2 Secondary RQ

> **Trust-aware adaptive sampling**：把可信度信号反馈进采样策略，相比纯变化率自适应，在减少误报与资源消耗的同时，**对空间共享事件召回引入什么变化**？

**报告口径与 Primary RQ 一致**（见 §1.1 的 7 项）：误报率、shared-event recall、采样数、通信字节数，各自给出绝对差、相对差、置信区间与逐场景结果；**不预设"不降低"这类未定义阈值**，而是把召回的变化作为**被测量的 trade-off** 呈现。

### 1.3 Engineering Evaluations（**不是研究贡献**）

E1 网关侧 vs 后端侧的放置代价 · E2 传输可靠性 · E3 资源代价 · E4 延迟刻画 · E5 可观测性 · E6 AI 诊断 · E7 部署与可复现。

**这些项目服务 FDE portfolio 与工程设计论证，不写进论文贡献列表。**

### 1.4 明确不在研究范围

| 项目 | 定位 |
|---|---|
| **AI Diagnosis Copilot** | **不是研究贡献**。Engineering layer。只读工具 + 证据可追溯 |
| **Dashboard** | **不是研究贡献**。Engineering layer |
| **Backend** | **不是研究贡献**。Engineering layer |
| `localized_event` vs `sensor_fault` 的区分 | **不声称能解决**。only stress test + limitation |
| `network_fault` 的判别 | **不是** Primary RQ 内容。进 EXP-010 的 transport reliability 评估 |
| `device_unavailable` | availability / observability 事件。**不进入分类指标** |
| 采样频率/开销/延迟/召回的 trade-off 刻画 | **评测框架**，不是 RQ |
| Mesh / 多网关 / actuator / 控制路径 / K8s / Kafka | **不做** |

---

## 2. Final Event / Fault Taxonomy

> **本节的核心原则（Amendment 3）**：**Ground Truth 与 System Decision 严格分离。**
> 真值只存在于 `truth/episodes.csv`；**运行系统不得知道实验真值**。两者的连接点只有一个：`analyze_experiment.py`。

### 2.1 两侧字段必须分离（取代原先单一的 `kind` / `event_category` 字段）

**A. 真值侧 —— 只存在于 `truth/episodes.csv`（运行系统不可读）**

| 字段 | 取值 | 语义边界 |
|---|---|---|
| **`truth_category`** | `sensor_fault` / `shared_event` / `localized_event` / `network_fault` / `device_unavailable` | **真实类别**。由实验组织者事后标注，不由系统产生 |
| **`event_source`** | `sensing` / `environment` / `radio` / `power` / `firmware` | **真实的产生层**。sensing ↔ sensor_fault；environment ↔ shared/localized_event；radio ↔ network_fault；power/firmware ↔ device_unavailable |
| **`ground_truth_source`** | `injection_plan` / `physical_intervention_log` / `firmware_diagnostic_counter` / `host_observation` | **真值从哪来**。禁止用检测器输出反推真值 |
| **`physical_or_injected`** | `physical` / `injected` | **真值侧字段**，不是系统观测。系统只观测到症状，不知道它是物理造成还是注入造成 |

**B. 运行侧 —— 运行系统产出的只是决策，不是真值**

| 字段 | 取值 | 所在表 | 语义边界 |
|---|---|---|---|
| **`decision_label`** | `normal` / `sensor_fault` / `shared_event` | `anomaly_events` | **系统的判定输出**。它是"系统认为这是哪一类"，**不是"实际上是哪一类"** |
| **`decision_source`** | `gateway_fused` / `node_only` | `anomaly_events` | 判定来自哪一层 |
| **`confidence`** / **`reason_codes`** / **`evidence_json`** | — | `anomaly_events` | 判定的置信、理由码、证据 |
| **`subsystem`** | `sensing` / `radio` / `power` / `firmware` | `device_events` | 系统**观测到**是哪个子系统报了症状。**与真值侧的 `event_source` 是两个不同字段**：两者不一致正是混淆矩阵要测的东西 |

**A 与 B 不可合并，也不可互相派生。** 一个字段同时承担 sensing / network / availability 语义会让指标失去含义——这正是 F5；而让运行系统持有真值标签，会让评估失去独立性。

**三个命名上的强制约束**
1. 真值侧字段一律以 `truth_` 前缀开头（`truth_category`），或明确属于真值记录的属性（`event_source` / `ground_truth_source` / `physical_or_injected`）。
2. **运行侧不得出现 `event_category` 或 `truth_category`。** `anomaly_events` 用 `decision_label`。
3. **运行侧不得出现 `physical_or_injected`。** 它只属于真值记录。

### 2.2 类别定义与归属

| `truth_category` | `event_source` | 包含 | `ground_truth_source` | 进哪个评估 |
|---|---|---|---|---|
| **`sensor_fault`** | `sensing` | RANGE · STUCK · SPIKE · DRIFT · OFFSET/BIAS · NOISE increase · missing reading · I²C 接口故障 · sensor disconnect | `injection_plan`（注入）或 `physical_intervention_log`（拔插/断电） | ✅ **Primary RQ**（+ EXP-002/003） |
| **`shared_event`** | `environment` | 整区开/关灯；同一区域湿度整体变化；同一区域温度整体变化 | `physical_intervention_log`（同一时刻对**两个节点**生效） | ✅ **Primary RQ** |
| **`localized_event`** | `environment` | 只对某一节点的 SHT30 呼气；只遮挡某一节点的 BH1750 | `physical_intervention_log`（只针对**一个节点**） | ❌ 不进 Primary RQ 指标。作 EXP-003 的 event 类 + EXP-009 stress test + limitation |
| **`network_fault`** | `radio` | LoRa 丢包 · 重复 · CRC 失败 · 陈旧包 · 通信延迟 · 临时无线电中断 | `firmware_diagnostic_counter` + `host_observation` | ❌ **不进 Primary RQ 的 `sensor_fault` 指标**。进 **EXP-010** |
| **`device_unavailable`** | `power` / `firmware` | 节点完全断电 · 固件崩溃 · 节点重启 | `host_observation`（心跳超时）+ `firmware_diagnostic_counter` | ❌ **不进入分类指标**。作为 availability / observability 事件 |

另有一个平凡真值类别 **`normal`**（"无事件窗口"的真值标记），**不是** `truth_category` 的取值，而是 `truth/episodes.csv` 之外由窗口定义推导的对照集。

### 2.3 Primary RQ 的类别空间（只有这两个 + normal）

**横轴 = 真值（`truth_category`，来自 `truth/episodes.csv`）；纵轴 = 系统判定（`decision_label`，来自 `anomaly_events`）。**

```
                     TRUTH = sensor_fault   TRUTH = shared_event   TRUTH = normal
DECISION = sensor_fault        TP                 ✗ event→fault         FP
DECISION = shared_event     ✗ fault→event ★            TP              FP
DECISION = normal              FN                     FN               TN
                                   ↑
                    第 3 列 = shared-event recall 的分母所在
```

★ = Primary RQ 的核心指标（`fault→event` rate）。

**`localized_event` / `network_fault` / `device_unavailable` 的真值窗口在计算 Primary RQ 指标时被排除**，并单独报告各自的混淆结果。

### 2.4 B1 的能力边界必须分开报告（重要的公平性要求）

`SensorTrust` v0.1 的**冻结语义只有 5 类**：RANGE / STUCK / SPIKE / DRIFT / MISSING。其中的已知盲区（OFFSET 恒定偏置）由它自己的 README 明确声明。

因此 EXP-002 必须把故障 family 分成两组报告：

| 组 | family | 报告方式 |
|---|---|---|
| **B1 声明覆盖** | RANGE · STUCK · SPIKE · DRIFT · MISSING | 正常算 B1 的 precision / recall / F1。**这是公平的主对照** |
| **B1 声明不覆盖** | OFFSET/BIAS · NOISE increase · I²C 接口故障 · sensor disconnect | **不记为 B1 的失败**，而是记为 "out of B1's declared scope"。由可观测层（逐通道 validity / supervisor）接管，单独报告 |

**理由**：B1 是一个语义已冻结的既有工作。拿它没声称能做的 family 去算它的 recall 是不公平对照，会污染"B1 vs B3"的归因。

---

### 2.5 分离的执行机制（不可绕过）

| 约束 | 落地方式 |
|---|---|
| **真值只存在于 `truth/episodes.csv`** | 该文件由实验组织者人工/半自动标注，**不经过固件、不经过后端、不进入数据库** |
| **运行系统不得知道实验真值** | 后端数据库 schema 里**没有** `truth_category` 列；`anomaly_events` 只有 `decision_label`。固件不接收、不存储、不上报任何真值标签 |
| **注入器只做注入，不做标注** | 注入器按**预先声明**的 injection plan 执行；它记录的调度表属于实验元数据，**不是运行系统的判定输入**。节点上报的是**观测到的症状**（读失败、值越界、超时），不是"我注入了什么" |
| **唯一的连接点** | `analyze_experiment.py` 读 `truth/episodes.csv` 与 `results/decisions.*`，按窗口对齐，做 `truth_category` **vs** `decision_label` 的匹配，产出混淆矩阵与全部指标 |
| **门禁** | 任一 `truth_category` 在评估集中出现 0 次而指标要求它的分母时，脚本**必须报错退出**（§5.4） |

**`analyze_experiment.py` 的输出契约**

```
输入：truth/episodes.csv（truth_category）+ results/decisions.*（decision_label）
输出：
  confusion matrix      truth_category × decision_label
  Primary RQ 指标        fault→event rate / event→fault rate / shared-event recall
                        absolute delta / relative delta / confidence interval / per-scenario
  分离报告              localized_event 单独一组；network_fault、device_unavailable 另计，不混入
  数据构成表            每个 truth_category 的 episode 数、来源、标注方式（防 F1 类错误）
```

**一条硬规则**：`analyze_experiment.py` **不得**被运行系统导入（后端与固件都不依赖它）。它在离线评估环境中运行。

---

## 3. Final B0–B3 Baselines（判别侧）

| ID | 名称 | 运行位置 | 输入 | 允许声称什么 | 允许的结果 |
|---|---|---|---|---|---|
| **B0** | Raw + fixed threshold | 节点 | 单通道原始读数 | 最朴素对照 | — |
| **B1** | SensorTrust（单通道） | 节点 | 单通道样本流 | 5 类冻结语义内的可疑数据判定 | — |
| **B2** | **Cross-channel context** | 节点 | **仅本节点**多通道 | **只声称"提供了额外 contextual evidence"**——**不是**通用 fault/event 判决器 | ✅ 有改善 / ✅ **无改善** / ✅ 只对某些 fault 或 event family 有改善 |
| **B3** | Cross-channel + cross-node（= **EdgeSense**） | 节点 + **网关** | 本节点多通道 + 邻节点同物理量 | Primary RQ 的待验方法 | ✅ 有改善 / ✅ 无改善（**允许主假设被否定**） |

### B2 的三条硬约束

1. **只能使用本节点信息。** 不得读取邻节点数据、不得读取网关产出的任何量。**必须有一个自动化测试来强制这条**（见 §3.2）。
2. **禁止把"多通道同时变化"硬编码为 `environmental_event`。** 判据必须是对一个**明确声明的**跨通道一致性特征的阈值/规则，且该阈值来自校准分集，不得写成 `if (n_channels_changed >= 2) return EVENT;`。
3. **必须逐 family 报告。** 汇总数字不得单独出现。

### 3.1 为什么必须收窄 B2 的 claim（含一个可推翻它的反例）

原假设"单通道变化 = 故障，多通道同时变化 = 环境事件"有两个独立的致命问题：

- **反向反例**：真实环境事件**可能只影响一个物理量**。光照事件主要影响 BH1750；湿度事件在 SHT30 上主要影响 humidity；温度事件主要影响 temperature。**单通道真实事件是常态，不是例外。**
- **正向反例（必须作为对抗性 fixture）**：**I²C 总线级故障会同时影响 SHT30 与 BH1750 两条通道。** 于是"多通道同时变化"完全可能由**一个总线级故障**造成，而不是环境事件。这直接推翻该假设。

因此 B2 的正确表述是：**within-node cross-channel context 是否提供了超出单通道 SensorTrust 的额外判别信息**——这是一个**可证伪的、允许答案为"否"的量化问题**，不是一个判决器。

### 3.2 B2 的两个强制测试

| 测试 | 内容 |
|---|---|
| **来源隔离测试** | 在离线 harness 中，把邻节点数据通道**注入为不可用**（抛错），B2 必须仍能运行并给出与"邻节点存在"时**完全一致**的输出。这从机制上排除偷看 |
| **对抗性 fixture** | ① 只影响单通道的真实事件（呼气 → humidity）——**不得**被判为 fault；② 同时影响两通道的**总线级故障**——**不得**被判为 environment event；③ 多通道同向的真实事件——可以判为 event；④ 单通道自激漂移——应判为 fault |

四个 fixture 必须全部通过，否则 B2 的 claim 不成立。

---

## 4. Final Sampling Baselines（采样侧）

| ID | 名称 | 来源 | 状态 |
|---|---|---|---|
| **S0–S4** | Fixed-5s / 10s / 20s / 40s / 60s | AdaptiveSense | 现成 |
| **S5** | AdaptiveSense（纯变化率） | AdaptiveSense | 现成。SQ1 的直接对照 |
| **S6** | Trust-aware adaptive（= EdgeSense sampling） | 新建 | SQ1 的待验方法。**只消费节点侧 trust，不含跨节点证据** |

**消融关系**：`S0–S4 ⊂ S5 ⊂ S6`。

**Primary RQ 阶段的采样设定（关键，见第 8 节）**：EXP-004/005/006 **不启用** S5/S6，两节点统一跑**固定密集参考 cadence（5 s）**，把自适应采样隔离为主 RQ 的混淆变量之外。

**S6 的一处强制约束**：S6 的输入只允许是节点侧量（`node_trust` + `change_score` + 本节点有效性）。**跨节点证据不得进入采样决策**——采样闭环必须在节点本地完成（Phase 0.1 §4.3）。

---

## 5. Corrected EXP-003

### 5.1 原设计错在哪

原 EXP-003 用 EXP-002 的 **fault injection set** 作为数据集，却报告 `fault→event` 与 `event→fault` 两个误判率。

- fault-only 数据集里**没有任何 event 样本**；
- 因此 `event→fault` 的分母为零，**该指标无真值支撑**。

这是一个**指标与其真值类别不匹配**的硬错误，不是表述问题。

### 5.2 修正后的 EXP-003

| 项 | 内容 |
|---|---|
| **目的** | 量化 within-node cross-channel context 相对单通道 SensorTrust 是否提供**额外判别信息** |
| **对照** | **B2 vs B1** |
| **硬件** | **单节点**（不需要第二套传感器；这是 B2 的定义所决定的） |
| **采样** | 固定密集 5 s（与 Primary RQ 一致） |
| **fault 样本来源** | **受控 sensor fault injection**，`ground_truth_source = injection_plan` |
| **event 样本来源** | **真实物理环境干预**（只作用于该节点的单通道），`ground_truth_source = physical_intervention_log` |
| **event 样本的类别标签** | **`localized_event`** |
| **B2 可用信息** | **仅本节点跨通道信息**。禁止邻节点数据（由 §3.2 的来源隔离测试强制） |
| **该数据集能否作为 Primary RQ 的 shared-event 数据集** | ❌ **不能**。它是**单节点**数据集，其 event 类只能是 `localized_event`。**Primary RQ 的正式 shared-event 评估只在 EXP-005/006 完成** |
| **阶段** | Phase 5 |

### 5.3 EXP-003 的指标与允许结果

**指标**（全部来自 `analyze_experiment.py`）
- `fault→localized_event` 与 `localized_event→fault` 误判率（绝对值、绝对差、相对差、置信区间）
- **逐个 fault family × 逐个 event family 的分格结果**（这是本实验的核心产出）
- 逐类 precision / recall / F1；`normal` 段的误报率（带时长与样本数）
- 数据构成表：每个真值类别各有多少 episode、各自来源与标注方式

**允许的结果（三种都必须被接受并如实报告）**
1. B2 有改善；
2. **B2 无改善**（跨通道 context 没有额外判别力）；
3. **B2 只对某些 fault / event family 有改善**（例如对 SHT30 的 T–H 物理耦合有效，对纯光照事件无效）。

**预期边界（必须事前写明，避免事后辩解）**：BH1750 的光照通道几乎没有跨通道耦合关系，因此**预期 B2 对光照类事件帮助有限**。若结果确实如此，它是一个**已知边界的确认**，不是失败。

### 5.4 数据集卫生（强制）

| 规则 | 说明 |
|---|---|
| **fault 与 event 干预分场次执行** | 绝不在同一时段同时注入故障又施加物理干预，避免真值标签重叠 |
| **真值与检测器解耦** | 生成真值时不查检测器输出（继承 SensorTrust 的做法） |
| **EXP-003 与 EXP-005 数据集按 ID 分离** | 两者都用物理干预，但一个是单节点 `localized_event`、一个是双节点 `shared_event`。**不得合并、不得互相充当对方的评估集** |
| **每个报告指标都必须有对应真值类别的数据** | 这是本轮新增的硬门禁：`analyze_experiment.py` 在指标分母为 0 时**必须报错退出**，而不是输出 `nan` 或静默跳过 |

最后一条直接防住 F1 这类错误在未来重演。

---

## 6. Final EXP-000–010 Matrix

| ID | 目的 | 真值类别 | 对照 | 硬件 | 干预 / 注入 | 采样 | 阶段 | 进 Primary RQ? | 主要指标 |
|---|---|---|---|---|---|---|---|---|---|
| **EXP-000** | 框架自检：指标脚本在已知答案的 fixture 上算得对 | 全部（构造） | — | 无（离线） | 人工 fixture | — | 4 | 门禁 | 指标正确性；**分母为 0 时报错**的行为 |
| **EXP-001** | 真机干净基线 | `normal` | — | B（+C） | 无 | 固定 5 s | 4 | ✅ | 误报/小时（**带时长与样本数**）、采样数、字节数 |
| **EXP-002** | 单通道故障检测（B1） | `sensor_fault` | B1 | 单节点 | 9 类 sensor_fault 注入 + 物理拔插 | 固定 5 s | 3–4 | ✅（B1 臂） | 逐 family precision/recall/F1、逐 episode 延迟；**B1 覆盖/不覆盖两组分开报告**（§2.4） |
| **EXP-003** | **跨通道 context（B2 vs B1）** | `sensor_fault` + **`localized_event`** + `normal` | B2 vs B1 | **单节点** | 受控注入 + 真实单通道物理干预（**分场次**） | 固定 5 s | 5 | ❌（但其 `sensor_fault` 臂为 B1/B2 提供 Primary RQ 的判别力基线） | 两向误判率 + **fault family × event family 分格**；允许"无改善" |
| **EXP-004** | **时间对齐测量** | — | — | B + C + A | 无（长时运行 ≥ 2 h，含 1 次节点重启 + 1 次网关重启） | 固定 5 s | 6（**第一步**） | ✅（前置） | offset / jitter / drift 实测值；`pairing_status` 分布 |
| **EXP-005** | **判别能力演示** | `shared_event` + `sensor_fault` | — | B + C + A 同房间 30–50 cm | ① 整区开/关灯、区域湿度整体变化；② 只影响 B 或只影响 C 的故障 | 固定 5 s | 6（**第二步**） | ✅ | 两个 case 的一致性；真值 vs 判定对照 |
| **EXP-006** | **Primary RQ 对比** | `shared_event` + `sensor_fault` + `normal` | **B3 vs B1 / B2** | **B + C + A（必须重新执行）** | **复用** EXP-002 的 fault family 定义 / injection plan / injection 参数 / 真值语义 + EXP-005 的 shared_event 干预方案；**不得复用 EXP-002 的 raw dataset**（见 §6.1） | 固定 5 s | 6 | ✅ **主实验** | `fault→event` rate、`event→fault` rate、**`shared-event recall`**、absolute delta、relative delta、confidence interval、per-scenario（§1.1） |
| **EXP-007** | **Trust-aware adaptive sampling** | `shared_event` + `sensor_fault` + `normal` | **S6 vs S5 vs S0–S4** | B（+C） | 复用 EXP-006 数据集 | **自适应（非同步）** | 7 | ❌ Secondary RQ | 误报率、召回、采样数、字节数（**标注为应用层指标**）+ **pairing coverage 率**（见第 8 节） |
| **EXP-008** | **Engineering Architecture Evaluation（E1）** | — | 网关侧判定 vs 后端侧判定 | B + C + A + 主机 | 无 | 固定 5 s | 11 | ❌ **不是** RQ 评估 | 见 §10 E1 的 7 项 |
| **EXP-009** | **`localized_event` 压力测试**（secondary） | `localized_event` | 同 B3 | B + C + A | 只对 B 呼气、只遮挡 B | 固定 5 s | 11 | ❌ **明确不计入 Primary RQ** | **预期可能失败**。结果单列，只写 limitations |
| **EXP-010** | **Network fault 战役（E2）** | `network_fault` | — | B + C + A | 应用层注入丢包/重复/CRC/陈旧/延迟/临时中断 + 物理条件变化 | 固定 5 s | 2 | ❌ 不进 `sensor_fault` 指标 | 包计数、序号缺口、重复、CRC、到达间隔/jitter；**严格区分注入丢包与物理丢包** |

### 6.1 数据集复用规则（强制）

| | 内容 |
|---|---|
| **✅ 允许复用** | fault family 定义 · injection plan · injection 参数（幅度/时长/速率）· **真值语义**（truth 的生成规则与标注口径）· 物理干预的**操作方案**（怎么做遮光/呼气/整区开关灯） |
| **❌ 禁止复用** | **任何 EXP 的 raw dataset**。尤其禁止把 EXP-002 的**单节点** raw 直接当作 EXP-006 的输入 |

**理由**：EXP-002 是**单节点**数据集，其 raw 里**不存在第二个节点的观测**。把它当作 EXP-006 的输入，跨节点判定就没有对应的真值数据支撑——这正是 F1 那类"指标与真值类别不匹配"的错误，只是换了一个位置。

**EXP-006 必须重新执行并生成属于它自己的产物**：

```
experiments/EXP-006/
├── manifest.json      # 本次运行的 fw_commit / config_sha / n / pairing_config / hardware(B,C,A)
├── truth/episodes.csv # 本次运行的 truth_category 记录
├── raw/               # 本次运行 B、C、A 三方的原始日志（带 SHA）
└── results/           # 本次运行的指标
```

manifest 里的 `fw_commit`、`config_sha`、`hardware`、`n`、`pairing_config` **必须是本次运行的值**，不得从 EXP-002 复制。

**关于 EXP-007 的复用**：EXP-007 是**采样策略的离线回放**评估，**可以**复用 EXP-006 的数据集作为参考流（与 AdaptiveSense 的 offline replay 方法论一致）。但若 EXP-007 同时在真机上运行，则必须生成属于它自己的 `raw/`。这一点必须在 `manifest.json` 的 `input_mode` 字段里写明（`offline_replay` 或 `on_device`）。



### 6.2 每个 EXP 的目录契约（强制）

```
experiments/EXP-<id>/
├── manifest.json          # exp_id, date, fw_commit, config_sha, hardware, sampling, n, pairing_config,
│                          # fault_taxonomy_version, input_mode, expected_event, status
├── truth/episodes.csv     # ★ 真值唯一来源，运行系统不可读（见 §2.5）
│                          # episode_id, truth_category, event_source, family, channel(s),
│                          # start_ts, end_ts, intensity,
│                          # injection_method | intervention_description,
│                          # physical_or_injected, ground_truth_source, notes
├── raw/                   # 原始串口日志（不可变、带 SHA）
└── results/               # analyze_experiment.py 的输出（不得手写）
```

**`truth/episodes.csv` 是"每个报告指标都有对应真值类别数据"的落地机制。** 没有它，EXP 不算完成。

### 6.3 样本量 n（仍不冻结任意数字）

n 在 `docs/research/experiment_design.md` 里、**采集测试数据之前**确定，依据为 pilot 方差估计（精度目标）或功效计算（检验能力）。两者都做不到时，**明确写"探索性实验，不做确证性声明"**。

---

## 7. Final Time Alignment / Pairing Algorithm

### 7.1 为什么原设计不成立（F3）

原设计用 `(session_id, window_index)` 配对，并隐含"两节点序号相同 = 同一时间窗"。

**这在这些条件下破裂**：
- 两节点自适应采样 interval 不同（例如 B 在 5 s、C 在 20 s）；
- 任一节点发生采样档位切换，导致 seq 增速改变；
- 节点在窗口内产生 0 个或多个样本。

**结论**：**node sequence number 或 node-local sample index 不能作为跨节点时间窗口的判定依据。** `B.seq == C.seq` 被明令禁止用作同步依据。

### 7.2 冻结的配对流水线

```
 1. Node monotonic timestamp         节点本地 esp_timer，样本带 node_ts_ms
        │
 2. Gateway time mapping             node_ts_ms → gateway timeline
        │                            （v1：offset + tolerance，不做漂移拟合）
        │
 3. Gateway-defined fusion window    窗口由 GATEWAY 定义，不由节点定义
        │                            W_k = [T_k, T_k + W)（tumbling，锚在 gateway epoch）
        │
 4. Nearest valid sample matching    在每个节点的候选样本中取"最接近窗口中心"的一个
        │
 5. Freshness / tolerance check      age = |窗口中心 − 该样本映射后的时间| ≤ max_sample_age
        │
 6. Cross-node fusion                只有两侧都通过 freshness 检查才融合
```

### 7.3 硬规则

| # | 规则 |
|---|---|
| **P1** | **fusion window 由 Gateway 定义。** 节点不知道窗口边界，也不需要知道 |
| **P2** | **Node seq 只用于本节点去重与缺包检测。** 不得用于跨节点配对 |
| **P3** | **禁止把 `B.seq == C.seq` 当作时间同步依据** |
| **P4** | **必须定义 `max_sample_age`**，并在 `manifest.json` 的 `pairing_config` 里记录其值 |
| **P5** | **禁止用陈旧邻节点样本硬做 fusion。** 不满足 freshness 就明确降级，不得"为了凑一对"而放宽窗口 |
| **P6** | 窗口内某节点样本数 > 1 时取**最接近窗口中心**者；被丢弃的样本数必须记录 |

### 7.4 配对状态机（三态，必须显式）

| 状态 | 条件 | 行为 |
|---|---|---|
| **`paired`** | 两节点都有 age ≤ `max_sample_age` 的样本，且两侧对齐质量达标 | 执行 cross-node fusion，产出 `{normal\|sensor_fault\|environmental_event}` |
| **`partial_evidence`** | 只有一侧有合格样本（另一侧窗口内无样本，或样本全部超龄） | **不做 cross-node fusion**。输出降级为"仅节点侧证据"，并显式标注 `decision_source = node_only` |
| **`insufficient_alignment`** | 对齐质量不达标（offset/jitter 超 tolerance，或 drift 未被界定），或两侧都无合格样本 | **不做判定**。记录原因，不产出 `anomaly_events` 行 |

**三态都要落库、都要可计数。** `pairing_status` 的分布本身就是 EXP-004/006/007 的一等指标。

### 7.5 配对的可能性不变量（把 F3 变成可检查的约束）

> **不变量 P0**：某窗口内要能形成 `paired`，每个参与节点必须至少有一个样本满足 `age ≤ max_sample_age`。
> **充分条件**：`节点采样 interval ≤ 2 × max_sample_age`。

**这个不变量的直接后果**（第 8 节会用到）：
- interval 越长，越容易在窗口内落到 0 个样本 → 降级为 `partial_evidence`；
- 当某节点的 adaptive interval 超过 `2 × max_sample_age` 时，**该节点的跨节点融合在结构上不再可能**，这不是 bug，而是设计约束的必然结果，**必须作为 adaptive sampling 的代价被报告**。

### 7.6 时间对齐的升级条件（不变，来自 Phase 0.1）

只有当 EXP-004 实测表明"offset + jitter 在目标窗口 W 内不足以支持配对"时，才引入线性漂移拟合。**在此之前不把拟合写进架构。**

---

## 8. Behavior Under Adaptive Sampling

### 8.1 隔离混淆变量（Primary RQ 阶段）

**EXP-004 / EXP-005 / EXP-006 期间，两节点统一运行固定密集参考 cadence（5 s）。**

理由：Primary RQ 研究的是**判别能力**（cross-channel + cross-node evidence 能否区分 fault 与 shared event）。若此时两节点各自跑自适应采样，则：

- 采样密度本身成为混淆变量（稀疏采样会降低检出率，与"证据是否有效"混在一起）；
- 配对成功率会随 cadence 漂移而变化，进一步污染指标。

固定密集参考采样把"判别"与"采样"两个问题**解耦**，使 Primary RQ 的结论干净。这与 `adaptive-lora-iot/EXPERIMENTS.md` 已写下的"5 秒流是密集参考，不是部署间隔"是同一个方法论。

### 8.2 非同步条件下的配对验证（Secondary RQ 阶段）

**仅在 EXP-007 验证**：两节点在自适应 / 异步 cadence 下的配对是否仍然正常。

EXP-007 必须同时报告三类量，缺一不可：

| 量 | 含义 |
|---|---|
| **判别指标** | 误报率、召回、采样数、字节数（沿用 EXP-006 的真值集） |
| **`pairing coverage` 率** | `paired` 窗口数 / 总窗口数。**这是 adaptive sampling 的新增代价，必须显式报告** |
| **配对状态分布** | `paired` / `partial_evidence` / `insufficient_alignment` 三者占比，以及降级原因分解（无样本 vs 超龄 vs 对齐不达标） |

**关键结论的诚实写法**：如果自适应采样把某节点的 interval 拉长到超过 `2 × max_sample_age`，跨节点融合的覆盖率会下降。这**不是实现缺陷，而是"省电/省通信"与"跨节点可判别性"之间的真实取舍**。它必须作为一个**量化发现**报告，而不是被隐藏或被当成 bug 修掉。

### 8.3 采样档位与配对兼容性表（供 EXP-007 事前声明）

| 节点 interval | 相对 `max_sample_age` | 预期配对状态 | 说明 |
|---|---|---|---|
| 5 s | « `max_sample_age` | `paired` | 密集参考（Primary RQ 的工作点） |
| interval ≤ 2 × `max_sample_age` | 满足 P0 | `paired` 可达 | **跨节点融合的设计工作区** |
| interval > 2 × `max_sample_age` | 违反 P0 | 结构性 `partial_evidence` | **跨节点融合不可能，必须如实报告为采样代价** |

`max_sample_age` 的具体数值由 EXP-004 的实测 offset/jitter 与窗口 W 共同决定，**不在本文件里冻结一个拍脑袋的数字**。

---

## 9. Final Research Questions

**EdgeSense v1 的最终研究问题，到此冻结，不再增加。**

| 类型 | 编号 | 问题 |
|---|---|---|
| **Primary** | RQ1 | 跨通道 + 跨节点证据能否把**单节点传感器故障**与**空间共享环境事件**区分开？（核心指标：fault→event 误判率） |
| **Secondary** | RQ2 | **Trust-aware adaptive sampling** 能否在不降低空间共享事件召回的前提下减少误报与资源消耗？ |

**已从研究问题中移除的项目（本轮修订）**

| 原编号 | 原内容 | 新定位 | 理由 |
|---|---|---|---|
| ~~SQ2~~ | 边缘 vs 后端的代价与检出延迟 | **Engineering Evaluation E1** | 网关侧与后端侧判定**都发生在 LoRa 数据已到达网关之后**，差异不在 LoRa。它不是判别能力问题，是资源与韧性放置问题（F4） |
| ~~RQ4~~ | 采样频率/开销/延迟/召回的 trade-off | **评测框架** | 是手段不是问题 |

**禁止再写的表述（F4 的直接后果）**

> ❌ "Gateway-side decision avoids LoRa latency"
> ❌ "网关侧判定避开了 LoRa 往返延迟"
> ❌ 任何暗示"网关侧相对后端侧的优势来自不经 LoRa"的说法

**正确的表述**：网关侧与后端侧判定的差异来自 **serial uplink · backend processing · resource placement · backend availability** 四项，**与是否经过 LoRa 无关**（两者都已在 LoRa 之后）。

---

## 10. Engineering Evaluations

**这一节的所有内容都是 Engineering / FDE portfolio layer，不是论文贡献。** 但它们必须被真实测量，因为它们支撑"这个人能把系统搭起来并说清代价"。

### E1 —— 网关侧融合 vs 纯后端融合（EXP-008）

> **What are the resource, latency, and resilience costs of gateway-side fusion compared with backend-only fusion?**

| 测量项 | 说明 |
|---|---|
| gateway flash increment | 编译后 ELF 段差值（继承 TinyEdgeBench 的方法与其"含脚手架代码"的限定） |
| gateway RAM | 静态 + 运行时峰值；未测则写 `not measured` |
| fusion computation latency | 网关侧单次融合计算耗时 |
| Gateway→Backend additional latency | 串口上行引入的额外延迟 |
| backend processing latency | 后端处理耗时 |
| total decision latency | 端到端（事件发生 → 判定可用）的分布：中位数 + min–max + p95 |
| behavior when Backend is unavailable | 后端不可用时的行为（网关侧判定是否仍可用、可维持多久、数据是否排队） |

**明确定位**：EXP-008 **是** Engineering Architecture Evaluation，**不是** Research Question evaluation。它服务 FDE portfolio 与工程设计的正当性论证。

### E2 —— Transport reliability（EXP-010）

链路投递的观测：包计数、序号缺口、重复、CRC、到达间隔/jitter。**严格区分应用层注入丢包与物理丢包。** 只写"本次摆放与配置下的观测结果"。

### E3 —— 资源代价

flash / RAM / 单次判定延迟。方法继承 TinyEdgeBench。**未测写 `not measured`，不写估计值。**

### E4 —— 延迟刻画

节点侧、网关侧、后端三段延迟分别刻画。

### E5 —— 可观测性

节点心跳、丢包、API 错误、DB 错误、设备错误、工具调用、AI 延迟、后端延迟。**v1 不引入 Prometheus/Grafana。**

### E6 —— AI 诊断（Copilot）

**不是研究贡献。** 评估的是：工具表是否只读、回答中的每个数值是否可追溯到 `tool_calls`、证据是否带 node/timestamp/metric 引用、后端不可用时是否明确说"证据不足"。

### E7 —— 部署与可复现

从零 clone 到跑通离线评测；`preflight.py` 能检出所有缺失依赖；docker-compose 单容器起 PostgreSQL + backend。

---

## 11. Final Hardware Requirements

### 11.1 已批准清单（B1 决策生效）

| 器件 | 数量 | 用途 | 说明 |
|---|---|---|---|
| ESP32-S3（N16R8） | **3** | A / B / C | 已有 |
| E220-400T22D | **3** | 星型链路 | 已有 |
| SHT30（0x44） | **2** | B、C 各一 | **第二颗已批准购置**（约 ¥30–60） |
| BH1750 / GY-302（0x23） | **2** | B、C 各一 | **第二颗已批准购置** |
| 电容式土壤湿度 | 1 | 可选，仅挂一个节点 | **不作为 spatially shared event 的核心研究通道**——其空间局部性太强，两个相距 30–50 cm 的探头不会给出可比读数 |
| 0.96" OLED (SSD1306) | 1 | 可选状态显示 | **不进入任何 Exit Criteria** |

### 11.2 明确不采购 / 不使用

- 第二个 soil moisture（B1 决策明确暂不要求）
- 第二个 OLED
- RTC 模块（v1 用节点单调时钟，不需要墙上时间）
- 任何执行器 / 继电器 / 水泵

### 11.3 角色（冻结）

| 角色 | 硬件 | 传感器 | 职责 |
|---|---|---|---|
| **Gateway A** | ESP32-S3 + E220 | **无** | 会话管理 · beacon · 时间对齐 · **跨节点融合** · **最终判定** · 证据聚合 · 链路质量 · 串口上行 |
| **Sensor Node B** | ESP32-S3 + E220 | SHT30 + BH1750 | 节点侧闭环（见 §12） |
| **Sensor Node C** | ESP32-S3 + E220 | SHT30 + BH1750 | 同 B，**同一固件镜像**，差异只在 `node_id` |

### 11.4 部署与接线（冻结）

- **B 与 C 同房间、相隔 30–50 cm**，测量同一组物理量，互为冗余见证。**这是 Primary RQ 成立的前提**（B3 决策）。
- 引脚（三个既有项目已一致，Phase 1 需在真机上逐项复核后才可标注"已验证"）：

| 信号 | 引脚 / 值 |
|---|---|
| I²C SDA / SCL | GPIO8 / GPIO9 |
| SHT30 地址 | 0x44 |
| BH1750 地址 | 0x23 |
| 土壤 ADC | GPIO1（可选） |
| E220 M0 / M1 / AUX | GPIO13 / 14 / 15 |
| E220 TXD / RXD（相对 ESP） | GPIO17 / 16 |
| E220 UART / 格式 | UART1，9600 8N1 |
| E220 地址 / REG0 / 信道 | 0x0000 / 0x62 / 0x17 |

### 11.5 表述限制（B4 决策生效）

> 当前实验**只**证明当前硬件与实验配置下的**实验室结果**。
> **禁止声称**："适合日本部署" / "符合日本无线法规" / "可直接在日本使用"。
> 除非后续专门完成法规与频段验证（400 MHz 段在日本的相关规范），否则这些表述不得出现在任何材料中。

---

## 12. Frozen Architecture

**本节即 EdgeSense v1 的冻结架构。** 内容与 Phase 0.1 一致（Phase 0.2 未改分层，只改了配对机制、类别体系与 B2 的 claim），此处完整重述以作为唯一权威版本。

### 12.1 分层与职责

| 层 | 位置 | 职责 | 禁止 |
|---|---|---|---|
| Sensing | Node B/C | 传感器读取、**逐通道** validity、I²C 错误上报 | 不做判定 |
| Reliability (node-local) | Node B/C | SensorTrust 单通道 + **跨通道 context** → `node_trust` | **不做跨节点融合** |
| Sampling | Node B/C | 自适应采样（只消费本节点量） | **不依赖邻节点状态**；不直接操作寄存器 |
| Transport | B/C + A | CRC 帧、序号、去重、重传、心跳、`session_id` | 不做业务语义 |
| **Reliability (cross-node)** | **Node A** | 时间对齐 · 窗口配对 · **跨节点融合** · **最终判定** · 证据聚合 | 不做传感器级故障检测本身 |
| Gateway (session) | Node A | 会话管理 · beacon · 心跳 · 超时 · 链路质量 · 串口上行 | 不缓存原始样本超过 N 分钟 |
| Backend | 主机 | 校验 · 持久化 · 只读 API · 可观测性 | **不做重新判定**；不提供写 API |
| Copilot | 主机 | 只读工具 + 证据组装 + 解释 | 不得生成工具结果以外的数值；不得展示 CoT |
| Dashboard | 浏览器 | 状态 · 曲线 · 事件时间线 · 诊断 | 无写操作 |

### 12.2 节点侧闭环（不依赖邻节点）

```
sensor acquisition → per-channel validity → SensorTrust
→ cross-channel context (B2) → local change detection
→ adaptive sampling → LoRa transport
输出：{ node_trust, change_score, reason_codes }
```

### 12.3 网关侧

```
receive → CRC/dedup/seq-gap → session mgmt → timestamp mapping
→ fusion window → nearest-sample + freshness → cross-node fusion
→ final decision {normal | sensor_fault | environmental_event}
→ evidence aggregation → link quality → serial uplink
```

### 12.4 冻结的显式非目标

Mesh · 多网关接管 · 任何执行器或控制路径 · 云侧推理 · Gateway→Node 邻节点数据广播 · Wi-Fi 传感器节点 · 深度学习异常检测 · Kafka/ClickHouse/K8s/Elasticsearch · Prometheus+Grafana · RTC。

### 12.5 数据模型（在 Phase 0.1 基础上按 F5 / F3 修订）

| 表 | 关键字段 | 本轮变化 |
|---|---|---|
| `nodes` | node_id, role, fw_commit, first_seen, last_seen, status | — |
| `sensor_readings` | exp_id, node_id, channel, value, valid, node_ts_ms | 加 `exp_id` |
| `trust_scores` | exp_id, node_id, channel, health_score, state, fault_flags, reason_codes, ts | 加 `exp_id` |
| `anomaly_events` | exp_id, node_id, window_index, **`decision_label`**, **`decision_source`**, confidence, reason_codes, evidence_json, ts | **Amendment 3**：`event_category` → **`decision_label`**（`normal`/`sensor_fault`/`shared_event`）。**本表不得含任何真值字段** |
| `network_metrics` | exp_id, node_id, session_id, seq, seq_gap_count, duplicates, crc_errors, inter_arrival_ms, jitter_ms, rssi(NULL) | 保持 Phase 0.1 版本；**无 SNR** |
| `session_alignment` | exp_id, session_id, node_id, offset_ms, jitter_ms, drift_ppm, residual_max_ms, quality, ts | 只承载**时钟质量** |
| **`fusion_windows`** | exp_id, session_id, window_index, window_start, window_end, node_a_ts, node_a_age_ms, node_b_ts, node_b_age_ms, dropped_sample_count, **pairing_status**, degraded_reason | **本轮新增**（F3 要求），承担窗口配对与 pairing_status |
| `device_events` | exp_id, node_id, **`subsystem`**, event_type, detail, ts | **Amendment 3**：只记录**系统观测到的症状**。删去 `event_category` 与 `physical_or_injected`（后者属真值侧） |
| `experiments` | exp_id, date, fw_commit, config_sha, hardware, sampling, n, pairing_config, **fault_taxonomy_version**, **ground_truth_source**, expected_event, status | 加 taxonomy 版本与真值来源 |
| `agent_queries` / `tool_calls` | 同 Phase 0 | — |

**真值不存两张表**：真值只存在于每个 EXP 的 `truth/episodes.csv`；评估用的联合表由 `analyze_experiment.py` 派生，避免出现两个真值来源。

---

## 13. Frozen Repository Integration Decisions

### 13.1 B2 决策的落地细则（`adaptive-lora-iot`）

| 步骤 | 内容 |
|---|---|
| 1 | **Freeze 原始版本**：在迁入之前，记录它的 source commit / 文件 SHA / 本地路径 |
| 2 | **不先创建独立公开 GitHub repo**（明确批准） |
| 3 | **保留原始真实实验数据与 provenance**：150 个真实样本 + 6 次物理事件标注 + 原始串口日志**原样保留**，并记录其采集时点与条件 |
| 4 | **选择性迁入 EdgeSense**：节点固件、采集/标注流程、接线文档、REPRODUCIBILITY 规则进 EdgeSense |
| 5 | **不修改历史实验结果**：迁入的是代码与数据结构，**不是**改写过的数字。任何重新分析必须作为**新的 EXP** 呈现，与原结果并列，不得覆盖 |

**迁入时的必修项**（与本轮修订无关，是 Phase 0 已识别的既有缺陷）：
- 逐通道 validity（原为整帧级，导致恒为 4095 的死土壤通道被上报为 valid）；
- 其 `analysis/` 与 AdaptiveSense `simulator/` 的重叠按 Phase 0 裁决合并（以 AdaptiveSense 的 `simulator/` 为准）。

### 13.2 全仓库冻结裁决（不变）

| 仓库 | 裁决 | 本轮变化 |
|---|---|---|
| **SensorTrust** | **GO** — vendor `core/sensor_trust.c/.h` + `fault_injector.c`；语义不改 | 无 |
| **AdaptiveSense** | **GO** — vendor 策略源文件 + `simulator/`；Wi-Fi/MQTT 通信层 DROP | 无 |
| **adaptive-lora-iot** | **GO（整体迁入，系统基座）** | **B2 批准：不先建独立 repo**（§13.1） |
| **EventGuard-LoRa** | **REFACTOR** — 只取传输层 + 实验编排；其 RQ 与 importance 策略 DROP | 无 |
| **TinyEdgeBench** | **REFACTOR** — 保持独立 companion，不进运行时 | 无 |
| **lora-p2p** | **GO（归档为 P2P 可行性证据）** | 无 |
| **EdgeSense-Fusion** | **REFACTOR → 归档改名** — 搬后端/Dashboard 骨架；先 `git init` + 修 README 措辞 | schema 按 §12.5 更新 |
| **Smart-Agriculture-Edge-AI** | **DROP（仅取协议词汇）** | 无 |
| **EdgeFaultLab** | **DROP（暂缓）** | 无 |
| **esp32-agri-node** | **DROP（另一条线，只引用接线与校准）** | 无 |

### 13.3 vendor 纪律（冻结）

- 上游仓库保持独立；EdgeSense 以**固定 commit** 用 `git subtree` 引入（不用 submodule）；
- `scripts/check_vendored_hashes.py` 防止 vendor 目录被静默修改；
- `docs/vendored/<repo>.md` 记录：来源 URL、commit SHA、引入日期、引入范围、**不使用范围**、边界声明；
- **上游戏外代码内部一行不改。**

---

## 14. Phase 1 Entry Criteria

**Phase 1 只有在以下全部满足后才开始。**（Phase 1 本身的工作是"B/C 同镜像节点稳定运行"，不在本轮范围内。）

| # | 准入条件 | 判据（可检查） |
|---|---|---|
| **C1** | 两份前置文档已更新优先级声明 | `PHASE_0_DESIGN_REVIEW.md` 与 `PHASE_0_1_ARCHITECTURE_CORRECTION.md` 开头均写明 **0.2 ＞ 0.1 ＞ 0**，且本文件存在 |
| **C2** | 四项批准已归档 | B1–B4 的批准内容写进 `docs/engineering/design_decisions.md` |
| **C3** | **第二套 SHT30 + BH1750 已到位并在真机上验证** | 分别在 B 与 C 上执行 I²C 扫描，**两颗 SHT30（0x44）与两颗 BH1750（0x23）全部可寻址**；记录扫描输出 |
| **C4** | 接线单一权威来源已建立并在真机复核 | `docs/hardware/wiring.md` 存在，且 §11.4 的每一项都逐项在真机上确认过；未确认项明确标注 `unverified` |
| **C5** | 冻结架构与分类法已落文档 | §2 的 taxonomy、§7 的配对算法、§12 的架构写进 `docs/research/experiment_design.md` 与 `docs/engineering/architecture.md` |
| **C6** | `adaptive-lora-iot` 的 freeze 记录完成 | `docs/vendored/adaptive-lora-iot.md` 含 source commit / SHA / 路径 / 采集时点；原始数据未被改写 |
| **C7** | 环境自检可用 | `scripts/preflight.py` 能列出本机缺失的依赖（venv / pytest / PyYAML / IDF / 串口）而不失败退出 |
| **C8** | 真值标注流程已定义 | §6 的 `truth/episodes.csv` 字段表与填写规则已文档化；含"物理 vs 注入"的判定标准 |
| **C9** | **无任何运行代码在本轮被创建** | 本轮只产出文档；Phase 1 才写代码 |

**C3 是唯一的物理阻塞项**，也是最关键的一条：没有第二套同型传感器，Primary RQ 的跨节点部分在结构上无法做。

---

## 附录：Before → After（15 项冻结修订 + 3 项澄清 Amendment）

> 第 1–15 项是 Phase 0.2 的冻结修订；**第 16–18 项是冻结后追加的三项非架构级澄清（Amendment 1–3）**，不构成 Phase 0.3，不改变冻结架构。

| # | 项目 | Before（Phase 0 / 0.1） | After（Phase 0.2） |
|---|---|---|---|
| 1 | **EXP-003 数据集** | fault-only 数据集，却报告 fault→event 与 event→fault | fault 来自受控注入 + event 来自**真实物理干预**；event 类标签为 `localized_event`；**明确声明不是 Primary RQ 的 shared-event 数据集** |
| 2 | **指标分母门禁** | 无 | `analyze_experiment.py` 在真值类别分母为 0 时**必须报错退出** |
| 3 | **B2 的定位** | 隐含的通用 fault/event 判决器 | **"提供额外 contextual evidence"**；不是判决器 |
| 4 | **B2 允许的结果** | 隐含"应有改善" | ✅ 有改善 / ✅ **无改善** / ✅ **只对某些 family 有改善** |
| 5 | **多通道=事件 的硬编码** | 未禁止 | **明令禁止**；必须有 4 个对抗性 fixture（含 I²C 总线级故障同时影响两通道） |
| 6 | **B2 的邻节点隔离** | 未强制 | **来源隔离测试**强制：邻节点通道不可用时 B2 输出必须完全一致 |
| 7 | **跨节点配对机制** | `(session_id, window_index)` + 隐含 seq 相等 | **gateway 定义的 fusion window** + nearest-valid-sample + freshness 检查；**禁止 seq 相等作同步依据** |
| 8 | **配对状态** | 隐含"配对或失败" | 三态显式：`paired` / `partial_evidence` / `insufficient_alignment`，全部落库可计数 |
| 9 | **`max_sample_age`** | 未定义 | **必须定义**，并写入 `manifest.json` 的 `pairing_config` |
| 10 | **自适应采样与配对** | 未讨论两者冲突 | Primary RQ 用固定密集 5 s 隔离混淆；**配对可能性不变量 `interval ≤ 2 × max_sample_age`**；EXP-007 报告 **pairing coverage** 作为 adaptive sampling 的代价 |
| 11 | **SQ2** | 正式 Research Question | **删除**，降级为 **Engineering Evaluation E1**；明确其"不是 RQ 评估" |
| 12 | **LoRa 延迟的论据** | "Gateway-side decision avoids LoRa latency" | **禁止该表述**。差异只来自 serial uplink / backend processing / resource placement / **backend availability** |
| 13 | **Fault Taxonomy** | `sensor_fault` 混装 network + device 故障 | 正交拆分为 `sensor_fault` / `network_fault` / `device_unavailable`；**真值侧** `truth_category` / `event_source` / `ground_truth_source`，**运行侧** `decision_label` / `decision_source` / `subsystem` |
| 16 | **Amendment 1 · EXP-006 数据集** | "复用 EXP-002 注入集"（歧义：可能被读作复用其 raw） | **可复用**：fault family 定义 / injection plan / injection 参数 / 真值语义 + EXP-005 干预方案；**禁止复用任何 raw dataset**。EXP-006 必须在 B+C+A 上**重新执行**并生成自己的 raw / truth / manifest（§6.1） |
| 17 | **Amendment 2 · Primary RQ 措辞** | "…without **materially** reducing recall…" | 改为 "…and **what trade-off does it introduce in shared-event recall**?"；报告 7 项量（fault→event rate / event→fault rate / shared-event recall / absolute delta / relative delta / CI / per-scenario）；**不预设任意 "material" 阈值** |
| 18 | **Amendment 3 · 真值与决策分离** | `anomaly_events.event_category` 同时表达真值与判定；`truth/episodes.csv` 用 `category` | 真值侧 `truth_category`（只存在于 `truth/episodes.csv`，运行系统不可读）；运行侧 `anomaly_events.decision_label`；`device_events` 用 `subsystem` 且删去真值字段。唯一连接点是 `analyze_experiment.py` |
| 14 | **B1 的评估公平性** | 未区分 family | **B1 声明覆盖（5 类）与不覆盖（OFFSET/NOISE/I²C/disconnect）分两组报告**，后者不算 B1 失败 |
| 15 | **硬件与部署** | B1 为"阻塞项" | **B1 批准**：第二套 SHT30 + BH1750 购置；soil **不作为 shared-event 通道**；**B4 批准**：只声称实验室结果 |

---

## 本轮结束状态

- ✅ F1–F5 五个缺陷全部在设计层面修正。
- ✅ B1–B4 四项决策已批准并落到文档（blocker 全部解除）。
- ✅ 研究范围冻结：**1 Primary RQ + 1 Secondary RQ**，不再增加。
- ✅ **架构冻结**（§12）。此后除非真实实验数据证明某设计不可行，不得再做宏观架构重设计。
- ✅ 未创建任何运行代码，未开始 Phase 1，未修改任何已有仓库。

**下一步是满足 §14 的 9 条准入条件。C3（第二套传感器到位并真机验证）是唯一的物理阻塞项。**
