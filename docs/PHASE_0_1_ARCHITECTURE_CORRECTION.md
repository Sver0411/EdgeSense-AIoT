# EdgeSense Phase 0.1 — Architecture Correction

**EdgeSense: Fault-Aware Adaptive Sensing and AI-Assisted Diagnosis for Resource-Constrained IoT Networks**

修订日期：2026-09-27
上游文档：`docs/PHASE_0_DESIGN_REVIEW.md`（部分章节已被本文件取代）
修订性质：**只修改设计文档。未创建任何运行代码，未开始 Phase 1。**
被审计仓库状态：**本轮未触碰任何已有仓库。**

> **⚠️ 本文件已被 `PHASE_0_2_FINAL_DESIGN_FREEZE.md` 部分取代（2026-09-27）。**
> 被取代的章节：**§1（D6 配对机制细化、D3 相关的 SQ2 定位、D8/D9 的字段命名）、§2（SQ2 已删除）、
> §3（H4 降级为 Engineering Evaluation）、§5（配对算法与失败流）、§6（保留，见 Phase 0.2 §2）、
> §7.1（配对机制已由 Phase 0.2 §7 取代，漂移拟合升级条件不变）、§8（分类法已拆为正交三分类）、
> §9（B2 的 claim 已被收窄）、§10（EXP-003 已重建、EXP-008 已重新定位）、§14（四项 blocker 已全部批准）**。
> **§12、§13 完全仍然有效。**
> **以下三节内容有效，但个别条目的标签已过时，需按下述读法理解**：
> - **§4**：内容有效。其中"仅当 **SQ2** 实测表明…"应读作"仅当 **E1 / EXP-008** 实测表明…"——SQ2 已不再是研究问题。
> - **§11（Roadmap）**：内容有效。其中 Phase 11 的"**SQ2** 的代价测量完成"应读作"**E1** 完成"；EXP-008 已从"RQ 评估"改为"Engineering Architecture Evaluation"。
> - **§15（Before → After 表）**：表中 D6 与 SQ2 相关行的**目标状态**已被 `PHASE_0_2_FINAL_DESIGN_FREEZE.md` 附录的 15 项修订覆盖（配对机制、SQ2 降级、taxonomy 拆分、EXP-003 重建均已在 Phase 0.2 进一步修订）。
>
> ### 最终冲突优先级：**Phase 0.2 ＞ Phase 0.1 ＞ Phase 0**
> 依序为 `PHASE_0_2_FINAL_DESIGN_FREEZE.md` ＞ 本文件 ＞ `PHASE_0_DESIGN_REVIEW.md`。
> 任何冲突以序号大者为准。本文件位于中位，被 Phase 0.2 取代者以 Phase 0.2 为准。

---

## 0. 这次修订在解决什么

Phase 0 的审计结论（已有资产、GO/REFACTOR/DROP、vendor 策略、"B/C 同房间冗余部署"）
经复核**全部成立**，不需要推翻。

但 Phase 0 的设计里有 **5 个必须修的问题**，其中第 1 个是硬伤：

| # | 问题 | 严重度 | 处置 |
|---|---|---|---|
| 1 | **架构矛盾**：Phase 0 §12.2 把 Cross-Node Fusion 画在 Sensor Node B/C 内部，但 B/C 是星型拓扑的发送端，**物理上拿不到邻节点的实时数据** | **硬伤**：设计自相矛盾，实现必然返工 | 跨节点融合整体上移到 Gateway A（第 4 节） |
| 2 | **主 RQ 的类别定义过宽**："genuine environmental event" 把"只作用于单个节点的局部扰动"也算了进去，而局部扰动与传感器故障**在单节点视角下不可分辨** | **研究有效性**：类别定义不干净，主 RQ 会退化为不可分 | 引入三分类事件分类法，主 RQ 只研究 `sensor_fault` vs `shared_event`（第 3、8 节） |
| 3 | **冻结了无依据的成功门槛**（H1 的"相对下降 ≥ 50%"） | 方法论 | 删除门槛，改为报告量 + 区间 + 逐场景结果（第 4 节） |
| 4 | **时间同步被过度设计**：把"线性时钟漂移拟合"写成 v1 的架构要求，但没有数据支持 | 过度工程，且把未验证的假设写进架构 | v1 只做 beacon + session + 窗口配对 + tolerance，**先实测 offset/jitter/drift**，拟合作为有数据依据的升级项（第 7 节） |
| 5 | **链路质量字段超出可证实范围**：设计里承诺了 RSSI 与 SNR，但两者都未在 E220-400T22D 的真实配置与真机日志上验证过 | **claim 纪律**：会被直接质疑 | v1 只记录可证实的量；RSSI 需实测确认，**SNR 直接不承诺**（第 6 节） |

**一句话总结**：Phase 0 把系统分层画错了位置，把研究类别画得太宽，把两个未验证的假设（≥50%、漂移拟合）写成了架构。Phase 0.1 把这三件事各自收回到能被证据支持的范围内。

---

## 1. Changed Decisions

以下为本次**变更**的决定（不含保持不变的决定，那些见第 12 节）：

| ID | Phase 0 的决定 | Phase 0.1 的决定 | 变更理由 |
|---|---|---|---|
| **D1** | Cross-Node Fusion 在 Sensor Node B/C 内部执行 | **Cross-Node Fusion 在 Gateway A 执行**。节点只做 sensor acquisition → per-channel validity → SensorTrust → cross-channel fusion → local change detection → adaptive sampling → LoRa transport | 星型拓扑下节点拿不到邻节点数据。原设计是架构矛盾 |
| **D2** | 未讨论 Gateway→Node 邻节点数据广播 | **v1 明确不引入**。只有实验数据证明节点侧融合不可替代时才重新评估 | 反向链路要额外空口预算；会让节点采样行为依赖邻节点可用性；破坏"节点独立可运行" |
| **D3** | 最终判定 `{normal\|sensor_fault\|environmental_event}` 语义未定在哪一层 | **最终判定固定在第 11 步 = Gateway A**。节点产出 `node_trust` + `change_score`（局部量），网关产出最终 `decision` | 判定需要跨节点证据；节点只有局部证据 |
| **D4** | 主 RQ 的 "genuine environmental event" 包含一切真实环境变化 | 主 RQ 只研究 **`sensor_fault` vs `spatially shared environmental event`**。`localized environmental event` 明确排除出主 RQ，只作 limitation 与 secondary stress test | 局部扰动与传感器故障在单节点视角下不可分辨，混入正样本会污染类别定义 |
| **D5** | H1 成功门槛 = fault→event 误判率相对下降 ≥ 50% | **删除门槛**。改为报告：绝对误判率、绝对差、相对差、置信区间、逐场景结果；样本量允许时给预先声明的统计检验 | 门槛无先验依据，且会诱导事后挑口径。主假设必须允许被否定 |
| **D6** | 时间同步 v1 = beacon + **线性时钟漂移拟合** + 拟合残差 | v1 = beacon + session id + 窗口配对 + timestamp tolerance。**漂移拟合降级为"仅在实测表明简单同步不足时才引入"的升级项** | 用未测数据支撑的复杂机制做架构要求是过度工程；先测量 offset/jitter/drift |
| **D7** | 链路质量包括 RSSI 与 SNR | v1 只记录：包计数、序号缺口、重复计数、CRC 错误、到达间隔/jitter。**RSSI 仅实测确认可读后启用；SNR 不承诺** | 未在真实 E220-400T22D 配置与真机日志上验证过的 RF 指标不得进入设计或 README claim |
| **D8** | `anomaly_events.kind` = `fault` / `event` 两类 | 三取值 `sensor_fault` / `shared_event` / `localized_event`，并新增 `decision_source`(node/gateway)；**主 RQ 指标只统计前两类** | 使类别定义与分类法一致；`localized_event` 单列但隔离出指标 |
| **D9** | `network_metrics` 用 `lost_range` / `rssi_or_proxy` | 改为 `seq_gap_count` / `inter_arrival_ms` / `jitter_ms` / `rssi(NULL 允许)`；新增 `session_alignment` 表 | 字段名与可观测事实对齐；把时间对齐质量变成可查询的数据 |
| **D10** | 融合层路径 `edge/fusion/` | 拆为 `edge/fusion_channel/`（节点侧跨通道）与 `edge/fusion_node/`（网关侧跨节点） | 两层的运行位置、可测试条件、部署目标都不同，不能塞在一个目录里 |
| **D11** | Phase 6 只有一个阶段：跨节点融合 | Phase 6 内部两步：**先测时间同步（EXP-004），再做融合与对比（EXP-005/006）** | 让"是否需要漂移拟合"成为有数据依据的工程判断 |

---

## 2. Revised Primary RQ

### 主 RQ（唯一）

> **在资源受限的多节点环境感知网络中，融合"节点内跨通道证据"与"跨节点（互为冗余的邻节点）证据"，能否把【单个节点的传感器故障】与【空间共享的环境事件】区分开——即降低"把传感器故障误判为空间共享环境事件"的错误率，优于仅使用单通道读数的做法，且不显著牺牲空间共享环境事件的召回？**

English:

> In a resource-constrained multi-node sensing network, can fusion of within-node cross-channel evidence and across-node (redundant neighbour) evidence distinguish a **single-node sensor fault** from a **spatially shared environmental event** — reducing the rate at which sensor faults are misclassified as environmental events — compared with single-channel readings, without materially reducing recall on spatially shared environmental events?

### 术语的三条硬定义（本次修订新增）

| 术语 | 定义 | 判别依据 |
|---|---|---|
| **single-node sensor fault** | 异常只出现在**一个节点的一条或多条通道**上，且该异常由传感器或该节点链路引起 | 另一冗余节点在同窗口内**无**同向变化 |
| **spatially shared environmental event** | 异常在**两个互为冗余的节点所处空间内同时**出现，且方向一致 | 两节点在同窗口内**同向**变化 |
| **localized environmental event** | 异常是真实环境变化，但**只作用于一个节点**的位置 | **与 sensor fault 在单节点视角下不可分辨**——这正是 v1 明确不声称能解决的部分 |

### 主 RQ 的边界（必须写进论文与 README）

1. 主 RQ **只**回答"单节点故障 vs 空间共享事件"。
2. **空间共享**是主 RQ 成立的前提：它要求两个节点部署在**同一空间**（同房间、相隔 30–50 cm），因而是**互为冗余的见证**。
3. 主 RQ **不**回答"局部环境扰动 vs 传感器故障"。这个区分在只有两个见证节点的条件下**在原理上不可解**（除非引入第三种独立测量手段），因此它被明确列为 limitation，而不是被悄悄假装解决。
4. 主 RQ 的成立条件依赖硬件：**需要每个节点各有一组同型传感器**（见第 14 节阻塞项 B1）。

### 次级 RQ（仍为 2 个，未变）

**SQ1（下游收益）**：把可信度信号反馈进自适应采样策略，相比"仅按变化率自适应"的策略，能否在不降低空间共享事件召回的前提下减少误报与通信量？

**SQ2（边缘代价）**：在 ESP32-S3 上运行节点侧判别逻辑的代价（flash / RAM / 单次判定延迟）是多少，与"只上传原始读数、在后端判定"相比，端到端检出延迟相差多少？

**注意 SQ2 的措辞因 D1/D3 而微调**：现在被比较的是**网关侧的最终判定**与**后端判定**，因为跨节点判定已经不在后端而在网关。这个对照反而更干净——它测的是"把判定放在 LoRa 链路的上游边缘一侧 vs 放在 LoRa 链路之后"。

### 明确不作为 RQ 的部分（未变）

候选 RQ4（采样频率 / 通信开销 / 检出延迟 / 召回 / 数据保真度的 trade-off）**是评测框架，不是研究问题**，不占 RQ 名额。

---

## 3. Revised Hypotheses

**总原则：不冻结任何百分比门槛。** 假设只给出**方向性预期**，实际结果以报告量与区间呈现。

| 编号 | 假设 | 报告什么（不设门槛） | 若被否定的后果 |
|---|---|---|---|
| **H1**（主） | 在同房间部署的两个互为冗余的节点上，**跨节点证据**能把"单节点传感器故障"与"空间共享环境事件"区分开 | fault→event 与 event→fault 误判率的**绝对值、绝对差、相对差、置信区间**，以及**逐场景**结果。方向性预期：B3 的 fault→event 误判率**低于** B1 | 主 RQ 得到否定答案。转向"跨通道证据足够、跨节点无增益"这一同样成立且与 SensorTrust 不重复的结论 |
| **H2** | **跨通道证据**（同节点内 temp/hum/light 的同时性）对区分"单通道自激漂移"与"空间共享事件"有独立贡献 | 同上四件套 + 逐场景结果。方向性预期：B2 的 fault→event 误判率**低于** B1 | 说明同节点多通道融合没有价值，主 RQ 完全依赖跨节点 |
| **H3** | 把 trust 信号喂给自适应采样，能减少误报且不降低空间共享事件召回（对比纯变化率策略） | 误报率、召回率、采样数、通信字节数四项的**绝对差 + 区间** | SQ1 否定，回到"纯变化率自适应已足够"（这本身是 AdaptiveSense 的延伸证据） |
| **H4** | 网关侧判别的端到端检出延迟明显低于"上传原始读数、后端判定" | 两者的延迟分布（中位数 + min–max + p95）与各自代价（flash / RAM / 单次延迟） | SQ2 否定，架构需重新考虑判定位置（这是一个**重要的设计发现**，不是失败） |

**关于统计检验**：样本量允许时，选择检验方式并在**实验设计阶段预先声明**（检验类型、α、配对方式、是否多重比较校正）。样本量不足时明确写 `insufficient n for a confirmatory test`，只给描述性统计与区间。**禁止在看到结果之后再挑检验。**

**关于否定结果**：主假设被否定是**被允许的、且必须如实报告的**正常结局。这一点继承 EventGuard-LoRa 已证明可行的做法（它保留并公开了自己不利的 Pareto 结果）。

---

## 4. Revised Hardware / Software Architecture

### 4.1 硬件架构（跨节点融合已上移到 Gateway A）

```
                    Physical Environment
        （B 与 C 同房间、相隔 30–50 cm —— 同一空间）
                          │
              ┌───────────┴───────────┐
              │                       │
         Sensor Node B           Sensor Node C
         ESP32-S3                ESP32-S3
         SHT30 T/H ──── 冗余 ──── SHT30 T/H
         BH1750 lux ─── 冗余 ──── BH1750 lux
         (soil ADC)              (soil ADC)
              │                       │
              │  ── 节点侧闭环 ──       │
              │  1 sensor acquisition   │
              │  2 per-channel validity │
              │  3 SensorTrust          │
              │  4 cross-channel fusion │
              │  5 local change detect  │
              │  6 adaptive sampling    │
              │  7 LoRa transport       │
              │  （不做跨节点融合）      │
              │                       │
              └──────── LoRa ─────────┘
                (E220-400T22D, star)
                          │
                    Gateway A
                    ESP32-S3 + E220
          ── 网关侧 ──
          8  receive B/C data
          9  node / session management
          10 timestamp alignment
          11 cross-node evidence fusion
          12 final decision
             {normal | sensor_fault | environmental_event}
          13 evidence aggregation
          14 link quality
                          │
                    Serial (USB) → 主机
                          │
                 EdgeSense Backend
                 FastAPI + PostgreSQL
                 写入通路：仅网关
                 读通路：只读 API
                          │
              ┌───────────┼───────────┐
              │           │           │
        Observability   Events    Analytics
          （指标表）    （事件表）  (trust/采样历史)
              │           │           │
              └───────────┼───────────┘
                          │
            Read-only Copilot (Tool Layer)
            仅 GET 工具；数值只来自工具结果
                          │
                     Dashboard
               （工程状态视图，非营销首页）
```

**被故意删除的（未变）**：Mesh、多网关、任何执行器/控制路径、云侧推理、**Gateway→Node 邻节点数据广播**。

### 4.2 软件架构与分层边界（修订）

| 层 | 运行位置 | 职责 | 禁止 |
|---|---|---|---|
| Sensing | Node B/C | 传感器读取、**逐通道** validity、I²C 错误上报 | 不做判定 |
| Reliability (node-local) | Node B/C | SensorTrust 单通道 + **跨通道融合** → `node_trust` | **不做跨节点融合**；不做采样决策；不做通信 |
| Sampling | Node B/C | 自适应采样（消费本节点 `node_trust` + `change_score`） | 不直接操作传感器寄存器；**不依赖邻节点状态** |
| Transport | Node B/C + A | CRC 帧、序号、去重、重传、心跳、`session_id` | 不做业务语义 |
| **Reliability (cross-node)** | **Node A** | **跨节点证据融合** + 时间对齐 + **最终判定** + 证据聚合 | 不做传感器级故障检测本身；不做采样决策 |
| Gateway (session) | Node A | 节点/会话管理、信标广播、心跳、超时、链路质量、主机上行 | 不缓存原始样本超过 N 分钟 |
| Backend | 主机 | 校验、持久化、只读 API、可观测性 | **不做重新判定**；不提供任何写 API 给外部 |
| Copilot | 主机 | 只读工具 + 证据组装 + 解释 | 不得生成工具结果以外的数值；不得展示 CoT |
| Dashboard | 浏览器 | 状态、曲线、事件时间线、诊断 | 无写操作 |

**可测试性要求（继承 Phase 0，未变）**：**node-local** 的两层（Reliability / Sampling）与 **cross-node** 层都**不得持有传感器或无线电驱动引用**，必须能在主机编译、被单元测试、并与 Python 实现做 parity 检查。网关侧逻辑同样要能脱机测——这是它能不能进 CI 的前提。

### 4.3 为什么跨节点融合必须在网关（把矛盾说清楚）

| 理由 | 说明 |
|---|---|
| **拓扑** | B/C 是发送端，星型只上行。节点**没有**邻节点数据的接收路径 |
| **空口预算** | 若要做节点侧融合，必须新增 Gateway→Node 的邻节点广播，占用稀缺的空口预算 |
| **故障耦合** | 节点采样策略若依赖邻节点数据，一个节点掉线会改变另一个节点的采样行为——把"数据可信度"问题扩散成"系统可用性"问题 |
| **可运行性** | 节点侧采样闭环只依赖本节点信息，才能保证单节点掉网时仍能自主工作（这是环境监测节点的基本要求） |
| **证据粒度** | 跨节点一致性本质上是"两个观测的对比"，它天然需要一个能同时看到两者的地方——那正是网关 |

**升级条件（可证伪、有数据依据）**：仅当 **SQ2 实测**表明"网关侧判定的端到端检出延迟无法满足需求"时，才重新评估节点侧融合。届时必须在 `design_decisions.md` 写清触发条件与实测数据。

---

## 5. Revised Data Flow

### 5.1 完整链路（一次采样 → 最终判定）

```
【节点侧 B/C —— 闭环在设备上完成，不依赖邻节点】
 1. Sensor read             → 每通道 value + valid + node_ts_ms（本地单调时钟）
 2. Per-channel validity     → 不能测/读取失败的通道上报 null + valid=false
 3. Per-channel trust        → SensorTrust core（现成）→ flags + health_score + state
 4. Cross-channel fusion     → 同节点同时性证据（新）→ channel_consistency
 5. Local change detection   → 本节点变化分（AdaptiveSense change_detector，现成）
 6. Node-local output        → { node_trust, change_score, reason_codes }
 7. Sampling policy          → 下一采样间隔 + 是否上传（只依赖本节点量）
 8. Transport                → CRC 帧 + seq + node_id + session_id + node_ts_ms
                               + payload(读值, 逐通道 valid, node_trust, change_score)

【网关侧 A —— 跨节点融合在这里】
 9. Receive                  → CRC 校验 / 去重 / 序号连续性 / session 管理 / 链路质量
10. Timestamp alignment      → node_ts_ms → gateway timeline
                               （v1：beacon + offset + tolerance；不做漂移拟合）
11. Window pairing           → 按 (session_id, window_index) 配对两节点样本
12. Cross-node fusion        → 互为冗余节点的同物理量对比 → node_consistency
13. Final decision           → {normal | sensor_fault | environmental_event}
                               + confidence + reason_codes
                               （对齐不可用 → insufficient_alignment，拒绝跨节点融合）
14. Evidence aggregation     → 节点侧 (node_trust, change_score)
                               + 网关侧 (node_consistency, link_quality)
                               → 一条可被 Copilot 引用的完整证据

【主机侧】
15. Serial uplink → Backend  → 校验 → 持久化（不做重新判定）
16. Observability            → 指标表（延迟、错误、工具调用）
17. API → Dashboard / Copilot（均为只读）
18. Copilot → 工具调用 → 证据 → 诊断（数值来自工具结果，带 node/timestamp/metric 引用）
```

### 5.2 通信流（修订）

```
Node B ──DATA(seq, node_id, session_id, node_ts_ms, payload, node_trust, change_score)──▶
Node C ──DATA(...)────────────────────────────────────────────────────────────────────▶  Gateway A ──▶ 主机
Gateway A ──BEACON(gateway_epoch, session_id)（低速率，仅用于时间对齐）──────────────────▶  Nodes
```

**v1 不存在 Gateway → Node 的邻节点数据广播**（见 §4.3）。

### 5.3 失败流（新增时间对齐相关项）

| 失败 | 检测者 | 行为 | 可观测证据 |
|---|---|---|---|
| 节点与网关 offset 超出 tolerance | Gateway | 该窗口标记 `insufficient_alignment`，**该窗口不做跨节点融合**；最终判定降级为"仅节点侧证据"并明确标注 | `session_alignment` + `anomaly_events.decision_source` |
| 节点重启导致 session 变更 | Gateway | 新 session；**跨 session 不做窗口配对** | `device_events` + `session_alignment` |
| 只收到一个节点的数据 | Gateway | 不产生跨节点判定；只上报节点侧结论，并标注 `partial_evidence` | `anomaly_events` |
| drift 持续增长（长时运行） | Gateway | 记录 drift 观测值；**触发"是否需要拟合"的评估**（§7 升级条件） | `session_alignment.drift_ppm` |
| 其余（I²C / CRC / 序号缺口 / 重复 / 静默 / 网关重启 / 后端不可用） | 同 Phase 0 §12.5，未变 | — | — |

---

## 6. Link Quality：可证实字段清单（修订 D7）

**原则：任何 RF 指标必须在真实 E220-400T22D 配置 + 真机日志上验证之后，才允许进入设计、数据库与 README claim。**

| 字段 | v1 状态 | 依据 |
|---|---|---|
| packet count（DATA / BEACON / ACK 分别计） | ✅ **v1 记录** | 固件计数器，已验证（lora-p2p 的 TX/RX 与 G_STATS 即此类） |
| sequence gaps（缺口数与缺口区间） | ✅ **v1 记录** | 序号连续性判定，已验证 |
| duplicate count | ✅ **v1 记录** | 按 (node_id, session_id, seq) 去重计数，已验证 |
| CRC errors | ✅ **v1 记录** | EventGuard 的 CRC 帧已有该计数 |
| inter-arrival time / jitter | ✅ **v1 记录** | 主机侧由到达时间戳算出，**已验证存在**（lora-p2p：平均 0.99995 s，最短 0.957 s，最长 1.042 s） |
| **RSSI** | ⚠️ **条件启用** | 仅当实测确认"当前 E220 配置 + 真机日志"能读到它时才启用；否则字段存在但值为 `NULL`。**不得以估算或替代值填充** |
| **SNR** | ❌ **v1 不承诺** | 未在任何现有实验中取得过。**不进入设计、不进数据库、不进 README** |

**表述纪律**：即使 RSSI 可用，也只能写"在本次摆放与配置下观测到的 RSSI 范围"，不得写成链路质量保证或覆盖范围结论。这与 lora-p2p 已确立的"计数边界"写法一致。

---

## 7. Revised Time Synchronization Strategy

### 7.1 v1 只做四件事，不做拟合（修订 D6）

1. **本地单调时钟**：节点用 `esp_timer` 维护单调时间，每个样本带 `node_ts_ms`。**绝不用墙上时间**（无 RTC，且重启后会跳变）。
2. **Gateway beacon / epoch**：网关周期性（例如每 60 s）广播 `BEACON(gateway_epoch, session_id)`。
3. **session id**：每次网关启动生成新 session。**跨 session 不做窗口配对**，避免把重启前后的时间混在一起。
4. **sample / window id**：节点在 frame 里带自增 `seq`；网关按 `(session_id, node_id, window_index)` 配对两节点样本。**配对以窗口索引为主、时间戳为辅**——这样即使存在固定 offset，只要 offset 稳定且小于窗口长度，配对依然正确。**窗口长度必须显著大于预期 offset 与 jitter。**
5. **timestamp tolerance**：每对节点一个容忍阈值。残差超过 tolerance 的窗口标记 `insufficient_alignment`，**拒绝跨节点融合**，而不是用错误对齐硬融合。

### 7.2 必须先测量的三项（EXP-004）

| 测量项 | 含义 | 观测手段 | 为什么重要 |
|---|---|---|---|
| **offset** | 节点本地时钟与网关时间基之间的固定偏差 | beacon 交换差值，多次取中位数 | 决定窗口长度的下界 |
| **jitter** | 单次 offset 估计的离散程度 | 差值序列的分位数范围 | 决定 tolerance 能不能设得住 |
| **drift** | offset 随时间的变化率（ppm） | 长时（建议 ≥ 2 h）offset 序列的回归斜率 | **决定是否需要漂移拟合** |

**同时必须覆盖的 case**：节点重启后 offset 是否重置、网关重启后 session 变更的处理、两节点同时长时间运行时的 drift 差异。

### 7.3 升级条件（把"是否需要漂移拟合"变成 engineering decision）

> **只有当 EXP-004 的数据表明"offset + jitter 在目标窗口长度内不足以支持跨节点配对"时**（例如 drift 使 offset 在一小时内漂移到超过 tolerance），才引入线性时钟漂移拟合：
> 对每个节点拟合 `gateway_time ≈ a · local_time + b`，并上报拟合残差；残差超过 tolerance 的窗口仍然拒绝融合。

**在拿到数据之前，不把拟合写成架构要求。** 这个结论本身要写进 `docs/engineering/design_decisions.md`——无论结论是"需要"还是"不需要"，它都是一个有数据依据的工程判断，而这正是要展示的能力。

---

## 8. Revised Event Taxonomy

### 8.1 三个事件类别（互斥，用于主 RQ）

| 类别 | 定义 | 真值来源 | 是否进主 RQ 指标 |
|---|---|---|---|
| **`sensor_fault`** | 异常只在**一个节点**上出现，且由该节点的传感器或链路引起（漂移 / 卡死 / 越界 / 缺失 / 偏置 / 噪增 / I²C 故障 / 断电拔出） | 注入计划（软件注入）或物理干预记录（物理故障） | ✅ **是**（主 RQ 一类） |
| **`shared_event`** | 真实环境变化，且**在同一空间的两个冗余节点上同时、同向**出现（整区开/关灯、同一区域湿度整体变化、同一区域温度整体变化） | 物理干预记录（同一时刻对两个节点生效） | ✅ **是**（主 RQ 另一类） |
| **`localized_event`** | 真实环境变化，但**只作用于一个节点**的位置（只对 Node B 的 SHT30 呼气、只遮挡 Node B 的 BH1750） | 物理干预记录（只针对某一节点） | ❌ **否**。单列报告，只作 stress test 与 limitation |

外加一个平凡类别 **`normal`**（无故障、无事件），用于算误报率。

### 8.2 两个正交标注轴（不要与类别混淆）

| 轴 | 取值 | 说明 |
|---|---|---|
| **物理 vs 注入** | `physical` / `injected` | 这是**来源**标注，不是类别。每条 `device_events` 必须带此标注（Phase 0 硬约束，未变） |
| **幅度档位** | 按实际干预强度记录（例如"呼气 20–30 cm"、"遮光 2 分钟"） | 用于逐场景分层与 limitations |

### 8.3 为什么必须做这个区分（把研究边界写明确）

**在只有两个见证节点的条件下，`localized_event` 与 `sensor_fault` 在单节点视角下原理上不可分辨**：两者都表现为"只有一个节点变了"。

因此：
- 把它们混在一起做正样本，会让主 RQ 退化成一个**不可解**的问题；
- 正确做法是**明确承认这条边界**：主 RQ 只宣称解决 `sensor_fault` vs `shared_event`，而 `localized_event` 被显式列为 limitation。

**这一条比任何"我能解决一切"的表述都更有说服力**，因为它证明你知道自己的方法在什么条件下成立、在什么条件下不成立。

### 8.4 落地约束

- **不因此把 v1 扩展成四分类系统。** 分类法里有 3 个类 + normal，但**判别逻辑只有一条二分类主线**（`sensor_fault` vs `shared_event`，加一个 `normal` 前置门限）。`localized_event` **不需要**一个专门的判别器——它只需要在实验设计里被**正确地标注为"不属于主 RQ 的评估集"**。
- 数据库里 `anomaly_events.kind` 保留三个取值，但 `analyze_experiment.py` 在算主 RQ 指标时**过滤掉 `localized_event`**，并在输出里单独给一行它的结果（作为 stress test 证据）。

---

## 9. Revised Baselines

阶梯结构不变，只补充定义与报告要求：

| ID | 名称 | 运行位置 | 输入 | 说明 |
|---|---|---|---|---|
| **B0** | Raw + fixed threshold | 节点或网关 | 单通道原始读数 | 通用朴素对照 |
| **B1** | SensorTrust（单通道） | 节点 | 单通道样本流 | **现成**。主 RQ 的直接对照 |
| **B2** | Cross-channel only | 节点 | 同节点多通道 | H2 的判据。**不含任何邻节点信息** |
| **B3** | Cross-channel + cross-node（= **EdgeSense**） | 节点 + **网关** | 同节点多通道 + 邻节点同物理量 | 主 RQ 的待验方法。**跨节点部分在网关** |
| **S0–S4** | Fixed-5s / 10s / 20s / 40s / 60s | 节点 | — | **现成**（AdaptiveSense） |
| **S5** | AdaptiveSense（纯变化率） | 节点 | 本节点变化分 | **现成**。SQ1 的直接对照 |
| **S6** | Trust-aware adaptive（= **EdgeSense sampling**） | 节点 | `node_trust` + 变化分 | SQ1 的待验方法。**注意：S6 仍然只依赖节点侧信息**，跨节点证据不参与采样决策 |

**消融关系**：`B0 ⊂ B1 ⊂ B2 ⊂ B3`（判别侧）；`S0–S4 ⊂ S5 ⊂ S6`（采样侧）。

**架构级对照**：`Gateway-side decision` vs `Backend-side decision`（SQ2）。

**禁止的做法（保持不变）**
- 不允许把 B0/B1 实现得比它应有的水平弱。阈值要么取文献惯例值，要么由同样的校准流程选出，并在实验设计里写清。
- 不允许在测试集上挑选 baseline 参数。选择集与测试集必须不相交（继承 `adaptive-lora-iot/EXPERIMENTS.md` 的规则）。

**S6 的一处重要澄清（因 D1 而来）**：跨节点证据**不参与采样决策**。理由是采样决策必须在节点本地闭环（§4.3）。因此 SQ1 检验的是"**节点侧** trust 信号能否改善采样"，跨节点证据只服务于最终判定。这一点必须在方法文档里写明，否则会被质疑"为什么跨节点结论没有反馈到采样"。

---

## 10. Revised Experiment Matrix

**编号规则**：`EXP-<3 位>`。每个 EXP 一个目录，含 `manifest.json` + 原始日志 + 分析输出。

| ID | 目的 | 对照 | 硬件 | 干预 / 注入 | 阶段 | 主要指标 |
|---|---|---|---|---|---|---|
| **EXP-000** | **框架自检**：验证 `analyze_experiment.py` 在已知答案的 fixture 上算得对 | — | 无（离线） | 人工构造 fixture | Phase 4 | 指标是否正确（这是防"手算填数"的门禁） |
| **EXP-001** | 真机干净基线 | — | B（或 B+C） | 无 | Phase 4 | 误报率/小时（**必须带时长与样本数**）、采样数、字节数 |
| **EXP-002** | 单通道故障检测 | B1 | 单节点 | 6 类**软件注入**故障 × n 次 + 物理拔插 | Phase 3–4 | 逐类 precision / recall / F1、逐 episode 检出延迟 |
| **EXP-003** | **跨通道证据**（H2） | B2 vs B1 | 单节点 | 同 EXP-002 的注入集 | Phase 5 | fault→event / event→fault 误判率（绝对 + 差 + 相对差 + CI + 逐场景） |
| **EXP-004** | **时间同步测量** | — | B + C + A | 无（长时运行，含一次节点重启） | Phase 6（**第一步**） | offset / jitter / drift 实测值；`insufficient_alignment` 占比 |
| **EXP-005** | **判别能力演示** | — | B + C + A 同房间 30–50 cm | ① **shared_event**：整区开/关灯、同一区域湿度整体变化 × n 次；② **单节点故障**：只影响 B 或只影响 C × n 次 | Phase 6（第二步） | 两个 case **必须都真实演示**；各自的判定与真值一致性 |
| **EXP-006** | **主 RQ 对比**（H1） | B3 vs B1 / B2 | B + C + A | 复用 EXP-002 注入集 + EXP-005 的 shared_event 集 | Phase 6 | fault→event / event→fault 误判率四件套 + 逐场景；样本量允许时给预先声明的检验 |
| **EXP-007** | **采样策略**（SQ1） | S6 vs S5（及 S0–S4） | B（或 B+C） | 复用上一组数据集 | Phase 7 | 误报率、召回、采样数、通信字节数（**标注为应用层指标**） |
| **EXP-008** | **代价与延迟**（SQ2） | Gateway-side vs Backend-side decision | B + C + A + 主机 | 无 | Phase 11 | flash 增量、静态 RAM、单次判定延迟、端到端检出延迟分布 |
| **EXP-009** | **localized event 压力测试**（secondary） | 同 B3 | B + C + A | 只对 B 呼气、只遮挡 B 的 BH1750 × n 次 | Phase 11 | **明确预期：可能失败**。结果单列，**不计入主 RQ 指标**，只写 limitations |
| **EXP-010** | **链路投递战役** | — | B + C + A | ① 应用层注入丢包 / 重复 / 陈旧 / 延迟；② 物理条件变化 | Phase 2 | 包计数、序号缺口、重复、CRC 错误、到达间隔/jitter；**严格区分注入丢包与物理丢包** |

### 关于样本量 n（不冻结任意数字）

- n **不在本文件中冻结**。它必须在 `docs/research/experiment_design.md` 里**在采集测试数据之前**确定，并给出理由：要么来自 pilot 的方差估计（精度目标），要么来自功效计算（检验的检出能力）。
- 若两者都做不到，则**明确写"探索性实验，n 由 pilot 决定，不做确证性声明"**——这比编一个数字诚实。
- 每个 EXP 的 `manifest.json` 必须记录实际的 n 与未完成/失败的重跑次数。

---

## 11. Revised Roadmap（只列出受影响的阶段）

Phase 0 / 1 / 2 / 3 / 4 / 7 / 8 / 9 / 10 / 12 的 Goal 与 Exit Criteria **未变**（仅 EXP 编号调整）。以下只列受本次修订影响的阶段：

### Phase 5 — Cross-Channel Fusion（`edge/fusion_channel/`，**节点侧**）

- **Goal**：同节点多通道证据能区分"单通道异常"与"多通道同时变化"。
- **Deliverables**：`edge/fusion_channel/`；`firmware/common/` 内对应实现；`docs/research/methodology.md`。
- **Tests**：主机侧单测 + 与 Python 的 parity 测试；两种人工构造 fixture：① 湿度骤升而温度/光照不变；② 多通道同向变化。
- **Exit Criteria**：
  - **EXP-003 完成**，报告 B2 vs B1 的 fault→event / event→fault 误判率四件套（**不设门槛**）。
  - 融合决策在固件与 Python 上逐步一致（parity 通过）。
  - 决策输出含 `reason_codes` 且与实际触发判据一致。
  - **本阶段只依赖本节点数据**，不需要第二套传感器、不需要邻节点信息。

### Phase 6 — Time Synchronization + Cross-Node Fusion（**网关侧**，主 RQ 核心）

- **前置**：**第二套 SHT30 + BH1750 已到位**。
- **Deliverables**：`session_alignment` 机制；`edge/fusion_node/` + `firmware/gateway/` 内的跨节点融合与最终判定；`docs/research/results.md` 第一版；`design_decisions.md` 的"是否需要漂移拟合"结论。
- **内部两步（不可跳）**：
  1. **EXP-004：先测量 offset / jitter / drift。** 只有数据表明简单同步不足时，才引入线性漂移拟合。
  2. **EXP-005 / EXP-006：再做跨节点融合与主 RQ 对比。**
- **Tests**：时间对齐与窗口配对的单测；"数据不足时拒绝融合"的路径覆盖；两节点同镜像的行为对称性检查。
- **Exit Criteria**：
  - EXP-004 给出 offset / jitter / drift 的实测值与升级判据结论。
  - **EXP-005 的两个 case 都在真机上演示**：空间共享事件下两节点同向变化；单节点故障时另一节点不受影响。
  - **EXP-006 完成**，报告 B3 vs B1/B2 的误判率四件套 + 逐场景结果。
  - `insufficient_alignment` 的窗口**不参与跨节点融合**（有测试覆盖）。
  - 主 RQ 得到书面回答（成立或**不成立**），写入 `results.md`，**不修饰**。

### Phase 11 — Evaluation（新增 EXP）

- **Exit Criteria 补充**：
  - **EXP-008（SQ2）完成**：网关侧 vs 后端判定的代价与端到端检出延迟，**含 `Gateway→Node 广播`是否值得的判据**（若 EXP-008 显示网关侧延迟不满足需求，才触发 §4.3 的升级条件）。
  - **EXP-009（localized event stress test）完成**：如实报告结果，**无论成功或失败**，并写入 limitations。
  - **EXP-010（链路投递战役）完成**：严格区分应用层注入丢包与物理丢包。
  - `limitations.md` 必须包含：`localized_event` 与 `sensor_fault` 在单节点视角下不可分辨；n=2 见证节点的统计功效限制；单一摆放与单一信道的链路限制；trust 是启发式分数而非校准概率；**400 MHz 频段在日本的可部署性未验证**。

---

## 12. 保持不变的决定（逐条确认）

以下 Phase 0 决定**本轮明确复核后保持不变**：

| # | 决定 | 状态 |
|---|---|---|
| 1 | Gateway A 不挂传感器 | ✅ 不变 |
| 2 | B/C 使用**同一固件镜像**，差异只在 `node_id` 配置 | ✅ 不变（且成为跨节点实验的前提条件） |
| 3 | SensorTrust 保持独立 baseline，其内部语义不改 | ✅ 不变 |
| 4 | AdaptiveSense 保持独立 baseline，其策略本体不加 trust | ✅ 不变 |
| 5 | EventGuard-LoRa **仅**复用 transport / experiment methodology，不复用其 RQ | ✅ 不变 |
| 6 | TinyEdgeBench 不进入运行时，保持独立 companion | ✅ 不变 |
| 7 | Agent / Copilot **绝对只读**，架构上不留写入口 | ✅ 不变（且由契约测试强制） |
| 8 | 不做 actuator / 控制路径 | ✅ 不变 |
| 9 | 不做 Mesh | ✅ 不变（星型拓扑是 D1 成立的前提） |
| 10 | 不做 Kubernetes / Kafka / ClickHouse / Elasticsearch | ✅ 不变 |
| 11 | **Fault Injection 与 Experiment Framework 必须早于新算法** | ✅ 不变（Phase 3–4 仍早于 Phase 5–7） |
| 12 | 所有 claim 必须可追溯到真实 experiment evidence | ✅ 不变（且新增了"RF 指标需真机日志验证"的强化） |
| 13 | B/C 同房间冗余部署（30–50 cm） | ✅ 不变（且由 D4 的主 RQ 定义进一步强化为**必要前提**） |
| 14 | 上游 repo 独立 + 固定 commit vendor + 哈希校验 | ✅ 不变（路径由 D10 拆分） |
| 15 | 物理故障与软件注入故障数据层分开 | ✅ 不变 |
| 16 | README 数字由脚本渲染，禁止手算填入 | ✅ 不变 |

---

## 13. Migration Impact on Existing Repositories

**核心结论：本次修订对已有仓库的影响很小，且没有一处需要修改上游仓库的内部代码。**

| 仓库 | 影响 | 具体 | 是否需要改上游仓库 |
|---|---|---|---|
| **SensorTrust** | **无影响** | 仍作为 `edge/sensor_trust/` vendor 引入；跨通道融合仍写在 EdgeSense 侧（只是路径由 `edge/fusion/` 变为 `edge/fusion_channel/`） | ❌ 否 |
| **AdaptiveSense** | 轻微 | 仍作为 S5 baseline vendor；`sensor_supervisor.c` 仍复用；`communication.c`（Wi-Fi/MQTT）仍 DROP。**新增说明**：其"节点本地闭环"的性质与 D1 一致，无需改动 | ❌ 否 |
| **adaptive-lora-iot** | **中等（迁入时的工作量）** | ① 节点固件需补齐**逐通道** validity（原为整帧级，实测缺陷）；② 其 `adaptive_policy.c` 正好落在 D1 划定的节点侧闭环内，**无需上移**；③ **不需要**为它加任何跨节点代码；④ 其 `analysis/` 仍按 Phase 0 裁决与 AdaptiveSense `simulator/` 合并 | ❌ 否（它将被迁入，不是在原地改） |
| **EventGuard-LoRa** | 轻微 | 传输层仍按原裁决复用。**新增需求**：网关侧需要引入 `session_id` 与 `BEACON` 帧类型。这属于 EdgeSense 侧的扩展，EventGuard 的帧格式可作为基础但**其仓库不改** | ❌ 否 |
| **TinyEdgeBench** | 无影响 | 仍为外部成本测量工具 | ❌ 否 |
| **lora-p2p** | 无影响 | 仍归档为 P2P 可行性证据。其 inter-arrival 统计（0.957–1.042 s）成为 §6 中"jitter 可测"的既有依据 | ❌ 否 |
| **EdgeSense-Fusion** | 轻微（schema 层面） | 仍按 Phase 0 裁决搬后端/Dashboard 骨架并重构。**新增**：schema 需包含 `session_id`、`decision_source`、`session_alignment`；`anomaly_events.kind` 改为三取值 | ❌ 否（它是被重构对象） |
| **Smart-Agriculture-Edge-AI** | 无影响 | 仍只取协议词汇作为设计参考 | ❌ 否 |
| **EdgeFaultLab** | 无影响 | 仍暂缓 | ❌ 否 |
| **esp32-agri-node** | 无影响 | 仍只引用接线与土壤校准 | ❌ 否 |

**唯一需要**在已有仓库里动手的地方，是 Phase 0 已识别的技术债（`adaptive-lora-iot` 的逐通道 validity、EdgeSense-Fusion 的 git init 与 README 措辞），它们**与本次修订无关，是本就必须做的**。

**新增的 EdgeSense 侧代码位置（供 Phase 1 参照，本轮不创建）**：

```
firmware/sensor_node/          # 节点侧闭环（含 cross-channel fusion、本地变化检测、自适应采样）
firmware/gateway/              # 会话管理 + beacon + 时间对齐 + cross-node fusion + 最终判定
edge/fusion_channel/           # 跨通道融合（节点侧的主机镜像，与 firmware/sensor_node 同源）
edge/fusion_node/              # 跨节点融合（网关侧的主机镜像，与 firmware/gateway 同源）
```

---

## 14. Remaining Blockers

**阻塞级（✅ 全部已于 Phase 0.2 批准，本节仅作历史记录）**

> **状态更新（2026-09-27，Phase 0.2）**：下列 B1–B4 **全部已批准**，不再是阻塞项。
> B1 → 批准购置第二套 SHT30 + BH1750（soil 不作 shared-event 通道）；
> B2 → 批准 `adaptive-lora-iot` 整体迁入、**不先建独立公开 repo**、保留原始数据与 provenance；
> B3 → 批准主 RQ = `sensor_fault` vs `shared_event`，`localized_event` 只作 stress test + limitation；
> B4 → 批准保守表述，**禁止声称"适合日本部署"**。
> 权威版本见 `PHASE_0_2_FINAL_DESIGN_FREEZE.md` §0 与 §11。

| ID | 原阻塞项 | 原阻塞范围 | 需要谁决定 | 现状态 |
|---|---|---|---|---|
| **B1** | **第二套 SHT30 + BH1750（约 ¥30–60）是否加购？** 你只有 1 套。主 RQ 的 "spatially shared event" 定义**要求两个节点各有一组同型传感器** | Phase 6 整段、主 RQ 的跨节点部分 | 你 | ✅ **已批准**（Phase 0.2） |
| **B2** | **`adaptive-lora-iot` 的归属**：整体迁入 / 先发布再 vendor / 保持独立。它是 EdgeSense 的系统基座，且带 150 个真实样本与 6 次物理事件标注 | Phase 1 的起点 | 你 | ✅ **已批准：整体迁入，不先建 repo** |
| **B3** | **主 RQ 收窄（`sensor_fault` vs `shared_event`）是否接受？** 这会把 `localized_event` 排除出主 RQ 指标 | Phase 4–6 的实验设计 | 你 | ✅ **已批准** |
| **B4** | **频段表述**：是否接受"实验室频段验证、部署频段需另行确认、不声称日本可部署" | 申请材料的措辞 | 你 | ✅ **已批准（保守表述）** |

**技术未知项（不需你决定，但需要在对应阶段用数据解决）**

| ID | 未知项 | 在哪解决 | 若结论为"是"的后果 |
|---|---|---|---|
| **U1** | **简单时间同步（offset + tolerance）是否足够？** | EXP-004（Phase 6 第一步） | 需要引入线性漂移拟合（§7.3） |
| **U2** | **RSSI 在当前 E220 配置下是否可读？** | Phase 2 真机确认 | 可读则进入指标表；不可读则字段恒为 NULL，**不得用替代值填充** |
| **U3** | **网关侧判定的端到端延迟是否满足需求？** | EXP-008（Phase 11） | 若否，触发 §4.3 的升级条件，需重新评估节点侧融合 |
| **U4** | **每类故障/事件的样本量 n** | `experiment_design.md`，在采集测试数据前确定 | 决定能否做确证性检验；否则只能写探索性结论 |
| **U5** | **`localized_event` 在主 RQ 评估集上是完全排除还是分层报告？** | Phase 4 实验设计 | 影响 limitations 的写法（建议：完全排除出主指标，单列一行报告） |
| **U6** | **B/C 同房间 30–50 cm 的机械固定方式**（避免人员走动、气流扰动破坏"同一空间"前提） | Phase 1 部署 | 若固定不稳，"shared_event" 的同步性会被机械噪声污染 |
| **U7** | **物理干预的人工标注时钟精度**（例如"14:32"是按分钟记录的） | Phase 3 标注流程 | 决定 shared_event 的真值窗口宽度；若精度不足，需规定最小事件持续时长 |

**U2 与 U3 特别说明**：这两个未知项各自对应一条**已经在 Phase 0 里被写成承诺**的设计（RSSI 字段、网关侧判定），Phase 0.1 已把它们降级为"待数据确认"，这正是本次修订要做的收窄。

---

## 15. Before → After

| # | 项目 | Before（Phase 0） | After（Phase 0.1） | 理由 |
|---|---|---|---|---|
| 1 | **Cross-Node Fusion 位置** | Sensor Node B/C 内部（§12.2 架构图） | **Gateway A** | 星型拓扑下节点拿不到邻节点数据。原设计是架构矛盾 |
| 2 | **节点侧职责** | 感知 → trust（含跨通道/跨节点融合）→ 自适应采样 → 发送 | 感知 → 逐通道 validity → SensorTrust → **跨通道融合** → 本地变化检测 → 自适应采样 → LoRa transport | 节点只有局部证据；采样闭环必须不依赖邻节点 |
| 3 | **网关侧职责** | 注册、心跳、超时、链路质量、事件聚合 | **会话管理 + 信标 + 时间对齐 + 跨节点融合 + 最终判定** + 证据聚合 + 链路质量 | 跨节点融合需要同时看到两个节点 |
| 4 | **Gateway→Node 邻节点广播** | 未讨论（隐含"可选 BEACON/ACK"） | **v1 明确不引入**；仅在 SQ2 实测要求时才重新评估 | 空口预算、故障耦合、节点独立可运行性 |
| 5 | **最终判定位置** | 未明确（`{fault\|event\|normal}` 语义含糊） | **固定在第 11 步 = Gateway A**；节点只产出 `node_trust` + `change_score` | 判定需要跨节点证据 |
| 6 | **主 RQ 类别定义** | "genuine environmental event"（泛指一切真实环境变化） | **只研究 `sensor_fault` vs `spatially shared environmental event`** | 局部扰动与传感器故障在单节点视角下不可分辨，混入会污染类别定义 |
| 7 | **事件分类法** | `fault` / `event` 两类 | `sensor_fault` / `shared_event` / `localized_event` 三类 + `normal`；**主 RQ 只用前两类** | 让类别定义与分类法严格一致 |
| 8 | **`localized_event` 的处置** | 未区分，隐含算作正样本 | **排除出主 RQ 指标**；单列报告，仅作 stress test 与 limitation | 承认方法边界，而不是假装能解决 |
| 9 | **H1 成功门槛** | fault→event 误判率**相对下降 ≥ 50%** | **删除门槛**。报告绝对值 / 绝对差 / 相对差 / 置信区间 / 逐场景 | 门槛无先验依据，且诱导事后挑口径 |
| 10 | **统计检验** | 未规定 | 样本量允许时选检验并**在实验设计阶段预先声明**；不足时写 `insufficient n for a confirmatory test` | 防止看到结果再挑检验 |
| 11 | **主假设被否定** | 措辞上允许，但硬门槛暗示"必须达标" | **明确写为"被允许且必须如实报告的正常结局"** | 与已有项目（EventGuard）的诚实标准一致 |
| 12 | **时间同步 v1** | beacon + **线性时钟漂移拟合** + 拟合残差（架构要求） | beacon + **session id** + **窗口配对** + **timestamp tolerance**；**不做拟合** | 用未测数据支撑的复杂机制当架构要求是过度工程 |
| 13 | **漂移拟合的地位** | v1 的既定组成部分 | **有数据依据的升级项**：仅当 EXP-004 表明简单同步不足时才引入 | 把"是否需要"变成 engineering decision |
| 14 | **时间同步的必测项** | 未要求 | **必须先实测 offset / jitter / drift**（EXP-004），含节点重启与网关重启 case | 先测量，再决定 |
| 15 | **窗口配对机制** | 未规定（隐含靠时间戳） | 按 `(session_id, window_index)` 配对，**时间戳为辅** | 固定 offset 下窗口索引比时间戳更稳健 |
| 16 | **链路质量字段** | 包计数、序号缺口、重复、CRC、到达间隔/jitter、**RSSI、SNR** | 同上，但 **RSSI 条件启用（NULL 优先）、SNR 直接不承诺** | 未在真实 E220 配置 + 真机日志上验证过的 RF 指标不得进入设计与 claim |
| 17 | **RF 指标纪律** | 未明确定义 | **任何 RF 指标必须先经真实 E220 配置 + 真机日志验证，才可进入 README claim** | 强化 claim↔evidence 规则 |
| 18 | **`network_metrics` 字段** | `lost_range`, `rssi_or_proxy` | `seq_gap_count`, `inter_arrival_ms`, `jitter_ms`, `rssi(NULL)`；**无 SNR** | 字段名与可观测事实对齐 |
| 19 | **`anomaly_events` 字段** | `kind(fault/event)`, `decision` | `kind(sensor_fault/shared_event/localized_event)`, **`decision_source(node/gateway)`** | 支撑三分类与"判定来自哪一层"的追溯 |
| 20 | **时间对齐的数据留存** | "拟合残差作为质量指标"（未定义表） | **新增 `session_alignment` 表**：offset / jitter / drift_ppm / residual_max / quality | 让升级判据有可查询的数据 |
| 21 | **融合层路径** | `edge/fusion/`（单一目录） | `edge/fusion_channel/`（节点侧）+ `edge/fusion_node/`（网关侧） | 两层运行位置与部署目标不同 |
| 22 | **Phase 5 的前置条件** | 未明确 | **只依赖本节点数据**，不需要第二套传感器 | 跨通道融合与跨节点融合解耦，可先做 |
| 23 | **Phase 6 结构** | 单一阶段：跨节点融合 | **两步：先时间同步测量（EXP-004），再融合与对比（EXP-005/006）** | 让时间同步的结论反过来指导是否拟合 |
| 24 | **EXP 编号** | EXP-006 = S5 vs S6；EXP-004/005 为跨节点演示 | EXP-004=时间同步、005=判别演示、006=主 RQ 对比、**007=S5 vs S6**、008=代价、**009=localized stress test**、**010=链路战役** | 补齐时间同步与 localized 两个已识别的缺口 |
| 25 | **样本量 n** | 多处写"×5 次"等具体数字 | **不冻结**；在 `experiment_design.md` 里以 pilot 方差或功效计算为依据确定 | 与"不冻结无依据数字"的原则一致 |
| 26 | **SQ2 的对照措辞** | "Edge detection vs Backend detection" | "**Gateway-side decision** vs Backend-side decision" | 因 D1/D3，跨节点判定已不在后端而在网关 |
| 27 | **S6 的输入说明** | 未澄清 | **明确：跨节点证据不参与采样决策**，S6 只消费节点侧 trust | 采样闭环必须在节点本地；否则会被质疑"为何跨节点结论未反馈到采样" |
| 28 | **风险表** | R1–R14 | 新增 **R15（局部扰动污染类别定义）**、**R16（跨节点融合放错层）** | 记录本次修订识别的两个风险 |
| 29 | **对已有仓库的影响** | 未系统梳理 | 新增第 13 节：**无一处需要修改上游仓库内部代码** | 让 vendor 边界可核查 |

---

## 16. 本轮结束状态

- ✅ 5 个问题全部在设计层面解决。
- ✅ `PHASE_0_DESIGN_REVIEW.md` 已就地修订，并加了取代声明（冲突时以 Phase 0.1 为准）。
- ✅ 未创建任何运行代码，未开始 Phase 1。
- ✅ 未修改任何已有仓库。

**下一步仍是等待第 14 节的 4 个决断（B1–B4）。** 决断完成后，Phase 1 的第一步是：新建 EdgeSense 骨架目录（只建目录与文档）、写 `docs/vendored/*.md`（记录上游 SHA 与边界）、写 `docs/engineering/design_decisions.md` 的前两条、写 `docs/hardware/wiring.md` 并在真机上逐项复核。

**在获得确认前不进入 Phase 1。**
