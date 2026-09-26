# Vendored: AdaptiveSense

> 上游仓库**内部代码永不修改**。冻结校验：`python3 scripts/vendor_manifest.py --verify`

## 来源

| 项 | 值 |
|---|---|
| remote | `https://github.com/Sver0411/AdaptiveSense.git` |
| local path | `/Users/mac/Documents/AdaptiveSense` |
| **pinned commit** | `6e0d086cf14c81d2fb5dba8305e13a7dff2dae3b` |
| commit date | 2026-09-23T13:28:55+08:00 |
| dirty at freeze | 否 |

## 引入范围（34 个文件，四组）

| 组 | 文件 | 引入目的 |
|---|---|---|
| **策略本体** | `change_detector.c/.h`、`adaptive_scheduler.c/.h` | **S5 baseline（纯变化率自适应）**。`-Wall -Wextra -Werror` 下已实测零警告 |
| **传感器层** | `sensor.c/.h`、`sensor_backend.h`、`sensor_bus.c/.h`、`sensor_supervisor.c/.h`、`sht30_proto.c/.h`、`bh1750_proto.c/.h`、`soil_moisture*.c/.h`、`sensor_sht30.c`、`sensor_bh1750.c` | **逐通道 validity 契约**的权威定义；共享 I²C 总线归属；可用性重探测状态机 |
| **离线评测** | `simulator/{replay,metrics,adaptive,scoring,events,config,fixed_sampling}.py` | EdgeSense 离线评测框架的基础 |
| **方法论** | `scripts/check_config_parity.py`、`tests/{test_parity_python_c.py,make_parity_fixture.py,host_build.py}`、`docs/change_score_spec.md`、`docs/methodology.md` | **C↔Python 决策 parity 测试**与 **YAML↔C 头文件一致性检查**——这两条是防止固件与仿真分叉的关键机制 |

## 明确不引入

- `communication.c/.h`、`communication_payload.c` —— Wi-Fi + MQTT。EdgeSense v1 走 LoRa。
- `power_mgmt.c`、`power_stats.c` —— v1 不做功耗声明；若日后需要能耗指标，只能作为 **UART 时间代理**，不得写成焦耳。
- `sensor_bme280.c`、`bme280_math.c` —— EdgeSense 硬件不含 BME280。

## 在 EdgeSense 中的角色

**Adaptive Sampling Engine 的 baseline（S5）与离线评测框架来源。**

## 边界声明（必须遵守）

1. **不修改其策略本体去加入 trust 信号。** EdgeSense 的 trust-aware 策略（S6）写在 EdgeSense 侧 `edge/adaptive_sampling/trust_aware/`，与原策略**并存**。理由：一旦把 trust 塞进它的策略，S5 与 S6 就分不开，Secondary RQ 无法回答。
2. **S6 只消费节点侧量**（`node_trust` + `change_score` + 本节点有效性）。跨节点证据**不得**进入采样决策——采样闭环必须在节点本地完成。
3. 其 `sensor.h` 的**逐通道 validity 契约**是 EdgeSense 传感器层的标准：不能测或读取失败的通道上报 `null` 且 `valid=false`，**不得**上报一个看似合理的 `0`。
4. 其 `communication.c` 里"application upload reduction 是应用层指标，不是无线电流量指标"的限定写法，在 EdgeSense 的通信指标中必须沿用。
5. 本机复现提示：该仓库**没有 venv**，其 Python 管线在当前机器上跑不通（缺 PyYAML）。其 **C 源文件**已本机验证在 `-Werror` 下零警告。**CI 绿 ≠ 本机可跑。**
