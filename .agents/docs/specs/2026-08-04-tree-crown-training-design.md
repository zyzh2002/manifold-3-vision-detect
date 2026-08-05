# Training Repository Design (tree-crown-training)

## Goal

Establish a separate, GitHub-public training repository for the YOLO11-seg tree-crown
detection model. It is the PC-side "producer" of the model artifact; the existing
onboard repository (manifold-3-vision-detect) is the "consumer". The two repositories
share no code; their only handoff point is a versioned Hugging Face private model repo.

## Decisions (user-confirmed)

- **Training code lives in its own GitHub public repo** (`tree-crown-training`), separate
  from the C++ onboard project. Different language, toolchain, and lifecycle justify the split.
- **Model artifacts are NOT in git.** The ONNX source model is versioned and published to a
  **Hugging Face private model repo**; the dataset never enters git (`.gitignore` or DVC).
- **No HF MCP.** The onboarding repo consumes the model via a download script
  (`fetch_model.sh`), not through an agent-facing MCP. Model fetching is a build/deploy path.
- **Runtime `.engine` is built on-device** with `trtexec` from the ONNX (target TensorRT 8.5.2),
  never committed. The `.engine` is device-specific (GPU + TRT version bound) and not reusable.
- **Training repo conventions mirror the onboard repo** where applicable: trunk-based branching,
  Conventional Commits, English code/comments/agent docs + Chinese human docs, git-ignored
  `.local/credentials.env` for secrets (HF_TOKEN), git-ignored data/artifacts. Python formatting
  uses ruff/black (NOT the onboard C++ `.clang-format`).

## Repository Handoff Point

```
┌──────────────────┐   ONNX + model.yaml    ┌──────────────────────┐
│  Training repo   │ ─────────────────────> │  HF 私有模型仓库      │
│  (GitHub 公有)    │    (版本化发布)         │  zyzh0/tree-crown-yolo11-seg│
└──────────────────┘                        └──────────────────────┘
                                                    │
                                             fetch_model.sh 拉取
                                                    ▼
┌──────────────────┐  ONNX + model.yaml   ┌──────────────────────┐
│  Onboard repo    │ ───────────────────> │ Manifold 3 设备        │
│  (本项目)         │    trtexec 构建 .engine│  (TensorRT 8.5.2)    │
└──────────────────┘                      └──────────────────────┘
```

## Training Repo Structure

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

## Model Artifact Contract (model.yaml)

`publish.py` uploads `model.yaml` alongside the ONNX. It is artifact metadata that records
the transport contract; the semantic inference ABI is defined in Phase 5B after a real ONNX
is exported and compared. `outputs` is a list, not a single mapping:

```yaml
schema_version: 1
model_name: tree-crown-yolo11-seg
version: v1.0.0
input:
  name: images
  dtype: float32
  shape: [1, 3, 1280, 1280]
outputs:
  - name: output0
    dtype: float32
    shape: [1, "channels", "anchors"]
classes:
  - tree-crown
train_commit: "<40-character git sha>"
target_trt: "8.5.2"
```

Final species/age/mask semantics, output layout, preprocessing/letterbox rules, and label
domains remain Phase 5B work and are not fixed by this file.

## Handoff to Onboard Repo

Each HF release tag is an immutable three-file release:

```text
model.onnx
model.yaml
SHA256SUMS
```

- `scripts/fetch_model.sh --version <tag>` resolves the exact tag commit, materializes the
  ONNX through Git LFS, verifies `SHA256SUMS`, and atomically installs the release under
  `.local/models/releases/<tag>/` with `.local/models/current -> releases/<tag>` (git-ignored).
- Phase 5B will define and implement the `TensorRtEngine` contract after a real exported ONNX
  is available. The current `TensorRtEngine` accepts only `synthetic_engine_contract.h` and
  does not parse `model.yaml`.
- Onboard repo changes are out of scope for this spec; this spec only defines the producer side
  and the handoff contract.

## Initialization Sequence (empty repo)

1. `git init` if needed.
2. Mirror onboard conventions into `AGENTS.md`, `CLAUDE.md`, `docs/`, `docs/README.md`.
3. Create directory structure and `.gitignore` (data, weights, ONNX, venv, `.local/`).
4. Write `README.md`, `pyproject.toml`, `data.yaml` (placeholder classes).
5. Scaffold `train.py`, `export.py`, `publish.py` skeletons.
6. Commit with an English conventional commit (e.g. `chore: scaffold training repo`).

## Acceptance Criteria

- Initialization commit present; contains no data, weights, or ONNX.
- `train.py` runs and reproduces; `export.py` produces fixed-name ONNX; `publish.py` uploads
  ONNX + `model.yaml` with a version tag.
- `model.yaml` classes match `data.yaml`; includes `train_commit`.
- Onboard `fetch_model.sh` can pull the artifact and build a `.engine` on-device.

## Open Items

- Actual tree-crown class list (placeholder in `data.yaml` until dataset is confirmed).
- Dataset size / provenance / versioning (DVC vs `.gitignore`).

## Confirmed Model Repo

- **HF repo**: `zyzh0/tree-crown-yolo11-seg` (private), accessible via SSH.