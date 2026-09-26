# Experiment Design — EdgeSense v1

> 方法论规范。**权威来源**：`docs/PHASE_0_2_FINAL_DESIGN_FREEZE.md`。与本文件冲突时以 Phase 0.2 为准。
> 本文件的作用是把冻结设计**落成可执行的实验协议**：谁标注、怎么标注、什么算数、什么时候必须报错。

---

## 1. 研究范围（只有两项）

| 类型 | 问题 |
|---|---|
| **Primary RQ** | 跨通道 + 跨节点证据能否把**单节点传感器故障**与**空间共享环境事件**区分开，相比单通道读数，它对 shared-event recall 引入了什么 trade-off？ |
| **Secondary RQ** | **Trust-aware adaptive sampling** 在减少误报与资源消耗的同时，对 shared-event recall 引入什么变化？ |

**报告清单（Primary RQ，缺一视为未完成）**：`fault→event` rate · `event→fault` rate · `shared-event recall` · absolute delta · relative delta · confidence interval · per-scenario results。

> **不预设任何"material"阈值。** 召回的变化以量呈现，不以"是否显著降低"判定。主假设**允许被否定**。

---

## 2. 事件 / 故障分类法

### 2.1 真值侧与运行侧严格分离

| 侧 | 字段 | 所在位置 | 运行系统可见？ |
|---|---|---|---|
| **真值侧** | `truth_category`、`event_source`、`ground_truth_source`、`physical_or_injected` | `experiments/EXP-xxx/truth/episodes.csv` | ❌ **不可见** |
| **运行侧** | `decision_label`、`decision_source`、`confidence`、`reason_codes`、`evidence_json` | `anomaly_events` 表 | ✅ 系统自身产出 |

**三个命名约束**
1. 真值侧字段以 `truth_` 前缀开头（或明确属真值记录属性）。
2. **运行侧不得出现 `event_category` 或 `truth_category`**——用 `decision_label`。
3. **运行侧不得出现 `physical_or_injected`**——它只属于真值记录。

**唯一连接点**：`analyze_experiment.py`。它读真值与决策，做 `truth_category` **vs** `decision_label` 的匹配。**该脚本不得被运行系统导入。**

### 2.2 `truth_category` 取值与归属

| `truth_category` | `event_source` | 包含 | `ground_truth_source` | 进哪个评估 |
|---|---|---|---|---|
| `sensor_fault` | `sensing` | RANGE · STUCK · SPIKE · DRIFT · OFFSET/BIAS · NOISE increase · missing reading · I²C 接口故障 · sensor disconnect | `injection_plan`（注入）/ `physical_intervention_log`（拔插、断电） | ✅ **Primary RQ** |
| `shared_event` | `environment` | 整区开/关灯；同一区域湿度整体变化；同一区域温度整体变化 | `physical_intervention_log`（同一时刻对**两个节点**生效） | ✅ **Primary RQ** |
| `localized_event` | `environment` | 只对某一节点呼气；只遮挡某一节点 BH1750 | `physical_intervention_log`（只针对**一个节点**） | ❌ 单列。作 EXP-003 的 event 类 + EXP-009 stress test + limitation |
| `network_fault` | `radio` | LoRa 丢包 · 重复 · CRC 失败 · 陈旧包 · 延迟 · 临时中断 | `firmware_diagnostic_counter` + `host_observation` | ❌ 进 **EXP-010** |
| `device_unavailable` | `power` / `firmware` | 节点断电 · 固件崩溃 · 节点重启 | `host_observation` + `firmware_diagnostic_counter` | ❌ availability / observability，**不进分类指标** |

`normal` 是**无事件窗口**的真值标记，不属于 `truth_category` 取值。

### 2.3 B1 的能力边界：必须分两组报告

| 组 | family | 报告方式 |
|---|---|---|
| B1 **声明覆盖** | RANGE · STUCK · SPIKE · DRIFT · MISSING | 正常算 B1 的 precision / recall / F1（**公平的主对照**） |
| B1 **声明不覆盖** | OFFSET/BIAS · NOISE increase · I²C 接口故障 · sensor disconnect | 记为 `out of B1's declared scope`，**不算 B1 的失败**。由逐通道 validity / supervisor 接管，单独报告 |

理由：SensorTrust 是一个语义已冻结的既有工作，且它自己的 README 明确声明了 OFFSET 是盲区。拿它从未声称能做的 family 算 recall 会污染 "B1 vs B3" 的归因。

---

## 3. `truth/episodes.csv` 字段规范

| 字段 | 必填 | 取值 / 格式 | 说明 |
|---|---|---|---|
| `episode_id` | ✅ | `EXP-006-E017` | 全局唯一，含 EXP 前缀 |
| `truth_category` | ✅ | 见 §2.2 五值 | **真值类别** |
| `event_source` | ✅ | `sensing` / `environment` / `radio` / `power` / `firmware` | 真实的产生层 |
| `family` | ✅ | `RANGE` / `STUCK` / … / `area_light_off` / `area_humidity_shift` … | 细粒度 family，**逐 family 报告的基础** |
| `channel(s)` | ✅ | `temperature` / `humidity` / `light` / `soil` / 多值用 `\|` | 受影响的通道 |
| `nodes` | ✅ | `B` / `C` / `B\|C` | 受影响节点。`shared_event` 必须为 `B\|C` |
| `start_ts` / `end_ts` | ✅ | ISO 8601，**gateway 时间基** | 事件区间 |
| `intensity` | ⬜ | 数值 + 单位 | 例如注入幅度、遮光时长、呼气距离 |
| `injection_method` | 条件 | 文本 | `physical_or_injected = injected` 时必填 |
| `intervention_description` | 条件 | 文本 | `physical_or_injected = physical` 时必填 |
| `physical_or_injected` | ✅ | `physical` / `injected` | **真值侧字段** |
| `ground_truth_source` | ✅ | `injection_plan` / `physical_intervention_log` / `firmware_diagnostic_counter` / `host_observation` | 真值从哪来 |
| `notes` | ⬜ | 文本 | 异常情况、偏离协议之处 |

### 3.1 填写规则（强制）

1. **真值与检测器解耦**：生成真值时**不得查检测器输出**（继承 SensorTrust 的做法）。
2. **真值不得进入运行系统**：数据库 schema 里没有 `truth_category` 列；固件不接收、不存储、不上报真值标签。
3. **注入器只做注入，不做标注**：注入器按**预先声明**的 injection plan 执行；它的调度表属实验元数据，**不是运行系统的判定输入**。节点上报的是**观测到的症状**（读失败、值越界、超时），不是"我注入了什么"。
4. **一个 episode 只能属于 physical 或 injected 之一。**

### 3.2 `physical` 与 `injected` 的判定标准（C8 要求）

| 判据 | `injected` | `physical` |
|---|---|---|
| **产生方式** | 由固件内注入器按 **injection plan** 产生 | 由真机外部行为产生（拔插、断电、遮挡、呼气、开关灯） |
| **真值来源** | `injection_plan`（**事前**声明：时间、通道、family、参数） | `physical_intervention_log`（干预**当下**记录） |
| **时间精度** | 固件时基（毫秒级） | 人工记录（**秒级或更粗**）。必须在 `notes` 里写明记录误差 |
| **可重复性** | 高（同 plan 可重放） | 低（每次实际操作都有差异），必须记录实际做法 |
| **必填字段** | `injection_method` + 参数 | `intervention_description` |

**重叠或不明确的处理**
- 若一次运行里注入与物理干预在时间上重叠 → **拆成两个 episode**（各自区间不重叠）；无法拆分时标 `notes` 为 `mixed` 并**排除出主指标**，单独报告。
- 若人工记录的干预时刻不确定（例如"大约 14:32"）→ 必须规定该类事件的**最小持续时长**，使真值窗口宽度大于记录误差；否则该 episode 不得进入主指标。

**报告要求**：所有指标**必须按 `physical` / `injected` 分组给出**。不得只给合并数字。

---

## 4. 时间对齐与配对算法

### 4.1 流水线

```
1. Node monotonic timestamp   节点本地 esp_timer，样本带 node_ts_ms（绝不用墙上时间）
2. Gateway time mapping       node_ts_ms → gateway timeline（v1：offset + tolerance，不做拟合）
3. Gateway-defined window     W_k = [T_k, T_k + W)，由 GATEWAY 定义，锚在 gateway epoch
4. Nearest valid sample       各节点取最接近窗口中心的合格样本
5. Freshness check            age = |窗口中心 − 样本映射时间| ≤ max_sample_age
6. Cross-node fusion          两侧都通过才融合
```

### 4.2 硬规则

| # | 规则 |
|---|---|
| **P1** | fusion window 由 **Gateway** 定义。节点不知道窗口边界 |
| **P2** | **Node seq 只用于本节点去重与缺包检测** |
| **P3** | **禁止 `B.seq == C.seq` 作为时间同步依据** |
| **P4** | **必须定义 `max_sample_age`**，并在 `manifest.json` 的 `pairing_config` 中记录 |
| **P5** | **禁止用陈旧邻节点样本硬做 fusion**——不满足 freshness 就明确降级，不得为"凑一对"放宽窗口 |
| **P6** | 窗口内某节点样本数 > 1 时取最接近窗口中心者；**被丢弃的样本数必须记录** |

### 4.3 配对三态（必须落库、必须可计数）

| 状态 | 条件 | 行为 |
|---|---|---|
| `paired` | 两节点都有 age ≤ `max_sample_age` 的样本，且对齐质量达标 | 执行跨节点融合，产出 `decision_label` |
| `partial_evidence` | 只有一侧有合格样本 | **不做跨节点融合**。降级为节点侧证据，`decision_source = node_only` |
| `insufficient_alignment` | 对齐质量不达标，或两侧都无合格样本 | **不做判定**，不产出 `anomaly_events` 行，记录原因 |

### 4.4 配对可能性不变量

> **P0**：某窗口要能形成 `paired`，每个参与节点必须至少有一个样本满足 `age ≤ max_sample_age`。
> **充分条件**：`节点采样 interval ≤ 2 × max_sample_age`。

**后果**：interval 越长越容易在窗口内落到 0 个样本 → 降级为 `partial_evidence`。当某节点的 adaptive interval 超过 `2 × max_sample_age` 时，**该节点的跨节点融合在结构上不再可能**。这**不是 bug**，必须作为 adaptive sampling 的代价报告（见 §5）。

**`max_sample_age` 的具体数值不预先拍定**，由 EXP-004 实测的 offset / jitter 与窗口 W 共同决定。

### 4.5 时间同步的升级条件

只有当 **EXP-004 实测**表明"offset + jitter 在目标窗口 W 内不足以支持配对"时，才引入线性漂移拟合（`gateway_time ≈ a·local_time + b`，并上报残差）。在此之前的架构里**不含拟合**。

---

## 5. 采样设定与混淆变量控制

| 阶段 | 采样设定 | 理由 |
|---|---|---|
| EXP-001 / 002 / 003 / 004 / 005 / 006 | **固定密集 5 s**，两节点一致 | 把"判别"与"采样"解耦。否则采样密度成为混淆变量，且配对成功率随 cadence 漂移而变 |
| EXP-007 | **自适应 / 异步** | 验证非同步条件下的配对。必须同时报告：① 判别指标；② **`pairing coverage` 率**；③ 配对状态分布与降级原因分解 |

**EXP-007 的诚实写法**：若自适应采样把 interval 拉长到超过 `2 × max_sample_age`，跨节点融合覆盖率会下降。这是**"省电/省通信"与"跨节点可判别性"之间的真实取舍**，必须作为量化发现报告，**不得隐藏、不得当成 bug 修掉**。

---

## 6. EXP 矩阵与数据集复用规则

| ID | 目的 | 真值类别 | 采样 | 进 Primary RQ |
|---|---|---|---|---|
| EXP-000 | 框架自检（离线 fixture） | 全部（构造） | — | 门禁 |
| EXP-001 | 真机干净基线 | `normal` | 固定 5 s | ✅ |
| EXP-002 | 单通道故障检测（B1） | `sensor_fault` | 固定 5 s | ✅（B1 臂） |
| EXP-003 | 跨通道 context（B2 vs B1） | `sensor_fault` + `localized_event` + `normal` | 固定 5 s | ❌（其 `sensor_fault` 臂提供判别力基线） |
| EXP-004 | 时间对齐测量（offset / jitter / drift） | — | 固定 5 s | ✅（前置） |
| EXP-005 | 判别能力演示 | `shared_event` + `sensor_fault` | 固定 5 s | ✅ |
| EXP-006 | **Primary RQ 对比（B3 vs B1 / B2）** | `shared_event` + `sensor_fault` + `normal` | 固定 5 s | ✅ **主实验** |
| EXP-007 | Trust-aware adaptive sampling | 同上 | **自适应** | ❌ Secondary RQ |
| EXP-008 | Engineering Architecture Evaluation（E1） | — | 固定 5 s | ❌ 不是 RQ |
| EXP-009 | `localized_event` 压力测试 | `localized_event` | 固定 5 s | ❌ **不计入** |
| EXP-010 | Network fault 战役（E2） | `network_fault` | 固定 5 s | ❌ 不进 `sensor_fault` 指标 |

### 6.1 数据集复用规则

| | 内容 |
|---|---|
| ✅ **允许复用** | fault family 定义 · injection plan · injection 参数 · **真值语义** · 物理干预的**操作方案** |
| ❌ **禁止复用** | **任何 EXP 的 raw dataset**。尤其禁止把 EXP-002 的**单节点** raw 当作 EXP-006 的输入 |

**理由**：EXP-002 是单节点数据集，其 raw 里**不存在第二个节点的观测**。直接复用会让跨节点判定没有对应的真值数据支撑——即"指标与真值类别不匹配"的错误。

**EXP-006 必须在 B + C + A 上重新执行**，并生成属于它自己的 `manifest.json` / `truth/` / `raw/` / `results/`。manifest 里的 `fw_commit`、`config_sha`、`hardware`、`n`、`pairing_config` **必须是本次运行的值**。

**EXP-007 的例外**：它是**离线回放**评估，可以复用 EXP-006 的数据集作为参考流（与 AdaptiveSense 的 offline replay 一致）。但若同时在真机上运行，必须生成自己的 `raw/`。这一点在 `manifest.json` 的 `input_mode` 字段写明（`offline_replay` / `on_device`）。

**EXP-003 与 EXP-005 的数据集按 ID 分离**：两者都用物理干预，但一个是单节点 `localized_event`、一个是双节点 `shared_event`。**不得合并、不得互相充当对方的评估集。**

---

## 7. 指标计算与门禁

所有指标由 `experiments/analyze_experiment.py` 从原始数据自动算出。**禁止手算后填入 README。**

**门禁（防重演"指标与真值不匹配"的错误）**

> 任一所要求的真值类别在评估集中出现 **0 次**时，脚本**必须报错退出**，而不是输出 `nan` 或静默跳过。

**输出契约**

```
输入：truth/episodes.csv（truth_category）+ results/decisions.*（decision_label）
输出：
  confusion matrix   truth_category × decision_label
  Primary RQ 指标     fault→event rate / event→fault rate / shared-event recall
                     absolute delta / relative delta / confidence interval / per-scenario
  分离报告            localized_event 单独一组；network_fault、device_unavailable 另计，不混入
  数据构成表          每个 truth_category 的 episode 数、来源、标注方式
```

**其他要求**
1. 聚合方式必须写明：micro（合并计数）还是 macro（先按场景再平均）。沿用 AdaptiveSense 的选择（micro）并在方法文档写明。
2. **逐场景结果必须与汇总结果同时给出**——micro 汇总会掩盖某一场景的完全失败。
3. 每个数字必须能追溯到：EXP id + 原始日志 SHA + 固件 commit + 配置哈希。
4. 通信类指标**必须标注为应用层指标**（继承 AdaptiveSense 的限定写法）。
5. 未测项写 `not measured`，**不写估计值**。

---

## 8. 样本量与预注册

- **`n` 不在冻结文档中拍定。** 它必须在本文件里、**采集测试数据之前**确定，依据为 pilot 的方差估计（精度目标）或功效计算（检验能力）。
- 两者都做不到时，**明确写"探索性实验，不做确证性声明"**。
- **统计检验须在实验设计阶段预先声明**（检验类型、α、配对方式、是否多重比较校正）。**禁止在看到结果之后再挑检验。**
- 每个 EXP 的 `manifest.json` 必须记录**实际**的 `n` 与未完成/失败的重跑次数。
- **选择段与测试段必须不相交**，评估规则在测试打分前固定（继承 `adaptive-lora-iot/EXPERIMENTS.md`）。

---

## 9. 每个 EXP 的目录契约

```
experiments/EXP-<id>/
├── manifest.json          # exp_id, date, fw_commit, config_sha, hardware, sampling, n,
│                          # pairing_config, fault_taxonomy_version, input_mode, expected_event, status
├── truth/episodes.csv     # ★ 真值唯一来源，运行系统不可读（字段见 §3）
├── raw/                   # 原始串口日志（不可变、带 SHA）
└── results/               # analyze_experiment.py 的输出（不得手写）
```

**没有 `truth/episodes.csv` 的 EXP 不算完成。**
