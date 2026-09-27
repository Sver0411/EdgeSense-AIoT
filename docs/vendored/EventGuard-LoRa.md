# Vendored: EventGuard-LoRa

> 上游仓库**内部代码永不修改**。冻结校验：`python3 scripts/vendor_manifest.py --verify`

## 来源

| 项 | 值 |
|---|---|
| remote | `https://github.com/Sver0411/EventGuard-LoRa.git` |
| local path | `~/Documents/ChatGPT/EventGuard-LoRa` |
| **pinned commit** | `f7b6e44597abe958d7e9d6d481265d0fae33b749` |
| commit date | 2026-09-27T02:44:16+08:00 |
| 冻结时所在分支 | `codex/public-repository-metadata` |
| 冻结时工作区 | `paper/main.pdf` 已修改、`.DS_Store` 未跟踪（**两者都不在冻结集内**） |

> **⚠️ 该上游在本次会话期间仍在产生提交**（今天 02:12–02:44 有 5 个新提交）。
> 这不影响本冻结集：`firmware/common/` **无未提交改动**，且最后一次变更在 `0b5b50d 2026-09-26T12:56:10+08:00`（"Diagnose and fix E220 receive stream parser"）。
> **纪律：只按上面记录的 commit 取用，永不跟踪上游 HEAD。** 上游继续演进不影响本冻结。

## 引入范围（8 个文件）

| 文件 | 引入目的 |
|---|---|
| `firmware/common/e220.c/.h` | E220 UART 驱动（M0/M1/AUX、只读配置探测） |
| `firmware/common/e220_stream_parser.c/.h` | **流式帧解析**——这是 `lora-p2p` 的 118 行驱动所缺的能力 |
| `firmware/common/protocol.c/.h` | **紧凑二进制帧 + CRC + 去重 + ACK** |
| `firmware/common/faults.c/.h` | **确定性应用层故障注入**（按 seed / frame kind / sequence / copy index 索引的丢包计划） |

冻结集之外，还以其**方法论**为参照（不复制代码）：`tools/run_hardware_validation.py`、`tools/finalize_hardware_dataset.py` 的实验编排与审计范式，以及 `results/final_hardware_v1/audit_report.md` 的 96/96 离线审计做法。

## 明确不引入

- `firmware/common/importance.c/.h`、`firmware/common/strategy.c/.h` —— 那是它的研究问题本体。
- `firmware/common/sensor_drivers.c/.h` —— EdgeSense 的传感器层来自 AdaptiveSense（同硬件、逐通道 validity）。
- `paper/`、`results/` —— 其研究记录。

## 在 EdgeSense 中的角色

**Communication Layer（LoRa 传输）+ 实验编排与审计方法论。**

## 边界声明（必须遵守）

1. **研究问题严格区隔**：EventGuard 的 importance 回答"哪些样本值得多花副本"（**投递优化**）；EdgeSense 的 trust 回答"这条读数是否可信"（**数据可信度**）。EdgeSense 复用其**传输与实验编排**，**不复用其研究问题**。
2. 其帧格式已被 96 次真机运行验证，因此 EdgeSense **不得**自造第二套 LoRa 帧格式。扩展 payload（多通道 + trust + session_id）时必须保持 CRC 与去重语义不变。
3. **`network_fault` 不进入 Primary RQ 的 `sensor_fault` 指标**（见 `PHASE_0_2` §2）。其 `faults.c` 的注入能力用于 EXP-010。
4. 必须沿用其已确立的限定写法：`estimated_communication_time_ms` 是 **UART 时间代理**（`(DATA bytes + ACK bytes) × 10 / 9600` 秒），**不是 E220 空口时间，不是焦耳**。未测电流时**不得**报告任何焦耳数。
5. 其保留失败案例的做法（seed-37 / run103 receive-path stall，原始证据保留且**明确声明原因未确认**）是 EdgeSense 处理未解问题的标准：**不声称已修复，不删除证据**。
