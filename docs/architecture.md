# 系统架构

本文从人类可读的视角描述系统的边界、数据流、模块划分与关键接口。环境与构建细节见
[build-environment.md](build-environment.md)。

## 系统边界

应用运行在 DJI 妙算 3（Manifold 3）上，通过 DJI Payload SDK（PSDK）从 Matrice 4T 相机接收数据，
并使用 JetPack 提供的 TensorRT 运行时执行目标检测推理。

```text
Matrice 4T camera
    |
    | E-Port V2 与 PSDK 平台服务
    v
Manifold 3
    |
    +-- 平台适配（platform）
    +-- PSDK 核心生命周期（core）
    +-- 实时视频采集（capture）
    +-- 有界帧移交（LatestFrameSlot）
    +-- TensorRT 推理（inference）
    +-- 产品输出（按实测选择，见文末）
```

## 主要数据流

初始实现采用解码后的图像流（PSDK ImageStream），因为妙算 3 直接暴露该路径，可避免初期引入 H.264 解码依赖：

```text
DjiLiveview_StartImageStream
    -> NV12 回调缓冲
    -> 拷贝/移交到有界的应用自有存储
    -> 预处理（NV12 -> NCHW）
    -> TensorRT 推理
    -> 结构化检测结果
```

关键约束：

- 采集层在返回回调前将数据拷贝进应用自有存储（除非目标机验证出其他安全的移交语义）。
- 昂贵的预处理/推理**不**运行在 PSDK 回调线程中。
- 消费者落后时，有界移交按显式策略丢弃，而不是无界增长内存。

## 次要 H.264 路径

`DjiLiveview_StartH264Stream()` 用于录像与回退能力验证，不作为初始推理输入（否则需额外解码依赖与缓存级）。
仅当以下任一情况成立时，H.264 才成为推理输入：

- 所需相机源或模式下解码 ImageStream 不可用；或
- 产品必须保留/处理压缩流以满足已验证的需求。

## 模块边界

| 模块 | 职责 | 直接依赖 |
|---|---|---|
| `src/platform/` | 注册 Linux OSAL、日志、文件系统、Socket、妙算 3 USB Bulk 处理器 | PSDK 平台头文件与 Linux API |
| `src/core/` | 负责 PSDK 初始化、应用启动、就绪与有序关闭 | `src/platform/`、PSDK 核心 API |
| `src/capture/` | 启停一路选定的 Liveview 源，通过有界接口暴露自有帧 | `src/core/`、PSDK Liveview API |
| `src/inference/` | 预处理帧、执行 TensorRT、返回结构化检测结果 | TensorRT、所需 CUDA API |
| `src/app/` | 选择配置，串联采集、推理与输出 | 所有应用模块 |

### 已落地的关键接口（基于目标机实测数据）

- `src/capture/`：
  - `LatestFrameSlot`（`Push` / `WaitTake` / `Stop`），latest-wins 有界移交；
  - `OwnedNv12Frame`（`data` / `width` / `height` / `frame_id`）；
  - `FramePushResult`（`kStored` / `kReplaced` / `kInvalid`）；`IsValidNv12Frame` 拒绝空、奇宽高或尺寸不符的缓冲；
    `kReplaced` 计移交丢弃，`kInvalid` 计无效帧；
  - `LiveviewCapture`（`Initialize` / `Start` / `Stop` / `Shutdown`）与统计快照
    （`source_dropped_frames`、`handoff_dropped_frames`、`invalid_frames`、间隔百分位）。
- `src/inference/`：
  - 纯 C++ 预处理 `PreprocessNv12ToNchw`；
  - TensorRT 封装 `TensorRtEngine::Load` / `Infer`，按名/类型/形状强制 `synthetic_engine_contract.h`
    中的合成三输出契约；
  - 解码器 `DecodeSyntheticSeg` 与逐窗口指标 `PipelineWindowStats`；
  - 结果模式位于 `inference_types.h`（`Detection`：species_id、age_class_id、confidence、cx/cy/w/h、mask_rle）。
- `src/app/`：串联 采集 → 预处理 → 推理 → 解码，每秒输出一行分阶段指标；消费者不阻塞 PSDK 回调线程。

## 构建边界

```text
x86_64 Linux 主机
    |
    +-- NVIDIA Bootlin GCC 9.3.0
    |
    +-- Jetson Linux r35.5.0 Phase 2 基础 sysroot
    |     +-- BSP 与示例 rootfs
    |     +-- NVIDIA 二进制 overlay 与 Tegra 运行时库
    |
    +-- Phase 5 sysroot 扩展
    |     +-- CUDA 11.4 开发文件
    |     +-- TensorRT 8.5.2 与 cuDNN 开发文件
    |
    +-- PSDK 3.16.0 头文件与 AArch64 libpayloadsdk.a
    |
    +-- AArch64 应用
          -> 直连目标机验证
          -> DPK 打包
```

标准 r35.5.0 sysroot 是构建基线；妙算 3 固件是运行时权威。仅当实测出具体不匹配时才以文档记录的方式
从设备覆盖文件。版本、链接与验证规则见 [build-environment.md](build-environment.md)。

## 错误与关闭模型

- 平台注册或 PSDK 核心初始化失败 ⇒ 应用无法启动。
- 采集启动失败 ⇒ 推理保持停止并报告具体 PSDK 返回码。
- 无效帧元数据或不支持的格式 ⇒ 在进入推理前被拒绝。
- 推理错误不阻塞 PSDK 回调线程：应用记录错误并在回调外执行配置的停止/重试策略。
- 关闭顺序：先停采集，排空或丢弃有界帧，销毁推理资源，反初始化 Liveview，再按逆序拆除其余 PSDK 生命周期。

## 推迟的产品输出

推理管线初期只产生本地结构化检测结果。后续里程碑将依据实际需求与已验证的 PSDK 支持选择一种产品输出：

- Pilot AI 元数据；
- 处理后的 H.264 视频；
- 文件或本地 IPC 输出；
- 应用环境允许的网络传输。

在选择之前，不引入任何输出相关的 PSDK 相机模拟或编码器生命周期。
