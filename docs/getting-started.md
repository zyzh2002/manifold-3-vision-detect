# 快速上手

本文介绍如何在主机上准备环境、交叉编译、部署到妙算 3（Manifold 3）并运行本项目的视觉检测应用。完整的环境细节见 [build-environment.md](build-environment.md)。

## 目录

1. [主机软件前置](#主机软件前置)
2. [克隆仓库](#克隆仓库)
3. [准备工具链与 sysroot](#准备工具链与-sysroot)
4. [配置环境变量](#配置环境变量)
5. [交叉编译](#交叉编译)
6. [主机单元测试（可选）](#主机单元测试可选)
7. [部署到妙算 3 并运行](#部署到妙算-3-并运行)
8. [开发凭据](#开发凭据)
9. [常见问题与排障](#常见问题与排障)

## 主机软件前置

交叉构建与测试需要以下主机软件：

| 软件包 | 用途 |
|---|---|
| `git` | 克隆仓库、管理子模块 |
| `cmake` (≥ 3.21) | 构建系统 |
| `gcc` / `g++` / `make` | 主机端编译工具链（预留给单元测试） |
| `python3` | 辅助脚本、DeepWiki Skill |
| `uv` | Python 依赖管理（生成 dummy ONNX 时按需拉取 `onnx`） |
| `binutils` | ELF 文件分析（`readelf`） |
| `file` | ELF 文件类型和架构检测 |

Ubuntu / Debian：

```bash
sudo apt install git cmake build-essential python3 binutils file
curl -LsSf https://astral.sh/uv/install.sh | sh    # uv 安装（或系统包管理器）
```

Fedora / RHEL：

```bash
sudo dnf install git cmake gcc gcc-c++ make python3 binutils file
```

> 自动化 AArch64 ELF 验证使用 Bootlin 工具链自带的 `aarch64-linux-readelf`，无需额外安装目标架构 binutils。

## 克隆仓库

```bash
git clone --recurse-submodules git@github.com:zyzh2002/manifold-3-vision-detect.git
cd manifold-3-vision-detect
```

已克隆的仓库可随时同步子模块：

```bash
git submodule update --init --recursive
```

## 准备工具链与 sysroot

交叉编译需要两个外部依赖，二者均**不纳入 Git**，克隆后需自行准备：

1. **NVIDIA Bootlin GCC 9.3.0 AArch64 工具链**（推荐放在 `.local-toolchains/`）；
2. **Jetson Linux r35.5.0 目标 sysroot**（推荐放在仓库根的 `sysroot/`，该目录被 Git 忽略）。

两者的下载地址、校验和与完整组装步骤见 [build-environment.md](build-environment.md)：

- 工具链：从 NVIDIA 官方 Jetson Linux 页面下载 Bootlin 工具链，SHA256 `7725b460...`；
- Phase 2 基础 sysroot：由 Jetson Linux r35.5.0 BSP + Tegra 示例 rootfs + `apply_binaries.sh` 组装；
- Phase 5 扩展（CUDA/TensorRT/cuDNN 开发文件）：从已连接的妙算 3 设备复制（推荐）或解压对应 `.deb` 包。

> 提示：仓库根存在 `sysroot -> .local-toolchains/downloads/Linux_for_Tegra/rootfs` 的符号链接约定，
> 详见 build-environment.md 的组装步骤。

## 配置环境变量

两个环境变量控制交叉构建：

| 变量 | 含义 |
|---|---|
| `MANIFOLD3_TOOLCHAIN_DIR` | Bootlin GCC 9.3.0 aarch64 工具链路径 |
| `MANIFOLD3_SYSROOT` | Jetson Linux r35.5.0 目标 sysroot 路径 |

最省事的方式是 source 仓库提供的脚本（保留已有变量，未设置时使用仓库默认路径）：

```bash
source scripts/setup_env.sh
```

等价于手动设置：

```bash
export MANIFOLD3_TOOLCHAIN_DIR="$(git rev-parse --show-toplevel)/.local-toolchains/bootlin-gcc-9.3-nvidia"
export MANIFOLD3_SYSROOT="$(git rev-parse --show-toplevel)/sysroot"
```

> 注意：`manifold3-cross-release` CMake 预设已通过 `environment` 内置仓库本地默认路径，直接配置即可。
> 若使用自定义/全局安装的工具链，请用 `-D MANIFOLD3_TOOLCHAIN_DIR=... -D MANIFOLD3_SYSROOT=...` 传入
> （cache 变量优先于预设环境，也优先于 shell 导出）。

## 交叉编译

```bash
cmake --preset manifold3-cross-release
cmake --build --preset manifold3-cross-release
```

预期结果：

- 生成 AArch64 目标二进制；
- `tests/toolchain/` 的 ELF 冒烟检查通过（架构、动态解释器、依赖、GLIBC/GLIBCXX 版本）。

若 sysroot 身份校验失败（缺少匹配的 r35.5.0 `/etc/nv_tegra_release`），仅在已记录明确原因时使用
`-DMANIFOLD3_ALLOW_UNVERIFIED_SYSROOT=ON`，不要默认开启。

## 主机单元测试（可选）

主机端单元测试覆盖 `src/core/`、`src/capture/`、`src/inference/` 与 `scripts/`：

```bash
cmake --preset host-debug
cmake --build --preset host-debug
ctest --test-dir build-host --output-on-failure
```

## 部署到妙算 3 并运行

妙算 3 通过 USB 连接主机，固定网络地址 `192.168.42.120`，SSH 用户 `dji`，私钥已入库：

```bash
# 重新编译 + 部署 + 前台运行
./scripts/deploy.sh 192.168.42.120 run

# 跳过重新编译，仅重新部署 + 运行
./scripts/deploy.sh 192.168.42.120 --no-build run
```

**运行前必须停止机上的 Smart3DExplore 应用**，否则本地通道绑定会报 "Address already in use"：

```bash
ssh -i config/manifold3_id_rsa dji@192.168.42.120 "dji_app_ctl stop Smart3DExplore"
# 兜底：pkill -f Smart3DExplore
```

结束后恢复：

```bash
ssh -i config/manifold3_id_rsa dji@192.168.42.120 "dji_app_ctl start Smart3DExplore"
```

SSH 私钥权限必须为 `600`（克隆后首次使用前执行）：

```bash
chmod 600 config/manifold3_id_rsa
```

合成推理管线冒烟测试脚本：

```bash
scripts/run_inference_smoke.sh
```

## 开发凭据

PSDK 连接飞机需要真实 DJI 开发者凭据（App ID / App Key / App License / 开发者账号），DJI 不提供官方示例凭据，
`app.json` 中的 `164884` 仅为格式示例。凭据在**构建时**注入、**绝不入库**：

```bash
cmake --preset manifold3-cross-release \
    -DMANIFOLD3_APP_ID=<id> \
    -DMANIFOLD3_APP_KEY=<key> \
    -DMANIFOLD3_APP_LICENSE=<license> \
    -DMANIFOLD3_APP_NAME=<name> \
    -DMANIFOLD3_DEVELOPER_ACCOUNT=<account>
```

日常开发可把凭据写入 git 忽略的 `.local/credentials.env`（权限 600），一条命令完成配置：

```bash
scripts/configure_cross_with_credentials.sh
```

文件缺失时脚本使用占位符并打印警告。应用启动时若检测到占位凭据，会报错并以退出码 1 结束。

## 常见问题与排障

| 现象 | 原因与处理 |
|---|---|
| `Address already in use` | 机上 Smart3DExplore 未停止，见上文「部署」章节 |
| SSH 拒绝使用私钥 | 私钥权限不是 600：`chmod 600 config/manifold3_id_rsa` |
| clangd 无诊断/无跳转 | 未生成编译数据库：先执行一次 `cmake --preset manifold3-cross-release`（`build-cross/` 被 git 忽略） |
| 链接报 GLIBCXX 版本问题 | 见 [build-environment.md](build-environment.md) 链接策略；默认不静态链接 libstdc++/libgcc |
| sysroot 身份校验失败 | 确认 sysroot 为 r35.5.0；确有原因时按文档使用 `MANIFOLD3_ALLOW_UNVERIFIED_SYSROOT=ON` |
| 设备包版本不匹配 | `scripts/extend_sysroot_from_device.sh` 会硬失败；核对设备 dpkg 版本与 build-environment.md 记录一致 |

更详细的构建环境、sysroot 组装与链接策略见 [build-environment.md](build-environment.md)。
