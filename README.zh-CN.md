<h1 align="center">EdgeSense-AIoT</h1>

<p align="center"><strong>故障感知自适应采样与 IoT 诊断研究</strong></p>

<p align="center">传感器可信度 · 跨节点证据 · 可复现实验</p>

---

<p align="center">
  <a href="#project-overview">概览</a> ·
  <a href="#研究方法与架构">方法与架构</a> ·
  <a href="#实现与验证状态">验证状态</a> ·
  <a href="#relationship-to-smart-agriculture-edge-ai">主项目关联</a> ·
  <a href="README.md">English</a>
</p>

---

<a id="project-overview"></a>

EdgeSense-AIoT 研究如何结合传感器可信度、本节点跨通道信息和邻近节点证据，区分传感器故障与空间共享环境变化。目标平台为 ESP32-S3 / E220 LoRa 网络，信任感知采样和基于证据的 AI 辅助诊断属于规划扩展。

**当前状态：Phase 1A 软件基础，已完成离线验证。** 已实现逐通道有效性、传感器可用性映射、节点配置、Protocol v1 和离线评估框架。传感器节点固件编译成功，但尚未刷写或在 ESP32-S3 上运行；融合、自适应采样和诊断界面尚未集成。

## 研究问题

- **主要问题：** 相比单通道读数，本节点跨通道上下文与跨节点证据能否区分单节点传感器故障和空间共享环境事件？对共享事件召回率带来什么权衡？
- **次要问题：** 信任感知自适应采样如何改变误报警、资源使用和共享事件召回率？

研究不预设融合或信任感知采样在所有场景都会改善结果。只影响一个见证节点的局部环境事件不在主要结论范围：仅有两个见证节点时，它可能与传感器故障无法区分。Dashboard、Backend 和 AI Copilot 是规划中的工程界面，不是研究贡献。

## 研究方法与架构

冻结设计将职责分为三层：

1. **节点本地采集与可信度：** 保留逐通道有效性，应用单通道基线，再补充跨通道上下文。采样只消费本地证据，不需要邻节点读数。
2. **网关融合：** 对齐节点时间戳，建立融合窗口，配对新鲜样本后再比较节点。序号相等不代表时间同步；部分证据与对齐不足都有显式状态。
3. **证据与诊断：** 后端持久化观测和决策，规划中的 Dashboard 与 Copilot 提供只读证据和解释。这些访问限制是设计要求，不是已实现能力。

**规划架构——下方状态表区分当前已实现部分：**

```text
Sensor B ─┐  本地：采集 → 逐通道有效性 → SensorTrust
          │        → 跨通道上下文 → 自适应采样 → LoRa
Sensor C ─┤  B/C：规划使用相同固件，同房间相距 30–50 cm
          ▼
      Gateway A：会话 → 时间对齐 → 跨节点融合 → 决策
          │
      串口上行
          ▼
      Backend：持久化 + 只读 API → Dashboard / AI Copilot
```

跨节点融合位于 Gateway A，星型网络传感器节点不接收邻节点读数。主要比较采用固定密集采样，隔离融合与采样的影响；自适应实验还需报告配对覆盖率。详见[冻结设计](docs/PHASE_0_2_FINAL_DESIGN_FREEZE.md)、[架构映射](docs/engineering/architecture.md)和[实验协议](docs/research/experiment_design.md)。

## 实现与验证状态

| 能力 | 当前证据 | 待完成工作 |
| --- | --- | --- |
| 逐通道有效性、可用性状态、驱动/帧映射 | 已实现、主机测试、已纳入固件构建 | 真实传感器验证 |
| B/C 节点配置与一致性 | 已实现、主机测试、已纳入固件构建 | 双节点硬件核验 |
| Protocol v1 编解码与流解析器 | 已实现、主机测试、已纳入固件构建 | 接入实际无线传输路径 |
| I²C 采集适配器 | 已接入 vendor 传感器层，编译验证 | 接线核验与当前固件下的真实读取 |
| 离线分析与 EXP-000 | 已实现，以合成 fixture 核验 | 研究数据集与策略评估 |
| LoRa 传输、SensorTrust、自适应采样 | 已设计，未接入当前固件 | 集成与硬件评估 |
| 跨通道上下文 / 网关跨节点融合 | 已设计，未实现 | 实现、对齐测试与配对实验 |
| Backend、Dashboard、AI Copilot | 已设计，未实现 | 工程实现与只读契约 |

记录的固件构建使用 ESP-IDF v5.4.4。**编译成功不等于硬件验证。** 传感器可用性描述通道能否提供读数，不证明其可信度。硬件配置在 EdgeSense 中仅有文档记录，尚未核验；第二套 SHT30 + BH1750 仍是双节点实验的记录前提。详见[构建证据](docs/engineering/build.md)、[接线与硬件前提](docs/hardware/wiring.md)和 [Phase 1A 报告](OVERNIGHT_BUILD_REPORT.md)。

## 评估状态与边界

**尚无研究性能结果或 EdgeSense 真机结果。** [EXP-000](experiments/EXP-000/results/analysis/report.md) 是核验分析计算的合成 fixture，不是可运行故障/事件分类器的效果证据。跨通道、跨节点、时间对齐、自适应采样和资源放置评估仍在规划中。

评估将 `sensor_fault`、`shared_event`、`localized_event` 与网络故障、设备不可用分开。真值保存在 `truth/episodes.csv`，仅离线分析器将其与 `decision_label` 关联。所要求的真值类别没有样本时，分析报错而非输出误导性比例。物理干预与软件注入分别报告，结论需可追溯至 manifest、真值、原始日志和生成结果。

EdgeSense 尚未测量运行期 RAM、算法延迟、功耗、能耗和链路可靠性。已知固件镜像体积是构建产物，不是运行期资源测量。规划中的通信开销代理 `(DATA bytes + ACK bytes) × 10 / 9600` 秒表示 UART 时间，不是 RF 空口时间或能耗。关联项目的既往测量不验证当前实现。

## 快速开始与复现

离线 fixture 不需要硬件。使用现有 CI 中的 Python 3.13；可选主机检查还需要 C 编译器。在仓库根目录执行：

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements-dev.txt
python scripts/preflight.py
python experiments/analyze_experiment.py --exp experiments/EXP-000
```

分析器将确定性报告写入 `experiments/EXP-000/results/analysis/`，复现的是 fixture 计算，不是规划中的研究实验。

与各项非硬件 CI 步骤一致的可选检查：

```bash
python scripts/vendor_manifest.py --verify
python scripts/check_config_parity.py
bash scripts/test_host.sh
python -m pytest
python scripts/static_checks.py
```

`scripts/check_all.py` 是完整本地检查，包含要求上游目录存在的 `--require-upstream` 校验；需要 manifest 中记录的路径和固定版本，缺少上游目录或上游已更新时，冻结校验会失败。独立 vendor 校验器会报告缺失的上游比较，跳过项不能当作通过。详见[vendor 来源记录](docs/vendored/)与 [CI 流程](.github/workflows/checks.yml)。

复现记录的构建需要 ESP-IDF v5.4.4 环境，并遵循[构建指南](docs/engineering/build.md)中的环境说明：

```bash
cd firmware/sensor_node
idf.py build
```

尝试真机运行前先核验接线。当前入口仅执行采集和编解码往返，不通过 LoRa 发送，也不计算可信度/变化评分。

## Relationship to Smart-Agriculture-Edge-AI

[Smart-Agriculture-Edge-AI](https://github.com/Sver0411/Smart-Agriculture-Edge-AI) 将 EdgeSense 的验证阶梯及 manifest/truth/raw/results 分离方式作为设计参考，详见其[集成报告](https://github.com/Sver0411/Smart-Agriculture-Edge-AI/blob/main/docs/V0_3_INTEGRATION_REPORT.md)。主系统未复制 EdgeSense 的业务实现、后端或诊断逻辑。EdgeSense 保持独立采集与诊断研究定位，不包含执行器控制路径。

## 局限与下一步

- 核验接线与两套传感器，再演示物理采集和 LoRa 路径，随后才形成硬件结论。
- 实现通道上下文、网关对齐/融合和信任感知采样；除汇总改善外，还报告负面结果与共享事件召回权衡。
- 两个同位置见证节点不能消除所有局部事件歧义。多通道一致也不证明环境事件：共享 I²C 故障可能同时影响多个通道。
- 网关侧与后端侧决策都发生在 LoRa 接收之后；放置比较涉及串口上行、处理、资源和后端可用性，不是避开 LoRa 延迟。
- 田间部署、无线法规验证、实测能耗、Mesh、多网关接管和执行器不在当前证据或 v1 范围内。

## 许可证与第三方权利

**尚未选择项目许可证。** 项目保留所有权利，未授权复用，仓库中没有 `LICENSE` 文件。

`firmware/common/vendor/` 中的文件仍受上游许可证、声明及使用限制约束。[来源记录](docs/vendored/)列出来源、固定版本、预期复用范围和边界，本 README 不改变这些权利。
