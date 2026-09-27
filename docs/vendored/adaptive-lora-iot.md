# Vendored: adaptive-lora-iot（EdgeSense 的系统基座）

> **本仓库没有 git 历史。** 冻结校验：`python3 scripts/vendor_manifest.py --verify`

## 来源与一个必须记录的事实

| 项 | 值 |
|---|---|
| local path | `~/Documents/ChatGPT/paper/adaptive-lora-iot` |
| 所属 git 仓库 | `~/Documents/ChatGPT/paper` |
| **该 git 仓库的提交数** | **0（`No commits yet`）** |
| `adaptive-lora-iot/` 是否被跟踪 | **否（untracked）** |
| remote | 无 |
| **因此可记录的 commit** | **不存在** |

**这带来一个真实的后果，必须写清楚**：

> EdgeSense 的**系统基座当前不受任何版本控制**。
> 它此前从未有过 git 历史，被迁入 EdgeSense 之后，**EdgeSense 的仓库将成为它的第一份历史**。
> 这意味着：在改任何一行之前，必须先做**内容哈希快照**（本文件 + `vendor_manifest.json`），否则改动之后将无法证明"原来的样子"。

**冻结机制**：commit 无法记录，因此以**逐文件 SHA-256** 为准。`docs/vendored/vendor_manifest.json` 中 `adaptive-lora-iot` 条目下的 `sha256` 值即权威记录。生成器同时记录了 git blob SHA-1（对该文件不适用，因为未被跟踪，故为 `null`）。

## 引入范围（16 个文件）

| 组 | 文件 | 引入目的 |
|---|---|---|
| **节点固件** | `firmware/node_s/main/{main.c, sensors.c, sensors.h, adaptive_policy.c, adaptive_policy.h, policy_config.h, e220.c, e220.h}` | **Sensor Node 的起点**：多传感器采集 + LoRa DATA 帧 + 自适应策略骨架 |
| **网关固件** | `firmware/node_g/main/{main.c, e220.c, e220.h}` | Gateway 的起点（需按冻结架构大幅扩展） |
| **实验流程** | `experiments/collect.py`、`experiments/add_event.py` | **不可变落盘**的采集脚本与**物理干预即时标注**流程 |
| **方法论文档** | `EXPERIMENTS.md`、`REPRODUCIBILITY.md`、`docs/HARDWARE_REVIEW.md` | 实验协议、可复现性规则、硬件评审记录 |

## 只读保留（5 个文件 —— **禁止修改**）

这些文件是 EdgeSense 的**首个真实数据集**，也是本项目的 provenance 起点。

| 文件 | 大小 | 内容 |
|---|---|---|
| `data/raw/20260925-phase2-main.csv` | 10,063 B | **150 个真实样本 / 5 s 间隔**，SHT30 + BH1750 + 土壤经 LoRa 上传 |
| `data/raw/20260925-phase2-main-events.csv` | 899 B | **6 条物理干预标注**（遮光起止、补光起止、呼气加湿、上电无扰动） |
| `logs/20260925-phase2-main-serial.log` | 14,989 B | 原始串口日志 |
| `logs/20260925-gateway-phase2-boot.log` | 379 B | 网关启动日志 |
| `logs/20260925-node-s-phase2-probe.log` | 612 B | 节点探测日志 |

**保留规则（硬约束）**

1. **不得修改**上述任何文件。
2. 若需要对同一批数据**重新分析**，必须作为**新的 EXP**（自带 `manifest.json` + `truth/` + `results/`）呈现，并与原结果**并列**，**不得覆盖**原结果。
3. 迁入 EdgeSense 后，这些文件进入 `datasets/real/`，且**保持内容哈希不变**。
4. 采集时点与条件必须随数据一起记录：**2026-09-24 17:44–17:57 UTC**（文件时间戳），节点 S 由充电器供电、网关接主机。

## 明确不引入

- `firmware/*/sdkconfig`、`firmware/*/managed_components/`、`idf_component.yml` —— 构建产物与依赖缓存，由 EdgeSense 自己的配置取代。
- `analysis/` —— 与 AdaptiveSense 的 `simulator/` 功能重叠。按 Phase 0 裁决：**以 AdaptiveSense 的 `simulator/` 为准**，仅把 `analysis/` 中独有的部分（真实参考流加载、物理事件标注的时间对齐逻辑）迁入，然后删除重复实现并留迁移说明。
- `paper/`（LaTeX 骨架）—— 留在原处不推进；Phase 0.2 明确"先不做论文"。

## 迁移时必须修的两处既有缺陷

这两项与 Phase 0.2 的修订无关，是 Phase 0 审计时**实测发现**的既有问题：

| # | 缺陷 | 证据 | 修法 |
|---|---|---|---|
| 1 | **`valid` 是整帧级而非逐通道级** | `data/raw/20260925-phase2-main.csv` 中 `soil_raw` **恒为 4095** 而 `valid=1`——一个事实失效（无介质）的通道被上报为有效 | 采用 AdaptiveSense `sensor.h` 的逐通道 validity 契约：不能测的通道上报 `null` + `valid=false` |
| 2 | 其 `analysis/` 与 AdaptiveSense `simulator/` 的实现重叠 | 两套 metrics 会算出两个数字 | 合并为一套（以 AdaptiveSense 为准），删除重复实现 |

## 在 EdgeSense 中的角色

**系统基座（B2 决策：直接迁入，不先创建独立公开 repo）。** 提供：多传感器节点固件、LoRa DATA 帧、真机采集与物理事件标注流程、以及首个真实数据集。

## 边界声明

1. **不修改历史实验结果。** 迁入的是代码与数据结构，不是被改写过的数字。
2. 其 `adaptive_policy.c` 恰好落在冻结架构划定的**节点侧闭环**内（感知 → 逐通道 validity → trust → 跨通道 → 本地变化检测 → 自适应采样 → 传输），**无需上移**，也**不得**为它加入任何跨节点逻辑。
3. 其 `EXPERIMENTS.md` 已写下的两条规则被 EdgeSense 直接继承：
   - "5 秒流是**密集参考**，不是校准过的物理真值"；
   - "训练/选择段与最终测试段必须不相交，评估规则在测试打分前固定"。
4. 其"PING 的 602 次发送由序号推定、未在 Node S 独立记录"这类**计数边界**声明，是 EdgeSense 报告链路数字时的写法标准。
