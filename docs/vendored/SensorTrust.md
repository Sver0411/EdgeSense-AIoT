# Vendored: SensorTrust

> 上游仓库**内部代码永不修改**。本文件记录来源、引入范围与边界。
> 冻结校验：`python3 scripts/vendor_manifest.py --verify`

## 来源

| 项 | 值 |
|---|---|
| remote | `https://github.com/Sver0411/SensorTrust.git` |
| local path | `/Users/mac/Documents/SensorTrust` |
| **pinned commit** | `fad43a495abddb9029ef005d16a0909ca1de957c` |
| commit date | 2026-09-23T22:22:13+08:00 |
| describe | `v0.1-7-gfad43a4` |
| dirty at freeze | 否 |

## 引入范围（8 个文件）

| 文件 | 引入目的 |
|---|---|
| `core/sensor_trust.c` / `.h` | **Sensor Reliability Engine 内核**。C11 可移植，无动态内存 / 无 RTOS / 无硬件头文件 |
| `firmware/main/fault_injector.c` / `.h` | 传感器故障注入器（扩展到多通道） |
| `firmware/main/sensor_reader.c` | SHT30 在 ESP-IDF v5.4 `i2c_master` API 下的 bring-up 参考实现 |
| `hardware/capture.py` | 原始串口采集与落盘 |
| `hardware/evaluate.py` | 由原始日志计算指标 |
| `hardware/render_readme.py` | **把指标渲染进 README 的机制**——README 数字不得手写 |

## 明确不引入

- `firmware/main/experiment.c`、`firmware/main/main.c` —— 那是它的 v0.2 单通道实验，EdgeSense 有自己的 EXP 体系。
- `simulator/`、`results/` —— 其仿真与结果属于它的研究记录，不在 EdgeSense 运行时。

## 在 EdgeSense 中的角色

**单通道 baseline（B1）的权威实现**，不是 EdgeSense 的一部分。

## 边界声明（必须遵守）

1. **其 v0.1 语义已冻结**（RANGE / STUCK / SPIKE / DRIFT / MISSING 五类）。EdgeSense **不得**向 `core/sensor_trust.c` 内部添加任何跨通道或跨节点逻辑。
2. 跨通道 / 跨节点融合层写在 EdgeSense 侧（`edge/fusion_channel/`、`edge/fusion_node/`），理由是：
   - 改动冻结语义会毁掉它的可引用性；
   - 它作为**未来论文里的 prior work / baseline**，比作为自己的一部分更有价值；
   - 一旦融合逻辑进入它，就无法再干净地回答"B1（单通道）vs B3（融合）"的差异来自哪里。
3. **B1 的能力边界必须公平报告**：它只声明覆盖 5 类。`OFFSET/BIAS` 是它自己 README 明确记录的盲区；`NOISE increase`、I²C 接口故障、sensor disconnect **不在其声明范围**，因此不计为 B1 的失败（见 `PHASE_0_2` §2.4）。
4. 其 `health_score` 是**启发式严重度分数，不是校准概率**。任何材料中不得表述为概率或置信度。
5. 其 v0.2 的干净基线结论（1810 样本 / 30m09s / 0 误报）**只描述那一次观测**，不得重述为普适零误报率。
