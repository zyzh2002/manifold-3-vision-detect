# 构建环境

本文记录交叉编译所用的工具链、sysroot、ABI、链接与验证规则。快速上手见 [getting-started.md](getting-started.md)。

## 选定构建基线

以下基线用于主机侧交叉编译，已确认可与本仓库组装出的工具链和 sysroot 配合使用。目标机运行确认仍需妙算 3 硬件。

| 组件 | 基线 |
|---|---|
| 目标 | DJI 妙算 3，AArch64 GNU/Linux |
| 妙算 3 SDK 基础 | NVIDIA JetPack 5.1.3 |
| Jetson Linux 基线 | r35.5.0 |
| Linux 内核基线 | 5.10 |
| 目标 glibc 基线 | 2.31 |
| 主编译器 | NVIDIA Bootlin GCC 9.3.0 二进制工具链 |
| Binutils | 2.33.1 |
| CUDA | 11.4.19 |
| TensorRT | 8.5.2 |
| cuDNN | 8.6.0 |
| DJI Payload SDK | 3.16.0 |

NVIDIA Bootlin 工具链是主编译器，因为 NVIDIA 为面向 Jetson Linux r35.5.0 的应用指定它。项目默认不自建编译器。

### 基线假设

JetPack 5.1.3 / Jetson Linux r35.5.0 是合理的起点，因为：

- 妙算 3 基于 NVIDIA Jetson Orin NX；
- PSDK 3.16.0 的 `libpayloadsdk.a` 以 `aarch64-linux-gnu-gcc` 为 AArch64 glibc 目标构建；
- JetPack 5.x 是 Orin NX 的生产发布系列（GCC 9.3 + glibc 2.31 + CUDA 11.4 + TensorRT 8.5）。

DJI 未公布妙算 3 固件内置的确切 JetPack / L4T 版本，实际设备固件才是运行时权威。请以目标机
`cat /etc/nv_tegra_release` 确认，若实测不匹配再调整 sysroot 基线。

## 工具链策略

- 使用 NVIDIA 官方预编译的 Bootlin GCC 9.3.0 AArch64 工具链。
- 工具链不入版本控制：可全局安装，或放在 git 忽略的 `.local-toolchains/` 下。
- 仅在 NVIDIA 工具链出现已确认并记录的限制后，才使用 crosstool-ng。
- 初始推理实现直接调用 TensorRT 与 CUDA C/C++ API，不编译自定义 `.cu` 文件。

## 环境变量

两个环境变量控制交叉构建：

| 变量 | 含义 |
|---|---|
| `MANIFOLD3_TOOLCHAIN_DIR` | Bootlin GCC 9.3.0 aarch64 工具链路径（含 `bin/`、`aarch64-linux/` 等） |
| `MANIFOLD3_SYSROOT` | Jetson Linux r35.5.0 目标 sysroot 路径 |

### 仓库内快速配置

```bash
export MANIFOLD3_TOOLCHAIN_DIR="$(git rev-parse --show-toplevel)/.local-toolchains/bootlin-gcc-9.3-nvidia"
export MANIFOLD3_SYSROOT="$(git rev-parse --show-toplevel)/sysroot"
```

或 source 仓库帮助脚本（应用同样默认值并保留已有值）：

```bash
source scripts/setup_env.sh
```

仓库根的 `sysroot/` 符号链接指向 `.local-toolchains/` 下组装好的 rootfs。`.local-toolchains/` 与 `sysroot/` 均被 git 忽略。

### CMake 预设与 IDE 集成

`CMakePresets.json` 提供 `manifold3-cross-release` 与 `host-debug` 两个预设，CMake Tools（VSCode）、CLion 与
CMake CLI 均原生支持。

- `manifold3-cross-release` 通过 `environment` 字段注入仓库本地默认工具链与 sysroot
  （`MANIFOLD3_TOOLCHAIN_DIR -> .local-toolchains/bootlin-gcc-9.3-nvidia`，`MANIFOLD3_SYSROOT -> sysroot/`），
  开箱即用，无需导出。`host-debug` 预留给主机单元测试，不需要二者。
- 因为预设 `environment` 会覆盖 shell 导出值，改用其他工具链/sysroot 时请用 `-D` cache 变量（见下）；
  工具链文件优先 cache 变量、其次环境变量。
- 仓库 `.clangd` 读取 `build-cross/compile_commands.json`；由于 `build-cross/` 被 git 忽略，
  全新克隆后需先执行一次 `cmake --preset manifold3-cross-release`，clangd 才有诊断与跳转。

### 全局工具链安装（可选）

```bash
export MANIFOLD3_TOOLCHAIN_DIR=/opt/toolchains/bootlin-gcc-9.3
export MANIFOLD3_SYSROOT="/opt/sysroots/manifold3-r35.5.0"
```

使用 `manifold3-cross-release` 预设时改传 `-D` cache 变量（预设 environment 会覆盖 shell 导出值）：

```bash
cmake --preset manifold3-cross-release \
  -D MANIFOLD3_TOOLCHAIN_DIR=/opt/toolchains/bootlin-gcc-9.3 \
  -D MANIFOLD3_SYSROOT=/opt/sysroots/manifold3-r35.5.0
```

### 验证

```bash
# 工具链
"$MANIFOLD3_TOOLCHAIN_DIR/bin/aarch64-linux-gcc" --version
# 预期: aarch64-linux-gcc.br_real (Buildroot ...) 9.3.0

# Sysroot
test -f "$MANIFOLD3_SYSROOT/lib/aarch64-linux-gnu/libc.so.6" && echo "glibc OK"
test -f "$MANIFOLD3_SYSROOT/usr/lib/aarch64-linux-gnu/tegra/libcuda.so" && echo "Tegra libs OK"

# 交叉编译
cmake --preset manifold3-cross-release
cmake --build --preset manifold3-cross-release
# 预期: 两个 AArch64 冒烟程序编译通过，verify_toolchain_smoke 的 2/2 ELF 测试通过
```

## 主机前置依赖

组装工具链与 sysroot 前需要：

| 软件包 | 用途 |
|---|---|
| `bzip2` | 解压 .tbz2 归档 |
| `lbzip2` | `apply_binaries.sh` 使用的并行 bzip2 解压 |
| `zstd` | `apply_binaries.sh` 解压 zstd 压缩的 Debian 包 |
| `tar` | 归档解压 |
| `curl` 或 `wget` | 下载归档 |
| `cmake` ≥ 3.21 | 构建项目 |
| `binutils` | 提供 `readelf` 等 ELF 检查工具 |
| `file` | 检查 ELF 类型与目标架构 |

Debian/Ubuntu：

```bash
sudo apt install bzip2 lbzip2 zstd tar curl cmake binutils file
```

## Bootlin GCC 9.3.0 aarch64 工具链

工具链从 NVIDIA 官方 Jetson Linux 页面下载，是 Bootlin `aarch64--glibc--stable-2020.08-1` 的重打包版，
带 NVIDIA 包装脚本，同时提供 `aarch64-linux-` 与 `aarch64-buildroot-linux-gnu-` 两种命令前缀。

| 属性 | 值 |
|---|---|
| GCC 版本 | 9.3.0 |
| Binutils 版本 | 2.33.1 |
| glibc 版本 | 2.31 |
| Linux 头文件 | 4.9.234 |
| 下载地址 | <https://developer.nvidia.com/embedded/jetson-linux/bootlin-toolchain-gcc-93> |
| 下载文件名 | `aarch64--glibc--stable-final.tar.gz` |
| SHA256 | `7725b4603193a9d3751d2715ef242bd16ded46b4e0610c83e76d8891cf580975` |
| 归档大小 | ~92 MB |
| 命令前缀 | `aarch64-linux-` |
| 替代前缀 | `aarch64-buildroot-linux-gnu-`（符号链接到包装脚本） |

快速下载与配置：

```bash
# 在仓库根目录
mkdir -p .local-toolchains && cd .local-toolchains
curl -L -o bootlin-gcc-9.3-nvidia.tar.gz https://developer.nvidia.com/embedded/jetson-linux/bootlin-toolchain-gcc-93
echo "7725b4603193a9d3751d2715ef242bd16ded46b4e0610c83e76d8891cf580975  bootlin-gcc-9.3-nvidia.tar.gz" | sha256sum -c
mkdir -p bootlin-gcc-9.3-nvidia && tar xzf bootlin-gcc-9.3-nvidia.tar.gz -C bootlin-gcc-9.3-nvidia
cd ..
export MANIFOLD3_TOOLCHAIN_DIR="$(pwd)/.local-toolchains/bootlin-gcc-9.3-nvidia"
```

## Phase 2 基础 sysroot

基础 sysroot 提供 glibc、标准 C/C++ 开发文件与 NVIDIA Tegra 运行时库，由以下官方 NVIDIA 包组装：

| 包 | 文件名 | 大小 | SHA256 |
|---|---|---|---|
| Jetson Linux Driver Package (BSP) | `Jetson_Linux_R35.5.0_aarch64.tbz2` | ~725 MB | `8cde3bd937d3eedb640a1c58d108c109f7cb904c38a03101dc17904b7d185ddf` |
| Tegra Linux Sample Root Filesystem | `Tegra_Linux_Sample-Root-Filesystem_R35.5.0_aarch64.tbz2` | ~1.5 GB | `d61cb54357f44fb7402da7e52151bf73a1cd698e33eaa6e6b49ab5ddc1bf60a6` |

下载基址：`https://developer.download.nvidia.com/embedded/L4T/r35_Release_v5.0/release/`

目标 sysroot 基于匹配的 NVIDIA 发布构建，而不是仅从 Bootlin 工具链拷贝：

```text
Jetson Linux r35.5.0 Driver Package (BSP)
+ Tegra Linux Sample Root Filesystem
+ NVIDIA binary overlay（来自 apply_binaries.sh）
```

Phase 2 基础包含 glibc、标准 Linux 开发文件与 NVIDIA 运行时 overlay。CUDA、TensorRT、cuDNN、OpenCV、FFmpeg、
GStreamer、ONNX parser 与 TensorRT plugin 开发包**不是** Phase 2 输入，只在后续用到其 API 的阶段扩展此已验证基础。

基础 sysroot 可放在仓库工作目录的 `sysroot/`（git 忽略，绝不提交），也支持集中管理位置。权威路径通过
`MANIFOLD3_SYSROOT` 提供：

```bash
export MANIFOLD3_SYSROOT="$(git rev-parse --show-toplevel)/sysroot"
```

- 不要把其他 JetPack / Jetson Linux 版本的文件混入本 sysroot；
- CMake 必须以 `MANIFOLD3_SYSROOT` 为目标搜索根，不得从主机 x86_64 目录解析目标头文件或库；
- 标准 r35.5.0 sysroot 是构建基线，实际妙算 3 固件是运行时权威；仅在实测出 ABI 或依赖差异、
  需要文档化 overlay 时才从设备拷贝文件。

### 组装步骤

```bash
# 工作目录：仓库根

# 1. 创建下载目录
mkdir -p .local-toolchains/downloads && cd .local-toolchains/downloads

# 2. 解压 BSP
tar xjf Jetson_Linux_R35.5.0_aarch64.tbz2

# 3. 解压示例 rootfs 到 Linux_for_Tegra/rootfs
tar xjf Tegra_Linux_Sample-Root-Filesystem_R35.5.0_aarch64.tbz2 -C Linux_for_Tegra/rootfs/

# 4. 应用 NVIDIA 二进制 overlay（--rootless：仅用于 sysroot）
cd Linux_for_Tegra
./apply_binaries.sh --rootless --target-overlay

# 5. 把 rootfs 符号链接为仓库 sysroot（仓库根执行）
cd "$(git rev-parse --show-toplevel)"
ln -sf .local-toolchains/downloads/Linux_for_Tegra/rootfs sysroot

# 6. 相对化 sysroot 中的绝对 .so 符号链接
#
# Ubuntu 20.04 rootfs 的 dev 符号链接形如 libpthread.so ->
# /lib/aarch64-linux-gnu/libpthread.so.0。交叉链接器不会对绝对符号链接目标应用
# sysroot 前缀，会回退到静态库并因 GLIBC_PRIVATE 符号失败（如 libpthread.a 的
# `_dl_pagesize`）。/lib 已并入 /usr/lib，修 usr/lib/aarch64-linux-gnu 下的链接即可。
cd sysroot/usr/lib/aarch64-linux-gnu
for l in lib*.so lib*.so.*; do
    [ -L "$l" ] || continue
    t=$(readlink "$l")
    case "$t" in
        /*) b=$(basename "$t"); [ -e "$b" ] && ln -sfn "$b" "$l";;
    esac
done

# 7. 设置环境
export MANIFOLD3_SYSROOT="$(pwd)/sysroot"
```

### 基础 sysroot 内容（apply_binaries.sh 之后）

- glibc 2.31（`lib/aarch64-linux-gnu/libc-2.31.so`）
- libstdc++ 6.0.28（`usr/lib/aarch64-linux-gnu/libstdc++.so.6.0.28`）
- NVIDIA Tegra 驱动库（`usr/lib/aarch64-linux-gnu/tegra/`）
- CUDA 驱动 `libcuda.so`（仅运行时；CUDA toolkit 头文件需要 JetPack SDK）

### JetPack 5.1.3 SDK 组件（推迟）

以下包仅在其 API 被使用时加入：

| 组件 | 版本 | 需要方 |
|---|---|---|
| CUDA Toolkit | 11.4.19 | Phase 5（TensorRT 推理） |
| TensorRT | 8.5.2 | Phase 5（TensorRT 推理） |
| cuDNN | 8.6.0 | Phase 5（TensorRT 推理） |

通过 NVIDIA SDK Manager 或 JetPack APT 源安装到目标 sysroot，在 Phase 5 扩展已验证的 Phase 2 基础 sysroot；
Phase 2 冒烟二进制不需要它们。

默认情况下工具链拒绝缺少匹配 r35.5.0 `/etc/nv_tegra_release` 的 sysroot。仅当目标机实测确定并记录了
使用不同或设备派生的 sysroot 的原因时，才传 `-DMANIFOLD3_ALLOW_UNVERIFIED_SYSROOT=ON`。

## Phase 5 sysroot 扩展

Phase 5 推理构建需要 CUDA Toolkit 与 TensorRT 开发文件。它们从妙算 3 设备固件（运行时权威）按相同相对路径
拷贝进 sysroot，使 `-I`/`-L` 与 `#include <cuda_runtime.h>` / `#include <NvInfer.h>` 能在 sysroot 前缀下解析。

### 设备包（记录于 2026-07-31）

脚本在设备（`dji@192.168.42.120`）上以 `dpkg-query -W -f='${Version}' <pkg>` 逐一核对 14 个包名；
任一包缺失或版本不同即在任何拷贝前硬失败：

| 包 | 版本 | 架构 |
|---|---|---|
| `cuda-cudart-11-4` | 11.4.298-1 | arm64 |
| `cuda-cudart-dev-11-4` | 11.4.298-1 | arm64 |
| `libcudla-11-4` | 11.4.298-1 | arm64 |
| `libcudla-dev-11-4` | 11.4.298-1 | arm64 |
| `libcublas-11-4` | 11.6.6.84-1 | arm64 |
| `libcublas-dev-11-4` | 11.6.6.84-1 | arm64 |
| `libcudnn8` | 8.6.0.166-1+cuda11.4 | arm64 |
| `libcudnn8-dev` | 8.6.0.166-1+cuda11.4 | arm64 |
| `libnvinfer8` | 8.5.2-1+cuda11.4 | arm64 |
| `libnvinfer-dev` | 8.5.2-1+cuda11.4 | arm64 |
| `libnvinfer-plugin8` | 8.5.2-1+cuda11.4 | arm64 |
| `libnvinfer-plugin-dev` | 8.5.2-1+cuda11.4 | arm64 |
| `libnvonnxparsers8` | 8.5.2-1+cuda11.4 | arm64 |
| `libnvonnxparsers-dev` | 8.5.2-1+cuda11.4 | arm64 |

与 "选定构建基线" 记录的 JetPack 5.1.3 基线（CUDA 11.4、TensorRT 8.5.2）一致。

### 组装步骤（连接设备）

妙算 3 固件是运行时权威，故 Phase 5 扩展从设备拷贝。`scripts/extend_sysroot_from_device.sh` 完成整个过程：
检查设备可达性与包版本、拷贝每个头文件与库、恢复设备符号链接布局。

拷贝先 staging 再落地：脚本用 `mktemp -d` 在 sysroot 内（同一文件系统，最终安装是 rename）创建暂存目录，
EXIT/INT/TERM 陷阱清理；安装前用 `scripts/check_inference_sysroot.sh` 校验暂存树。失败或中断的运行不会在
sysroot 留下残缺扩展；只有通过校验的暂存树会被安装，且只替换下述受管文件。

```bash
# 前置
# 1. 妙算 3 经 USB 连接，网络就绪（固定地址，见 AGENTS.md）
# 2. SSH 私钥权限：chmod 600 config/manifold3_id_rsa
# 3. 设备包版本与上表一致（脚本不匹配即硬失败）

scripts/extend_sysroot_from_device.sh              # 192.168.42.120, $MANIFOLD3_SYSROOT 或 ./sysroot
scripts/extend_sysroot_from_device.sh 10.0.0.5      # 其他地址
scripts/extend_sysroot_from_device.sh --sysroot /opt/m3-sysroot   # 显式 sysroot
# 预期最后两行：
#   PASS: inference sysroot extension present
#   DONE: sysroot extension applied to /opt/m3-sysroot
```

脚本是主路径；下表与符号链接布局记录它拷贝什么、如何链接，便于手工修复部分失败。设备不可用时，
可用 `dpkg-deb -x` 将上表同版本的 `.deb` 解压进 sysroot，仍需恢复符号链接。对受管文件重跑脚本幂等；
sysroot 其他内容不动。

### 拷贝的文件

| 源（设备） | 目标（sysroot） | 内容 |
|---|---|---|
| `/usr/include/aarch64-linux-gnu/NvInfer*.h` | `usr/include/aarch64-linux-gnu/` | TensorRT 头文件 |
| `/usr/include/aarch64-linux-gnu/NvOnnx*.h` | `usr/include/aarch64-linux-gnu/` | ONNX parser 头文件 |
| `/usr/local/cuda/include/`（整树） | `usr/local/cuda/include/` | CUDA toolkit 头文件（`cuda_runtime.h`、`cuda.h`、`crt/host_config.h`、…） |
| `/usr/lib/aarch64-linux-gnu/libnvinfer.so*` | `usr/lib/aarch64-linux-gnu/` | TensorRT dev 符号链接 + `libnvinfer.so.8.5.2` |
| `/usr/lib/aarch64-linux-gnu/libnvonnxparser.so*` | `usr/lib/aarch64-linux-gnu/` | ONNX parser dev 符号链接 + `libnvonnxparser.so.8.5.2` |
| `/usr/lib/aarch64-linux-gnu/libnvinfer_plugin.so*` | `usr/lib/aarch64-linux-gnu/` | TensorRT plugin dev 符号链接 + `libnvinfer_plugin.so.8.5.2`（来自 `libnvinfer-plugin-dev`） |
| `/usr/local/cuda/lib64/libcudart.so*` | `usr/local/cuda/lib64/` | CUDA 运行时 dev 符号链接 + `libcudart.so.11.4.298` |
| `/usr/local/cuda/lib64/libcudla.so*` | `usr/local/cuda/lib64/` | CUDA DLA 运行时 dev 符号链接 + `libcudla.so.1.0.0`（来自 `libcudla-11-4`） |
| `/usr/local/cuda/lib64/libcublas.so*` | `usr/local/cuda/lib64/` | cuBLAS dev 符号链接 + `libcublas.so.11.6.6.84`（来自 `libcublas-11-4`） |
| `/usr/local/cuda/lib64/libcublasLt.so*` | `usr/local/cuda/lib64/` | cuBLAS-Lt dev 符号链接 + `libcublasLt.so.11.6.6.84`（来自 `libcublas-11-4`） |
| `/usr/lib/aarch64-linux-gnu/libcudnn.so.8*` | `usr/lib/aarch64-linux-gnu/` | cuDNN 运行时 `libcudnn.so.8` -> `libcudnn.so.8.6.0`（来自 `libcudnn8`；文件名携带 8.6.0 子版本） |

设备上的无版本 `libcudnn.so` 是 alternatives 管理的链接（`/etc/alternatives/libcudnn_so ->
/usr/lib/aarch64-linux-gnu/libcudnn.so.8`），故**故意不**拷入 sysroot；请链接带版本名：`-l:libcudnn.so.8`。

`libnvdla_compiler.so` 与 `libnvdla_runtime.so` 已存在于 Phase 2 sysroot 的
`usr/lib/aarch64-linux-gnu/tegra/`，无需拷贝（已在设备与 sysroot 上核对大小一致）。

库符号链接链恢复为与设备布局完全一致（普通 `scp` 会解引用符号链接，故重复的完整拷贝被删除并重新链接）：

```text
libnvinfer.so -> libnvinfer.so.8.5.2
libnvinfer.so.8 -> libnvinfer.so.8.5.2
libnvonnxparser.so -> libnvonnxparser.so.8
libnvonnxparser.so.8 -> libnvonnxparser.so.8.5.2
libnvinfer_plugin.so -> libnvinfer_plugin.so.8.5.2
libnvinfer_plugin.so.8 -> libnvinfer_plugin.so.8.5.2
libcudart.so -> libcudart.so.11.0
libcudart.so.11.0 -> libcudart.so.11.4.298
libcudla.so -> libcudla.so.1
libcudla.so.1 -> libcudla.so.1.0.0
libcublas.so -> libcublas.so.11
libcublas.so.11 -> libcublas.so.11.6.6.84
libcublasLt.so -> libcublasLt.so.11
libcublasLt.so.11 -> libcublasLt.so.11.6.6.84
libcudnn.so.8 -> libcudnn.so.8.6.0
```

最小推理链接测试（`NvInfer.h` 运行时 + `cuda_runtime.h` + `cublas_v2.h`，链接 `-lnvinfer -lnvonnxparser
-lnvinfer_plugin -lcudart -lcudla -lcublas -lcublasLt -l:libcudnn.so.8`，DLA 编译器加
`-L <sysroot>/usr/lib/aarch64-linux-gnu/tegra`）闭合完整传递依赖链：无未定义引用、无
`--allow-shlib-undefined`。

### 校验

```bash
bash scripts/check_inference_sysroot.sh
# 预期: PASS: inference sysroot extension present
```

校验器只输出一行最终结论。经 `scripts/extend_sysroot_from_device.sh` 应用扩展时最终输出两行：

```text
PASS: inference sysroot extension present   （来自校验器）
DONE: sysroot extension applied to <abs sysroot>
```

被校验的 sysroot 按优先级解析：`--sysroot <path>` > `$MANIFOLD3_SYSROOT` > `<repo>/sysroot`：

```bash
bash scripts/check_inference_sysroot.sh --sysroot /opt/m3-sysroot
```

校验器检查：`NvInfer.h`、`NvOnnxParser.h`、`cuda_runtime.h`、`cuda.h`、`crt/host_config.h` 为常规非空文件；
每个受管真实库（`libnvinfer.so.8.5.2`、`libnvonnxparser.so.8.5.2`、`libnvinfer_plugin.so.8.5.2`、
`libcudnn.so.8.6.0`、`libcudart.so.11.4.298`、`libcudla.so.1.0.0`、`libcublas.so.11.6.6.84`、
`libcublasLt.so.11.6.6.84`）为常规非空 AArch64 ELF 对象且 SONAME 与设备完全一致；每个 dev 符号链接
是符号链接（`-L`）且 `readlink` 目标与设备完全一致；每条链接链都能解析到存在的文件；无版本 `libcudnn.so`
不存在（`[ -e ] || [ -L ]`，悬空链接同样拒绝）。

> 注意：此扩展取代上表 "CUDA Toolkit" 与 "TensorRT" 两行推迟项的**开发文件**部分。cuDNN 运行时库已存在
> （链接闭合）；cuDNN 头文件在推理代码直接调用 cuDNN API 前继续推迟。

## 开发凭据

PSDK 只有使用真实 DJI 开发者凭据（App ID、App Key、App License、开发者账号）才能连接飞机。DJI 不提供官方
示例凭据；PSDK 示例 `app.json` 中的 `164884` 仅是格式示例，无对应密钥。

凭据在构建时注入，绝不入库：

```bash
cmake --preset manifold3-cross-release \
    -DMANIFOLD3_APP_ID=<id> \
    -DMANIFOLD3_APP_KEY=<key> \
    -DMANIFOLD3_APP_LICENSE=<license> \
    -DMANIFOLD3_APP_NAME=<name> \
    -DMANIFOLD3_DEVELOPER_ACCOUNT=<account>
```

默认值是 `your_app_*` 占位符。应用启动时拒绝占位符并报错退出（退出码 1）；`app.json` 由同一 CMake 变量生成，
保证 `user_app_id` 与编译进应用的一致（DPK 安装要求）。DPK 打包/安装/启动/停止/卸载不需要有效凭据；
只有飞机连接需要。

### 本地凭据文件（可选便利）

日常开发可把真实凭据写入 git 忽略的 `.local/credentials.env`（权限 600，绝不提交；`.local/` 在 `.gitignore`）。
它导出 `MANIFOLD3_APP_ID`、`MANIFOLD3_APP_KEY`、`MANIFOLD3_APP_LICENSE`、`MANIFOLD3_APP_NAME` 与
`MANIFOLD3_DEVELOPER_ACCOUNT`，再一步配置交叉预设：

```bash
scripts/configure_cross_with_credentials.sh
```

文件缺失时脚本以占位符配置并打印警告。手动 `-D` 调用仍是主要、显式路径；帮助脚本只是其便利封装。

## 链接策略

最小 PSDK 应用链接：

```text
third_party/psdk/psdk_lib/lib/aarch64-linux-gnu-gcc/libpayloadsdk.a
-pthread（链接选项，非 -lpthread）
libm
libdl
```

依赖处理分三组：

| 依赖类型 | 策略 |
|---|---|
| PSDK | 链接随附的 AArch64 `libpayloadsdk.a` |
| 普通第三方库 | 优先静态库，以符合妙算 3 DPK 指引 |
| CUDA、TensorRT 与驱动栈 | 链接匹配的开发文件，并复用妙算 3 固件安装的兼容运行时库 |

静态 `libstdc++` 与 `libgcc` 不是默认项；仅在目标机测试暴露真实 `GLIBCXX_*` 兼容问题时才评估。

## 运行时验证

目标产物被接受前，用以下命令检查：

```bash
file <binary>
readelf -h <binary>
readelf -d <binary>
readelf --version-info <binary>
# 部署到妙算 3 后运行：
ldd <binary>
```

必须确认：

- ELF 架构为 AArch64；
- 未链接任何主机 x86_64 路径或库；
- 目标机上可用的 `GLIBC_*` 与 `GLIBCXX_*` 版本满足要求；
- CXX 依赖集包含 `libm.so.6`：Phase 5 sysroot 内置设备派生的 `libstdc++.so.6`，其自身 `DT_NEEDED`
  含 `libm.so.6`，链接器会把该继承依赖传播到每个 C++ 二进制（见 `tests/toolchain/verify_elf.cmake`）；
- CUDA 与 TensorRT 的 SONAME 与目标固件匹配；
- 不存在意外的绝对 RPATH/RUNPATH；
- 程序在直连部署与 DPK 应用环境两种方式下都能启动。

## 推迟的决策

以下决策需要实现证据，刻意不由脚手架固定：

- 预处理是否需要 OpenCV、VPI 或自定义 CUDA 内核；
- H.264 解码是否需要 FFmpeg 或 GStreamer；
- TensorRT engine 在主机还是妙算 3 上生成；
- 生产环境选择 GPU、DLA、FP16 还是 INT8；
- 是否有妙算 3 固件库必须覆盖标准 r35.5.0 sysroot。
