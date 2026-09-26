# Architecture — EdgeSense v1

> **权威版本**：`docs/PHASE_0_2_FINAL_DESIGN_FREEZE.md` §12（Frozen Architecture）。
> 本文件**不重复**架构定义，只做三件事：指向权威版本、给出**层到模块的映射**、列出**各层的可测试条件**。
> 与 Phase 0.2 §12 冲突时，**以 Phase 0.2 为准**。

---

## 1. 分层与职责（引用冻结版）

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

## 2. 层 → 模块映射（含 vendor 来源）

| 层 | 模块路径 | 来源 |
|---|---|---|
| Sensing | `firmware/common/sensor/` | AdaptiveSense（`sensor.c/.h`、`sensor_bus`、`sensor_backend.h`、`sht30_proto`、`bh1750_proto`、`soil_moisture*`）**vendored** |
| Sensing | `firmware/common/supervisor/` | AdaptiveSense `sensor_supervisor.c/.h` **vendored** |
| Reliability (node-local) | `edge/sensor_trust/` | SensorTrust `core/` **vendored（pin commit）** |
| Reliability (node-local) | `edge/fusion_channel/` | **新建**（跨通道 context） |
| Sampling | `edge/adaptive_sampling/` | AdaptiveSense 策略 **vendored（= S5）** |
| Sampling | `edge/adaptive_sampling/trust_aware/` | **新建**（= S6） |
| Transport | `firmware/common/transport/` | EventGuard-LoRa `e220` + `e220_stream_parser` + `protocol` **vendored** |
| Comm fault injection | `firmware/common/comm_faults/` | EventGuard-LoRa `faults.c` **vendored** |
| Sensor fault injection | `firmware/common/fault_injector/` | SensorTrust `fault_injector.c` **vendored + 扩展为多通道** |
| Node role | `firmware/sensor_node/` | adaptive-lora-iot `node_s` **迁入 + 重构**（B/C 同镜像） |
| Gateway role | `firmware/gateway/` | adaptive-lora-iot `node_g` + 新建（会话/beacon/对齐/跨节点融合/判定） |
| Reliability (cross-node) | `edge/fusion_node/` | **新建**（网关侧逻辑的主机镜像，与 `firmware/gateway/` 同源） |
| Backend | `backend/` | EdgeSense-Fusion 分层骨架 **重构** |
| Dashboard | `dashboard/` | EdgeSense-Fusion 骨架 **重构** |
| Copilot | `copilot/` | **新建** |
| 实验 | `experiments/` | EventGuard 编排/审计 + SensorTrust 证据管线 + adaptive-lora-iot 采集/标注，**合并重构** |

## 3. 各层的可测试条件（这是分层能否成立的关键）

**硬要求**：`edge/` 下的**全部**算法模块（node-local 与 cross-node 都一样）**不得持有任何传感器或无线电驱动的引用**。它们必须能在主机上编译、单测、并与 Python 实现做 parity 检查。

理由：这是 AdaptiveSense 已验证有效的做法。**做不到这一点，算法就只能靠真机看日志来调**，无法进入 CI，也无法在离线数据上复现。

| 层 | 主机可测 | Python parity | 真机验证 |
|---|---|---|---|
| `edge/sensor_trust/` | ✅ 已有 21 个 C 测试（404 checks） | 参考实现 | 已有（SensorTrust v0.2） |
| `edge/fusion_channel/` | ✅ 必须 | ✅ 必须（决策逐步一致） | **待硬件** |
| `edge/fusion_node/` | ✅ 必须（网关侧逻辑也要能脱机测） | ✅ 必须 | **待硬件** |
| `edge/adaptive_sampling/` | ✅ 已有 parity 测试范式 | ✅ 已有 | 已有（AdaptiveSense） |
| `firmware/common/*` | ✅ 纯逻辑部分（协议、解析、注入） | ✅ 协议与判定需 parity | 待硬件 |
| `backend/` | ✅ pytest | — | 待 Phase 8 |
| `copilot/` | ✅ 契约测试（禁止写操作工具；数值必须可追溯） | — | 待 Phase 10 |

## 4. 明确非目标（冻结）

Mesh · 多网关接管 · 任何执行器或控制路径 · 云侧推理 · **Gateway→Node 邻节点数据广播** · Wi-Fi 传感器节点 · 深度学习异常检测 · Kafka / ClickHouse / Kubernetes / Elasticsearch · Prometheus + Grafana（v1） · RTC 模块 · OTA 升级。

## 5. 变更纪律

v1 架构自 **2026-09-27 冻结**。除**真实实验数据**证明某设计不可行外，不得再做宏观架构重设计。
任何变更必须引用触发它的 **EXP id 与具体数据**，并以**修正案**形式追加（见 `docs/engineering/design_decisions.md` 的变更纪律）。
