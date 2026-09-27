# Vendored: TinyEdgeBench

> 上游仓库**内部代码永不修改**。冻结校验：`python3 scripts/vendor_manifest.py --verify`

## 来源

| 项 | 值 |
|---|---|
| remote | `https://github.com/Sver0411/TinyEdgeBench.git` |
| local path | `~/WorkBuddy/TinyEdgeBench` |
| **pinned commit** | `485494ef46445681027a5678a8ef87824625661c` |
| commit date | 2026-09-16T01:34:43+08:00 |
| dirty at freeze | 否 |

## 引入范围（3 个文件 —— 只取**方法**，不取数据集与结论）

| 文件 | 引入目的 |
|---|---|
| `benchmark/measure_flash.py` | **真实 flash 成本的测量方法**：用 `-DTINYEDGEBENCH_MODEL=...` 分别构建，读 ELF 段大小算差值（基线 189,492 字节） |
| `training/export_models.py` | 把模型导出成纯 C 浮点推理代码的方法 |
| `benchmark/run_benchmark.py` | 对照表与作图的组织方式 |

## 明确不引入

- `dataset/`（合成的 20,000 行 / 4 类窗口行为数据集）—— **任务定义不同**（窗口行为分类 vs 故障/事件判别）。
- `training/train.py` 与其"哪个模型更准"的结论。
- `results/`。

## 在 EdgeSense 中的角色

**部署评估（deployment evaluation）工具，不进入运行时。** 保持为**独立 companion repo**——不并入 EdgeSense 仓库、不进固件。

## 边界声明（必须遵守）

1. **可复用的量化依据**：它的结果显示 LR（0.8283）与 MLP（0.8250）的精度落在噪声内而 LR 更便宜，决策树与 MLP 更贵却不更准。这为 EdgeSense "v1 用规则/统计判别器，只有证据不足时才考虑 LR" 提供了**可引用的量化理由**，而不是"轻量算法更适合嵌入式"这种空话。
2. **引用其方法时必须带上它的限定**：`Compiled Flash Δ` 是**编译后 ELF 段差值**，包含判别器**加上**任何非空构建都会拉进来的少量固定脚手架代码；它**不应被读作纯模型大小**。
3. 其**决策树没有参数数组**（导出为嵌套比较），因此 `raw_constants_bytes` 对它为空、只在 `structural_estimate_bytes` 单独记录。**不得**把树的行与其他行按常数字节数并列比较。
4. **未测项不得编造**：硬件延迟、真实 RAM、功耗、单模型 RAM 全部标记为 not measured。EdgeSense 的 EXP-008 若测这些量，必须用同样的"未测就写 not measured"纪律。
