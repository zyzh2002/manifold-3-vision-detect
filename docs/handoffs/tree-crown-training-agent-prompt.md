# Training Repo Handoff Prompt (tree-crown-training)

Copy this prompt to onboard the AI agent for the training repository.

---

# 训练仓库交接提示词（tree-crown-training）

## 角色与背景

你是本仓库的 AI 协作代理。本仓库是 **tree-crown-training**（YOLO11-seg 树冠目标检测的训练仓库），只负责"训练 → 验证 → 导出 → 发布"的 PC 端工作，**不涉及任何机载推理**。机载推理由另一个仓库（[manifold-3-vision-detect](https://github.com/zyzh2002/manifold-3-vision-detect)）承担，它是 C++ 交叉编译项目，与本仓库互不共享代码。

两个仓库唯一的交接点是 **Hugging Face 私有模型仓库**（`zyzh0/tree-crown-yolo11-seg`，SSH 可访问）。本仓库是产物的"生产者"，机载仓库是"消费者"。

## 工作流边界（必须遵守）

```
训练数据 → 训练/验证 → 导出 ONNX → 发布到 HF 私有仓库 → 机载仓库 fetch_model.sh 拉取 → trtexec 构建 .engine
```

- **本仓库终点是 HF 私有仓库**，不负责部署到设备。
- **数据集不进 git**。目录结构里 `data/` 用 `.gitignore` 忽略，或用 DVC 管理版本。
- **ONNX 权重不进 git**。最终产物通过 `publish.py` 上传 HF，绝不提交到仓库。
- 一旦发布，ONNX + `model.yaml` 就是对外契约，**改动必须升版本号**，不能静默覆盖。

## 参考约定（从机载仓库迁移，务必沿用）

机载仓库 [manifold-3-vision-detect](https://github.com/zyzh2002/manifold-3-vision-detect) 的约定可直接参考。初始化时点击 `docs/development.md` 与根目录 `AGENTS.md` 获取模板，并落地到本仓库：

- **分支策略**：trunk-based 主流开发，`main` 始终可用；短生命周期分支，前缀 `feat/`/`fix/`/`docs/`/`chore/`/`refactor/`/`test/` + kebab-case；rebase 不产生 merge commit；合入 `main` 前明确授权。
- **提交规范**：Conventional Commits，英文，如 `feat: train yolo11-seg`、`chore: scaffold repo`。
- **文档分层**：`docs/`（人类，中文）+ `.agents/docs/`（agent，英文）。根目录 `AGENTS.md` 放 agent 指令，`CLAUDE.md` 用 `@AGENTS.md` 引用。
- **语言规则**：代码注释、提交信息、agent 文档一律英文；人类文档（`docs/`）用中文；禁止中文注释与非英文标识符。
- **凭据管理**：密钥存 git 忽略的 `.local/credentials.env`（权限 600），不入库；HF_TOKEN 走这里。
- **Git 忽略**：`.gitignore` 忽略数据、权重、实验输出、ONNX 与凭据。
- **格式**：Python 用 `ruff`/`black`（或 `pyproject.toml` 内规则），**不沿用**机载仓库的 C++ `.clang-format`。

## 初始化阶段（当前仓库为空，你首先要做这一步）

本仓库目前只有一个空目录，尚未初始化。你需要在开始任何训练工作前，完成以下初始化：

1. **确认存在并接入**：`git status` 确认空仓库；若为空则 `git init`。
2. **参考约定**：阅读机载仓库 `docs/development.md` 与 `AGENTS.md`，把上述约定落地到本仓库（`AGENTS.md`、`CLAUDE.md`、`docs/`、`docs/README.md`）。
3. **创建目录结构**：按下方"仓库结构"建好所有目录和占位文件。
4. **写一份 `.gitignore`**：必须忽略 `data/`（数据集）、`runs/` 或 `weights/`（训练输出/权重）、`*.onnx`、venv、`.local/` 等。**核心原则：数据、权重、导出产物、凭据一律不进 git。**
5. **写 `README.md`**：说明数据来源、训练方法、导出、发布流程，以及"目标机 TensorRT 8.5.2"约束（见下）。
6. **写 `pyproject.toml`**：声明依赖（`ultralytics`、`onnx`、`huggingface_hub`、`pyyaml`、`ruff` 等）。
7. **搭好训练/导出/发布三入口**：`train.py`、`export.py`、`publish.py` 骨架（至少能跑通脚本、打印清晰日志）。
8. **写 `data.yaml`**：定义树冠类别列表的占位（真实类别待确认后填入）。
9. **提交初始化**：完成以上后，用清晰的英文 conventional commit 提交（例：`chore: scaffold training repo`）。**不要提交任何数据、权重或 ONNX。**

初始化完成后，再进入正常训练迭代。

## 仓库结构（初始化时应创建）

```
tree-crown-training/
├── README.md            # 简介：数据集、训练、导出、发布流程
├── AGENTS.md            # agent 指令（参考机载仓库）
├── CLAUDE.md            # 引用 @AGENTS.md
├── pyproject.toml       # Python 依赖（ultralytics, onnx, huggingface_hub 等）
├── data.yaml            # 树冠类别列表（与部署侧 model.yaml 对齐）
├── configs/             # 训练超参配置
├── train.py             # 训练入口（YOLO11-seg）
├── export.py            # 导出 ONNX（固定输入命名/尺寸）
├── publish.py           # 把 ONNX + model.yaml 上传到 HF 私有仓库
├── .gitignore           # 忽略数据集、权重、实验输出、凭据
├── docs/                # 人类文档（中文）：上手/开发/发布
└── .agents/docs/        # agent 工作产物（英文）：specs、plans
```

## 技术约束

- **语言**：Python（ultralytics / YOLO11-seg）。
- **模型**：YOLO11-seg，实例分割（box + mask）。
- **输入**：固定 1280x1280，3 通道 NCHW，FP16 可转（机载是 TensorRT 8.5.2）。
- **导出命名**：`export.py` 必须固定 ONNX 输入输出名，保证机载 `TensorRtEngine` 能按名校验。
- **类别列表**：`data.yaml` 和 `model.yaml` 的类别必须一致，机载侧读 `model.yaml` 做 ABI 校验。

## 发布契约（model.yaml）

`publish.py` 上传 HF 私有仓库时，同目录必须带上 `model.yaml`，内容至少包含：

```yaml
model_name: tree-crown-yolo11-seg
version: "<tag>"
input: { name: "images", dtype: "float32", shape: [1, 3, 1280, 1280] }
outputs: { name: "...", dtype: "...", shape: [...] }   # 按真实导出填写
classes: [ ... ]          # 与 data.yaml 完全一致
train_commit: "<git sha>" # 可回溯到训练配置
target_trt: "8.5.2"       # 目标机 TensorRT 版本
```

## 目标机约束（写进 README 供回归参考）

- 机载基线：TensorRT 8.5.2 / CUDA 11.4.19 / cuDNN 8.6.0。
- 输出 `.engine` 由设备端 `trtexec` 从 ONNX 构建，本仓库不产 `.engine`。
- 导出时注意算子兼容 TensorRT 8.5.2，避免不支持的层。

## 交接给机载仓库的信息

完成一次发布后，请在提交/说明里明确记录：
1. HF 仓库地址 + 版本 tag。
2. `model.yaml` 的输入/输出名、dtype、shape。
3. 类别列表。
4. 训练的 git commit（用于回溯）。

机载仓库的 `fetch_model.sh` 会按此拉取，机载侧 `TensorRtEngine` 会读 `model.yaml` 做按名 ABI 校验，替换掉目前硬编码的合成契约（`synthetic_engine_contract.h`）。

## 验收标准

- [ ] **初始化完成**：目录结构、`.gitignore`、`README.md`、`AGENTS.md`、`CLAUDE.md`、`pyproject.toml`、`train/export/publish` 三入口、`data.yaml`、`docs/` 均已就位并提交。
- [ ] 已达成本仓库的参考约定（分支、提交、文档分层、语言、凭据、git 忽略）。
- [ ] 初始化提交不含任何数据、权重或 ONNX。
- [ ] `train.py` 能跑通训练并复现。
- [ ] `export.py` 导出 ONNX 输入输出命名固定、形状正确。
- [ ] `publish.py` 能把 ONNX + `model.yaml` 上传到 HF 私有仓库并带版本号。
- [ ] 数据集、权重、实验输出均未提交 git。
- [ ] `model.yaml` 与 `data.yaml` 类别一致，含 train_commit 可回溯。
- [ ] 机载仓库能通过 `fetch_model.sh` 拉取并成功构建 `.engine`。