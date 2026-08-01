# manifold-3-vision-detect

基于妙算 3（Manifold 3）的无人机视觉检测系统：从 DJI Matrice 4T 相机实时获取视频帧，在机载端用 TensorRT 运行目标检测模型（YOLO11-seg）。当前已打通「采集 → 预处理 → 推理 → 后处理」全链路，正在接入真实模型；联调完成后将打包为 DPK 发布。

> 本文档是人类快速入口。完整知识库见 [`docs/`](docs/README.md)。

## 快速开始

```bash
# 1. 初始化（含 PSDK 子模块）
git clone --recurse-submodules git@github.com:zyzh2002/manifold-3-vision-detect.git
cd manifold-3-vision-detect

# 2. 交叉编译（面向 Manifold 3）
cmake --preset manifold3-cross-release
cmake --build --preset manifold3-cross-release

# 3. 部署到妙算 3 并前台运行
./scripts/deploy.sh 192.168.42.120 run
```

详细的环境准备、构建与联调步骤见 [`docs/getting-started.md`](docs/getting-started.md)。

## 当前状态（摘要）

| 阶段 | 内容 | 状态 |
|---|---|---|
| Phase 1-3 | 仓库骨架、交叉编译工具链、PSDK 生命周期与 DPK 应用 | ✅ 完成 |
| Phase 4 | 单路 NV12 视频采集（1440x1080 @ 30fps，0 丢帧） | ✅ 完成 |
| Phase 5A | 合成 TensorRT 推理管线（端到端平均约 26ms，内存稳定） | ✅ 完成 |
| Phase 5B | 接入真实 YOLO11-seg 模型 | ⏳ 待办 |
| Phase 6 | 产品输出与 DPK 打包发布 | ⏸ 待办 |

完整状态与目标机验证记录见 [`docs/project-status.md`](docs/project-status.md)，路线图见 [`docs/roadmap.md`](docs/roadmap.md)。

## 已确定基线

| 组件 | 版本 / 方案 |
|---|---|
| 无人机 | DJI Matrice 4T |
| 机载计算平台 | DJI 妙算 3（Manifold 3） |
| 平台软件基线 | NVIDIA JetPack 5.1.3 / Jetson Linux r35.5.0 |
| 目标系统 | AArch64 GNU/Linux / Linux kernel 5.10 / glibc 2.31 |
| DJI SDK | Payload SDK 3.16.0 |
| 交叉编译器 | NVIDIA Bootlin GCC 9.3.0 二进制工具链 |
| Sysroot | Jetson Linux r35.5.0 完整 sysroot |
| 推理框架 | TensorRT 8.5.2 / CUDA 11.4.19 |
| 构建系统 | CMake ≥ 3.21 / CMake Presets |

`crosstool-ng` 仅作为备用方案：只有 NVIDIA 官方工具链出现已确认且已记录的限制时，才考虑自行构建工具链。

## 数据流

```text
Matrice 4T camera
    -> PSDK Liveview ImageStream
    -> NV12 frame handoff
    -> TensorRT inference
    -> detection result
```

H.264 路径仅用于能力验证与录像，不作为初始模型输入。具体分析见 [`docs/architecture.md`](docs/architecture.md)。

## 仓库布局

```text
├── README.md              # 本文档：人类快速入口（中文）
├── AGENTS.md / CLAUDE.md  # AI 代理工作指南（英文，人类无需阅读）
├── cmake/                 # 交叉编译工具链配置
├── config/                # Manifold 3 SSH 访问配置
├── docs/                  # 人类文档（中文）：上手/状态/路线图/架构/构建/开发约定
├── .agents/
│   ├── skills/            # AI 代理技能（Superpowers + PSDK 研究）
│   └── docs/              # AI 工作产物（英文）：里程碑计划、specs、实施计划
├── scripts/               # 部署、sysroot、凭据与工具脚本
├── src/
│   ├── app/               # 应用入口和模块组装
│   ├── capture/           # PSDK 视频帧接收 + 有界帧槽（LatestFrameSlot）
│   ├── core/              # PSDK 生命周期 + 凭据填充（psdk_user_info）
│   ├── inference/         # 预处理、TensorRT 引擎封装、后处理、指标
│   └── platform/          # Manifold 3 HAL/OSAL 适配
├── sysroot/               # 本地 Jetson sysroot（git 忽略，克隆后需自行准备）
├── tests/                 # 主机端单元测试 + 交叉 ELF 验证
└── third_party/psdk/      # PSDK 3.16.0 子模块（只读）
```

`sysroot/` 与 `.local/`（含 `credentials.env`）均被 Git 忽略，克隆后不会自动存在，需按 [`docs/getting-started.md`](docs/getting-started.md) 准备。

## 文档导航

- [`docs/README.md`](docs/README.md) — 文档总览与阅读路径
- [`docs/getting-started.md`](docs/getting-started.md) — 环境准备、构建、部署、运行、排障
- [`docs/project-status.md`](docs/project-status.md) — 项目当前状态与验证记录
- [`docs/roadmap.md`](docs/roadmap.md) — 里程碑路线图
- [`docs/architecture.md`](docs/architecture.md) — 系统架构与模块边界
- [`docs/build-environment.md`](docs/build-environment.md) — 工具链、sysroot、链接策略
- [`docs/development.md`](docs/development.md) — 开发工作流与代码约定

## 许可

本项目代码遵循 [GNU General Public License v3.0](LICENSE)。PSDK 子模块遵循 DJI Payload SDK 自身许可条款。
