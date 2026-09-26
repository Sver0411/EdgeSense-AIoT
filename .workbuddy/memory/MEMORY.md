# EdgeSense — 项目长期约定

## 项目定位

**EdgeSense: Fault-Aware Adaptive Sensing and AI-Assisted Diagnosis for Resource-Constrained IoT Networks**

- 目标：真实硬件可运行 + 研究问题明确 + 实验可复现 + 系统工程完整。
- 双重用途：日本修士申请（研究能力）+ FDE 求职作品集（系统工程能力）。
- 不是课程设计，不是功能堆砌。

## 不可违反的硬约束

1. **Agent 是 Decision Support，不是 Autonomous Control。** 禁止任何自主重启设备、改固件、改数据库、
   控制执行器的能力。**架构上不留入口**（工具表白名单只读、API 只有 GET、写通路仅在网关 ingest 内部），
   靠契约测试强制，不靠约定。
2. **Copilot 回答中的每个数值必须来自 Tool Result**，并引用 node / timestamp / metric 或 event。
   禁止生成无法追溯的数据。禁止展示模型内部 Chain-of-Thought，只展示 action / tool / evidence / result。
3. **README 必须区分 Claim 与 Evidence。** 禁止 "high reliability" / "high accuracy" /
   "significant energy saving" / "robust system" 这类无数据支持的表述。未测就写 `Evaluation planned`
   或 `not measured`。所有性能数字必须由 `experiments/analyze_experiment.py` 自动算出，
   禁止手算后填入。理想做法：README 数字由脚本渲染生成（继承 SensorTrust 的 `render_readme.py`）。
4. **物理故障与软件注入故障必须严格区分**，数据层就分开（`datasets/real` vs `datasets/injected`），
   不能只在文档里区分。每条 `device_events` 必须带 `physical_or_injected` 标注。
5. **禁止把特定实验观测写成普适结论。** 例如 lora-p2p 的 602 包 0 丢包 → 只能写
   "No packet loss was observed during the tested 600.985-second point-to-point experiment."
6. **算法必须 deterministic 或 statistically defined，且可测试。** LLM 只负责把已算出的证据
   解释成人类可读的诊断，不参与判定。
7. **不为了"高级感"加依赖。** 已明确不做：Kafka / ClickHouse / K8s / Elasticsearch /
   Prometheus+Grafana（v1）/ 复杂微服务 / Wi-Fi 传感器节点 / Mesh / 深度学习异常检测。
8. **不擅自修改已验证的硬件接线。** SHT30 地址 0x44、SDA GPIO8、SCL GPIO9 已在三个项目中一致。

## 研究设计（已冻结，2026-09-27 Phase 0.2 最终冻结）

- **主 RQ**：跨通道 + 跨节点证据能否区分**【单个节点传感器故障】与【空间共享环境事件】**
  （核心指标 = fault→event 误判率）。
  - **只研究 `sensor_fault` vs `spatially shared environmental event`。**
  - `localized environmental event`（只对某一节点呼气、只遮挡某一节点光照）**排除出主 RQ 指标**——
    它在单节点视角下与 sensor_fault **原理上不可分辨**。只作 secondary stress test 与 limitation。
  - **不因此扩展成四分类系统**：判别主线仍是二分类 + normal 前置门限。
- **SQ1**：trust 信号反馈进采样策略，能否在不降低事件召回的前提下减少误报与通信量。
  **注意：跨节点证据不参与采样决策**，S6 只消费节点侧 trust（采样闭环必须在节点本地）。
- **SQ2 已删除（2026-09-27 Phase 0.2）**：降级为 **Engineering Evaluation E1**（网关侧 vs 后端侧的
  资源/延迟/韧性代价），**不是研究问题**。禁止写"gateway-side avoids LoRa latency"——
  两者都在 LoRa 数据到达 gateway **之后**，差异只来自 serial uplink / backend processing /
  resource placement / backend availability。
- **候选 RQ4 不是 RQ**，是评测框架。
- **最终范围：只有 1 Primary RQ + 1 Secondary RQ，不再增加。**
  AI Copilot / Dashboard / Backend **都不是研究贡献**，属 Engineering / FDE portfolio layer。
- **B2（跨通道）的 claim 已收窄**：只声称"提供额外 contextual evidence"，**不是**通用 fault/event 判决器。
  **禁止**把"多通道同时变化"硬编码为 environmental_event（反例：I²C 总线级故障会同时影响两条通道）。
  允许结果：有改善 / **无改善** / 只对某些 family 有改善。
- **指标门禁**：`analyze_experiment.py` 在真值类别分母为 0 时**必须报错退出**（防 EXP-003 那类错误重演）。
- **B1 的评估公平性**：B1 只声明覆盖 RANGE/STUCK/SPIKE/DRIFT/MISSING；
  OFFSET/NOISE increase/I²C 故障/sensor disconnect **不算 B1 失败**，记为 out of declared scope。
- **Baseline 阶梯（冻结）**：判别侧 `B0 原始阈值 ⊂ B1 SensorTrust 单通道 ⊂ B2 +跨通道 ⊂ B3 +跨节点`；
  采样侧 `S0–S4 定频 ⊂ S5 变化感知自适应 ⊂ S6 信任感知自适应`。
- **主 RQ 的核心 error type 是 fault→event 误判**。fault→event 与 event→fault 两个误判率**必须同时报告**，
  并附绝对差、相对差、置信区间、逐场景结果。
- **禁止冻结无依据的百分比门槛。** 主假设被否定是被允许、且必须如实报告的正常结局。
  统计检验须在实验设计阶段预先声明；样本量不足时写 `insufficient n for a confirmatory test`。

## 分层与职责（Phase 0.2 冻结，勿再改动）

- **Sensor Node B/C（节点侧闭环，不依赖邻节点）**：
  sensor acquisition → 逐通道 validity → SensorTrust → **跨通道融合** → 本地变化检测
  → 自适应采样 → LoRa transport。产出 `node_trust` + `change_score`。**不做跨节点融合。**
- **Gateway A（网关侧）**：会话管理 → beacon → 时间对齐 → **跨节点融合** → **最终判定**
  `{normal | sensor_fault | environmental_event}` → 证据聚合 → 链路质量 → 主机上行。
- **Backend**：校验、持久化、只读 API、可观测性。**不做重新判定。**
- **为什么跨节点融合必须在网关**：B/C 是星型发送端，**物理上拿不到邻节点数据**。
  v1 **不引入 Gateway→Node 邻节点广播**（空口预算 / 故障耦合 / 节点独立可运行性）。
  仅当 SQ2 实测表明网关侧延迟不满足需求时，才重新评估。
- **时间同步 v1**：local 单调时钟 → gateway time mapping（offset + tolerance）→
  **gateway 定义的 fusion window** → **nearest valid sample** → **freshness 检查**（`max_sample_age`）→ 跨节点融合。
  **不做漂移拟合**——先由 EXP-004 实测 offset / jitter / drift，只有数据表明简单同步不足时才引入。
- **配对三态**：`paired` / `partial_evidence` / `insufficient_alignment`，全部落 `fusion_windows` 表并可计数。
- **配对硬规则**：window 由 Gateway 定义；**seq 只用于本节点去重与缺包检测**；
  **禁止 `B.seq == C.seq` 作同步依据**；禁止用超龄样本硬融合。
- **配对可能性不变量 P0**：`节点 interval ≤ 2 × max_sample_age` 才可能 `paired`；
  超出即结构性 `partial_evidence`，**必须报告为自适应采样的代价，不是 bug**。
- **Primary RQ 阶段固定密集 5 s 采样**（EXP-004/005/006）以隔离混淆变量；
  异步/自适应配对只在 EXP-007 验证，并报告 `pairing coverage` 率。

## 事件/故障分类（Phase 0.2 冻结，三字段正交，勿合并）

- `event_category` ∈ `sensor_fault` / `shared_event` / `localized_event` / `network_fault` / `device_unavailable`
- `event_source` ∈ `sensing` / `environment` / `radio` / `power` / `firmware`
- `ground_truth_source` ∈ `injection_plan` / `physical_intervention_log` / `firmware_diagnostic_counter` / `host_observation`
- **只有 `sensor_fault` 与 `shared_event` 进 Primary RQ 指标**；`localized_event` 单列（stress test + limitation）；
  `network_fault` → EXP-010；`device_unavailable` → availability/observability，不进分类指标。
- 真值只存在于每个 EXP 的 `truth/episodes.csv`；评估用的联合表由脚本派生，**不设第二真值来源**。
- **链路质量 v1 字段**：包计数、序号缺口、重复、CRC 错误、到达间隔/jitter。
  **RSSI 条件启用（未实测确认可读则为 NULL）**；**SNR 不承诺**。
  任何 RF 指标必须经真实 E220 配置 + 真机日志验证后才能进 README claim。

## 真值 / 决策分离（Phase 0.2 Amendment 3，硬约束）

- **真值侧**（**只在 `experiments/EXP-xxx/truth/episodes.csv`，运行系统不可读**）：
  `truth_category`（`sensor_fault`/`shared_event`/`localized_event`/`network_fault`/`device_unavailable`）、
  `event_source`、`ground_truth_source`、`physical_or_injected`。
- **运行侧**：`anomaly_events.decision_label`（`normal`/`sensor_fault`/`shared_event`）+ `decision_source` +
  `confidence` + `reason_codes` + `evidence_json`。`device_events` 用 **`subsystem`**（不与真值 `event_source` 同名），
  **运行侧不得出现 `event_category` / `truth_category` / `physical_or_injected`**。
- 唯一连接点 = `experiments/analyze_experiment.py`（`truth_category` **vs** `decision_label`），
  该脚本**不得被运行系统导入**。任一所要求真值类别在评估集中出现 0 次 → **必须报错退出**。
- **注入器只做注入、不做标注**：节点上报的是观测到的症状，不是"我注入了什么"。
- **数据集复用**：可复用 family 定义 / injection plan / 参数 / 真值语义 / 干预方案；
  **禁止复用任何 raw dataset**。多节点结论必须在多节点配置上重新执行并生成自己的 raw/truth/manifest。
  EXP-007 例外：离线回放可复用，须在 `manifest.json` 的 `input_mode` 写明。

## 研究问题措辞（冻结，勿改回）

- Primary RQ 结尾为 **"…and what trade-off does it introduce in shared-event recall?"**
  **禁止** "without materially reducing recall"（"materially" 无可操作定义，是变相门槛）。
- 报告 7 项：fault→event rate / event→fault rate / **shared-event recall** / absolute delta /
  relative delta / confidence interval / per-scenario。

## 代码与文档路径（EdgeSense 侧）

- `firmware/sensor_node/`（B/C 同镜像）、`firmware/gateway/`（A）
- `edge/fusion_channel/`（跨通道，节点侧主机镜像）、`edge/fusion_node/`（跨节点，网关侧主机镜像）
- `experiments/EXP-000..010`，唯一指标入口 `experiments/analyze_experiment.py`
- 文档：`docs/PHASE_0_DESIGN_REVIEW.md` + `docs/PHASE_0_1_ARCHITECTURE_CORRECTION.md`
  + `docs/PHASE_0_2_FINAL_DESIGN_FREEZE.md`
- **最终冲突优先级：Phase 0.2 ＞ Phase 0.1 ＞ Phase 0**（三份文档开头均已声明）。
- **v1 架构已于 2026-09-27 冻结**：除真实实验数据证明某设计不可行外，不得再做宏观架构重设计；
  任何变更须引用触发它的 EXP id 与数据，以修正案形式追加。

## 工程文档与工具（Phase 1 Step 0 已建）

- `docs/engineering/design_decisions.md` —— D-01…D-10，含"触发重估的条件"。**工程判断的记录地。**
- `docs/engineering/architecture.md` —— 层→模块映射 + 各层可测试条件。
- `docs/hardware/wiring.md` —— **单一权威接线来源**。状态 `documented` = 有既往真机日志；
  `verified` = 本项目在真机确认。**目前无一项 verified。**
- `docs/research/experiment_design.md` —— 分类法 + truth schema + pairing 算法 + 指标门禁 + n 政策。
- `docs/vendored/vendor_sources.json`（手写权威）→ `scripts/vendor_manifest.py` → `vendor_manifest.json`（生成）。
  **校验：`python3 scripts/vendor_manifest.py --verify`。上游代码内部一行不改。**
- `scripts/preflight.py` —— 环境自检，**报告缺失但不失败退出**（`--strict` 才非零退出）。
- `experiments/EXP-000-template/` —— manifest.json + `truth/episodes.csv` + raw/ + results/。**无 truth 的 EXP 不算完成。**

## 上游仓库的两个关键事实（迁移前必读）

1. **EventGuard-LoRa 仍在活跃提交**（2026-09-27 02:12–02:44 有 5 个新提交），
   但 `firmware/common/` 无未提交改动、最后变更 2026-09-26。
   **只按记录的 commit `f7b6e44` 取用，永不跟踪上游 HEAD。**
2. **`adaptive-lora-iot` 完全没有 git 历史**（父仓库 `~/Documents/ChatGPT/paper` 零提交、内容 untracked）
   → **没有 commit 可记录，冻结靠逐文件 SHA-256**。迁入后 **EdgeSense 的仓库是它的第一份历史**。
   其 5 个真实数据/日志文件为**只读保留**，禁止修改；重新分析须作为新 EXP 与原结果并列。


## 硬件

- 3 × ESP32-S3（N16R8）、3 × LoRa E220-400T22D、SHT30、BH1750、电容式土壤湿度、0.96" OLED。
- ESP-IDF v5.4.4（在 `~/esp/esp-idf`），xtensa-esp-elf GCC 14.2.0。
- **角色**：A = Gateway；B、C = Sensor Node（**同一固件镜像，差异只在 node_id 配置**）。
- **B/C 必须同房间冗余部署（相隔 30–50 cm）**——这是主 RQ 成立的前提。分不同房间则跨节点证据失效。
- **硬件清单（Phase 0.2 批准后）**：3×ESP32-S3、3×E220-400T22D、
  **2×SHT30（0x44）+ 2×BH1750（0x23）——第二套已批准购置**、
  1×soil moisture（可选，**不作 shared-event 通道**——空间局部性太强）、1×OLED（不进出题标准）。
  **不采购**：第二个 soil、第二个 OLED、RTC、任何执行器。
- **部署前提**：B 与 C **同房间相隔 30–50 cm**，测量同一组物理量，互为冗余见证。
- **Phase 1 准入**：9 条条件见 Phase 0.2 §14；**C3（第二套传感器到位并 I²C 扫描验证）是唯一物理阻塞项**。

## 代码质量要求

- C/C++：`-Wall -Wextra -Werror` 零警告（SensorTrust 与 AdaptiveSense 已达此标准，不得放松）。
- Python：type hints、结构化日志、pytest。
- 每个模块可独立测试。**算法层（reliability / sampling）不得持有传感器或无线电驱动引用**，
  以便在主机编译、单测、并与 Python 实现做 parity 检查（这是 AdaptiveSense 已验证有效的关键做法）。
- 固件必须处理：重连、超时、看门狗、畸形帧、传感器故障、LoRa 故障。禁止只写 Happy Path。
- 无 secrets，提供 `.env.example`，无硬编码生产凭据。

## 整合策略（已冻结）

- **保持上游 repo 独立，EdgeSense 以固定 commit 用 git subtree vendor 引入**
  （不用 submodule——clone 时静默缺失对实验项目是隐患），配 `scripts/check_vendored_hashes.py` 防静默漂移。
- 记录 provenance 在 `docs/vendored/{SensorTrust,AdaptiveSense,EventGuard-LoRa,adaptive-lora-iot}.md`。
- **上游戏外代码绝不改其内部**：跨通道/跨节点融合层写在 EdgeSense 侧 `edge/fusion/`，
  因为 ① SensorTrust 语义已冻结，改它等于毁掉可引用性；② 要保持"B1 vs B3"的差异可归因。
- 同理，**不修改 AdaptiveSense 的策略本体**去加 trust——否则 S5 与 S6 分不开，SQ1 无法回答。
- **新贡献只集中在四处**：`edge/fusion_channel/`、`edge/fusion_node/`、`backend/`、`copilot/`。

## 关键路径（相对本工作区）

- SensorTrust：`~/Documents/SensorTrust`
- AdaptiveSense：`~/Documents/AdaptiveSense`
- TinyEdgeBench：`~/WorkBuddy/TinyEdgeBench`（保持独立，不进运行时）
- EventGuard-LoRa：`~/Documents/ChatGPT/EventGuard-LoRa`（只取传输层 + 实验编排）
- **adaptive-lora-iot：`~/Documents/ChatGPT/paper/adaptive-lora-iot`（系统基座，待迁入）**
- 归档对象：lora-p2p、EdgeSense-Fusion、Smart-Agriculture-Edge-AI、esp32-agri-node（另一条线）

## 本机环境坑（会影响构建与测试）

- zsh 下直接调 `grep` 偶尔返回空（shim 影响）→ 用专用检索工具。
- 多个仓库缺 venv（AdaptiveSense 缺 PyYAML、EventGuard `.venv` 损坏、TinyEdgeBench 缺 pytest）→
  需要 `scripts/preflight.py` 统一自检。**CI 绿 ≠ 本机可跑。**
- pip 装 sdist 易触发 `EEXIST` 假错误；批量删除会触发 SAFE_DELETE 保护
  → `export CODEBUDDY_SAFE_DELETE_ENABLED=0`，或 `env -i` 干净环境。
- 长命令（>2 分钟）会被 SIGKILL → `run_in_background` + 输出重定向到文件再读。
