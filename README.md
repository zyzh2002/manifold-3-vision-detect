# manifold-3-vision-detect

基于妙算 3 的无人机视觉检测系统：从 Matrice 4T 相机实时截取视频流，在端侧运行 AI 模型进行目标检测与识别。

## 硬件栈

| 组件 | 型号 / 规格 |
|---|---|
| 机载计算机 | 妙算 3（Manifold 3）— 100 TOPS（60 GPU + 40 DLA），16GB LPDDR5，256GB SSD |
| 无人机 | Matrice 4T（可见光 + 红外） |
| 接口 | E-Port V2，PSDK 核心链路使用 USB Bulk；Liveview/数据通道使用 Linux socket/network 抽象；24V 供电 |
| 系统环境 | NVIDIA JetPack 5.1.3 / Ubuntu 20.04 / aarch64 / glibc 2.31 / kernel 5.10 |
| 妙算 3 固件 | v17.00.01.01（2026-03-31） |
| 原生编译器 | gcc 9.4.0（Ubuntu 20.04 默认，支持 C++17） |

## 软件栈

| 组件 | 版本 | 用途 |
|---|---|---|
| DJI PSDK | 3.16.0（git submodule，pinned tag） | 无人机通信与视频流 API |
| 交叉编译工具链 | crosstool-ng 构建：gcc 11.5.0 + glibc 2.31 + kernel 5.10 headers（详见下方说明） | 在 x86_64 主机上编译 aarch64 产物 |
| 推理框架 | TensorRT 8.5.2 / CUDA 11.4.19（JetPack 5.1.3 基线，Phase 3） | 端侧模型推理 |
| 构建系统 | CMake | 交叉编译 |

## 仓库布局

```
.
├── README.md
├── AGENTS.md                  # Agent instructions (English only)
├── .gitignore
├── .gitmodules
├── cmake/                     # CMake toolchain files (Phase 2+)
├── third_party/
│   └── psdk/                  # dji-sdk/Payload-SDK git submodule (read-only)
├── toolchain/
│   └── crosstool-ng/          # crosstool-ng config (Phase 2+)
├── config/                    # App manifest & compile-time config (Phase 2+)
├── src/
│   ├── app/                   # Entry point (Phase 2+)
│   ├── core/                  # PSDK lifecycle wrapper (Phase 2+)
│   ├── capture/               # Video stream capture abstraction layer (Phase 2+)
│   ├── platform/              # Manifold 3 HAL/OSAL port (vendored from PSDK sample)
│   └── inference/             # TensorRT/CUDA model inference (Phase 3+)
├── scripts/                   # Build / deploy / package scripts (Phase 2+)
├── docs/
│   ├── plan.md                # Phased implementation plan
│   └── architecture.md        # Architecture & data flow
└── tests/                     # Unit tests (Phase 2+)
```

## 为何交叉编译工具链使用 gcc 11.5.0 而非目标原生 gcc 9.4.0？

妙算 3 的原生系统（JetPack 5.1.3 / Ubuntu 20.04）默认 gcc 为 **9.4.0**，其 `libstdc++.so.6` 仅支持到 `GLIBCXX_3.4.28`。若交叉编译工具链使用更高版本的 gcc 且动态链接 libstdc++，产物会在妙算 3 上因缺少符号而无法运行。

gcc 9 已提供非实验性的 C++17 实现，`std::filesystem` 也不需要额外链接 `-lstdc++fs`。选择 **gcc 11.5.0** 不是因为 gcc 9 缺少完整 C++17，而是为了统一较新的主机构建工具链和诊断能力。

该选择必须结合妙算 3 实机环境验证：

1. **以目标 sysroot 为准** — glibc、系统头文件以及 CUDA、TensorRT、OpenCV 和多媒体库必须来自与目标固件匹配的 JetPack 5.1.3 sysroot；仅构建 crosstool-ng 工具链并不足以编译完整推理程序
2. **检查实际符号依赖** — 使用 `readelf`、`ldd` 和目标机运行测试确认 glibc、libstdc++ 与 NVIDIA 共享库兼容性
3. **静态 C++ 运行库是可选策略** — `-static-libstdc++ -static-libgcc` 可以减少对目标 libstdc++ 的依赖，但并非 DPK 格式要求，也不能静态替代 CUDA、TensorRT 等驱动相关共享库
4. **保留回退方案** — 如果交叉编译依赖闭环不稳定，Phase 3 可在妙算 3 上原生构建 TensorRT 部分，或改用官方 JetPack 交叉编译包和 Ubuntu 20.04 构建环境

## 快速上手

### 前置条件

- 主机：x86_64 Linux；PSDK 基础交叉编译可使用常规发行版，JetPack/CUDA 交叉编译优先采用 NVIDIA 支持的 Ubuntu 20.04 环境或容器
- 妙算 3：已安装 JetPack 5.1.3 并可通过 SSH 访问
- DJI 开发者账号：已在 [developer.dji.com](https://developer.dji.com) 创建应用并获取 `app_id` / `app_key` / `app_license`

### 初始化仓库

```bash
git clone --recurse-submodules <repo-url>
cd manifold-3-vision-detect
```

或已克隆后：

```bash
git submodule update --init --recursive
```

### 构建工具链（Phase 2）

参见 `toolchain/crosstool-ng/README.md`（待创建）。

### 编译与部署（Phase 2+）

```bash
export MANIFOLD3_TOOLCHAIN_DIR=/opt/x-tools/aarch64-manifold3-linux-gnu
./scripts/build.sh
./scripts/deploy.sh <manifold3-ip>
```

## 路线图

详见 [docs/plan.md](docs/plan.md)。

## VibeCoding 环境配置

本项目使用 [DeepWiki](https://deepwiki.com) 和 [Exa](https://exa.ai) MCP 服务器辅助 PSDK API 查询。

### MCP 服务器配置

在任意支持 MCP 的编辑器或 AI 工具中，添加 HTTP 类型的 MCP 服务器：

```json
{
    "mcpServers": {
        "deepwiki": {
            "type": "remote",
            "url": "https://mcp.deepwiki.com/mcp",
            "enabled": true
        },
        "exa": {
            "type": "remote",
            "url": "https://mcp.exa.ai/mcp",
            "enabled": true
        }
    }
}
```

## 许可

本项目代码部分遵循 [GNU General Public License v3.0](LICENSE)。PSDK 子模块（`third_party/psdk/`）遵循 DJI Payload SDK 许可条款。
