# manifold-3-vision-detect

基于妙算 3 的无人机视觉检测系统：从 Matrice 4E 相机获取实时视频帧，并在板端使用 TensorRT 运行目标检测模型。完成联调后，应用将打包为 DPK 发布。

## 已确定基线

| 组件 | 版本 / 方案 |
|---|---|
| 无人机 | DJI Matrice 4E |
| 机载计算平台 | DJI 妙算 3（Manifold 3） |
| 平台软件基线 | NVIDIA JetPack 5.1.3 / Jetson Linux r35.5.0 |
| 目标系统 | AArch64 GNU/Linux / Linux kernel 5.10 / glibc 2.31 |
| DJI SDK | Payload SDK 3.16.0 |
| 交叉编译器 | NVIDIA Bootlin GCC 9.3.0 二进制工具链 |
| Sysroot | Jetson Linux r35.5.0 完整 sysroot |
| 推理框架 | TensorRT 8.5.2 / CUDA 11.4.19 |
| 构建系统 | CMake ≥ 3.21 / CMake Presets |

`crosstool-ng` 仅作为备用方案：只有 NVIDIA 官方工具链出现已确认且已记录的限制时，才考虑自行构建工具链。

## Agent 开发注意事项

本仓库提供 PSDK 专用研究 Skill：

[`psdk-deepwiki-research`](.agents/skills/psdk-deepwiki-research/SKILL.md)

任何涉及 DJI Payload SDK API、生命周期、回调语义、平台适配、Liveview 或示例用法的研究和实现，在编写代码前都必须先调用该 Skill。该 Skill 要求：

- 通过 DeepWiki MCP 获取并验证最新的 PSDK 文档缓存；
- 完整阅读相关章节，而不是仅使用搜索摘要；
- 使用本仓库 PSDK 3.16.0 的头文件和示例核对函数签名、枚举、结构体及调用顺序；
- 本地 PSDK 3.16.0 资料与 DeepWiki 冲突时，以本地头文件和示例为准；
- DeepWiki MCP 不可用时停止 PSDK API 研究，不使用普通网页搜索替代。

Agent 还应阅读 [`AGENTS.md`](AGENTS.md)，其中包含完整的研究流程、代码规范和 Git 权限边界。

## 数据流

首选路径直接使用妙算 3 支持的 PSDK ImageStream 获取 NV12 帧，避免在初始实现中增加 H.264 解码依赖：

```text
Matrice 4E camera
    -> PSDK Liveview ImageStream
    -> NV12 frame handoff
    -> TensorRT inference
    -> detection result
```

H.264 路径首先用于能力验证和录像，仅在 ImageStream 无法满足需求时才作为模型输入路径。

## 仓库布局

```text
.
├── README.md
├── AGENTS.md
├── cmake/                       # Phase 2 构建配置
├── config/                      # Manifold 3 SSH 访问配置
├── docs/
│   ├── architecture.md          # 高层架构和模块边界
│   ├── build-environment.md     # 工具链、sysroot、ABI 和链接策略
│   └── plan.md                  # 结果导向的实施里程碑
├── scripts/                     # 部署与打包脚本（deploy.sh 可直接部署调试）
├── src/
│   ├── app/                     # 应用入口和模块组装
│   ├── capture/                 # PSDK 视频帧接收
│   ├── core/                    # PSDK 生命周期
│   ├── inference/               # TensorRT 推理
│   └── platform/                # Manifold 3 HAL/OSAL 适配
├── sysroot/                     # 本地 Phase 2 Jetson 基础 sysroot，不提交到 Git
├── tests/
│   └── toolchain/               # 交叉编译和 ELF 冒烟验证
├── third_party/
│   └── psdk/                    # PSDK 3.16.0 子模块，只读
```

`sysroot/` 和可选的 `.local-toolchains/` 均被 Git 忽略，克隆仓库后不会自动存在。

## 当前范围

Phase 2 已全部完成，包括主机侧交叉构建与目标机运行验证：

1. NVIDIA Bootlin GCC 9.3.0 工具链下载与校验。
2. Jetson Linux r35.5.0 sysroot 组装（BSP + sample rootfs + `apply_binaries.sh`）。
3. CMake 交叉编译工具链配置（`cmake/aarch64-manifold3.cmake`）。
4. 最小 C/C++ 目标程序编译与 ELF 静态验证。
5. 冒烟程序已在 Manifold 3 上运行通过：目标环境与基线一致（R35.5.0 / kernel 5.10.192 / glibc 2.31 /
   CUDA 11.4.19 / TensorRT 8.5.2 / cuDNN 8.6.0），动态依赖全部解析，无需 sysroot overlay。

Phase 3（最小 PSDK 生命周期与 DPK 应用）进行中：
- 已完成：platform 层移植（OSAL/FS/Socket/USB Bulk）、`src/core/` 最小生命周期、`src/app/` 入口、CMake 集成与链接、开发 DPK 生成（`build_dpk.sh`）、直接部署调试脚本（`scripts/deploy.sh <ip> run`）
- 设备已验证：二进制运行、handler 注册、FunctionFS 通道就绪、占位凭据拒绝路径
- 待完成：DJI Pilot 2 安装验证 DPK 生命周期；提供真实 DJI 开发者凭据后验证连接飞机

开发凭据通过 CMake 变量注入（`MANIFOLD3_APP_ID` 等，默认占位符），`app.json` 由 CMake 从同一变量生成，仓库不提交凭据。DJI 不提供官方示例凭据（`164884` 仅为格式示例）。

更细的实现选择保留到对应阶段获取实测数据后再决定，详见 [`docs/plan.md`](docs/plan.md)。

## 本地路径

推荐通过环境变量提供工具链和 sysroot：

```bash
export MANIFOLD3_TOOLCHAIN_DIR="$(git rev-parse --show-toplevel)/.local-toolchains/bootlin-gcc-9.3-nvidia"
export MANIFOLD3_SYSROOT="$(git rev-parse --show-toplevel)/sysroot"
```

或直接 source 仓库提供的脚本（默认值与上面相同，保留已有变量）：

```bash
source scripts/setup_env.sh
```

完整的环境来源、依赖分类、ELF 验证要求和构建命令见 [`docs/build-environment.md`](docs/build-environment.md)。

## 开发环境依赖

构建本项目需要在主机上安装以下软件包：

| 软件包 | 用途 |
|---|---|
| `git` | 克隆仓库、管理子模块 |
| `cmake` | 构建系统（≥ 3.21） |
| `gcc` / `g++` / `make` | 主机端编译工具链（预留给后续单元测试） |
| `python3` | 辅助脚本、DeepWiki Skill |
| `binutils` | ELF 文件分析工具（`readelf`） |
| `file` | ELF 文件类型和架构检测（`file`） |

**Ubuntu / Debian：**

```bash
sudo apt install git cmake build-essential python3 binutils file
```

**Fedora / RHEL：**

```bash
sudo dnf install git cmake gcc gcc-c++ make python3 binutils file
```

> 自动化 AArch64 ELF 验证使用 Bootlin 工具链自带的 `aarch64-linux-readelf`，
> 无需额外安装目标架构 binutils 包。

> 交叉编译器（NVIDIA Bootlin GCC 9.3）和 Jetson sysroot 需单独准备，详见 [`docs/build-environment.md`](docs/build-environment.md)。

## 初始化仓库

```bash
git clone --recurse-submodules git@github.com:zyzh2002/manifold-3-vision-detect.git
cd manifold-3-vision-detect
```

已克隆仓库可运行：

```bash
git submodule update --init --recursive
```

## 许可

本项目代码遵循 [GNU General Public License v3.0](LICENSE)。PSDK 子模块遵循 DJI Payload SDK 自身许可条款。
