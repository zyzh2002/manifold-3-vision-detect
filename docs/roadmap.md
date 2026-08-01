# 路线图

项目按里程碑推进，每个阶段以「可在妙算 3 上运行并验证」为产出目标。当前进度见 [project-status.md](project-status.md)；
AI 侧的完整计划（含每个阶段的退出标准与验证记录）见 `.agents/docs/plan.md`（英文）。

## 阶段总览

```text
Phase 1-2  脚手架与可复现交叉编译          ✅
Phase 3    最小 PSDK 生命周期与 DPK 应用    ✅
Phase 4    单路视频采集                     ✅
Phase 5    板端推理
  ├─ 5A    合成 TensorRT 管线（验证链路）   ✅
  └─ 5B    真实 YOLO11-seg 模型             ⏳ 当前
Phase 6    产品输出与 DPK 发布              ⏸
```

## Phase 1-2 — 脚手架与可复现交叉编译（已完成）

- 搭建仓库结构、锁定 PSDK 3.16.0 子模块、记录基线。
- 以 NVIDIA Bootlin GCC 9.3.0 + Jetson Linux r35.5.0 sysroot 产出最小 AArch64 程序并在妙算 3 上运行。
- 建立 ELF 静态校验（架构、动态依赖、GLIBC/GLIBCXX 版本）与目标机运行校验。

## Phase 3 — 最小 PSDK 生命周期与 DPK 应用（已完成）

- 移植 Manifold 3 所需的 OSAL / FS / Socket / USB Bulk 平台适配。
- 实现最小 PSDK 生命周期、应用入口、开发 DPK 打包。
- 采用 `scripts/deploy.sh` 直连部署作为日常迭代方式；完整 DPK 生命周期验证推迟至 Phase 6。

## Phase 4 — 单路视频采集（已完成）

- 验证 `DjiLiveview_StartImageStream()` NV12 输出（1440x1080 @ 30fps，0 丢帧）。
- 落地有界帧移交 `LatestFrameSlot` 与丢帧/无效帧计数，回调缓冲不跨回调保留。
- H.264 采集作为录像与回退能力，另行验证。

## Phase 5 — 板端推理（进行中）

**Phase 5A（已完成）**：用合成 dummy engine 打通「采集 → 预处理 → 推理 → 后处理」全链路并完成目标机指标测量，
验证 CPU 预处理、TensorRT 引擎封装、YOLO11-seg 形状后处理与逐秒分阶段指标。合成数据不代表真实模型性能。

**Phase 5B（当前，待办）**：接入真实 YOLO11-seg 模型——

1. 冻结真实模型 ABI（标准 2 输出或自定义多任务契约）；
2. 匹配 PC 与设备端预处理（resize、padding、颜色约定）；
3. 实现 box 的逆 letterbox 几何与源帧实例 mask；
4. 与 ONNX Runtime 数值对比；
5. 重测真实模型延迟/吞吐/内存/丢帧，再标记 Phase 5 完成。

## Phase 6 — 产品输出与 DPK 发布（待办）

- 依据产品流程选择输出：本地结构化结果、Pilot AI 元数据、处理后的视频或其他传输。
- 审计所有运行时依赖（固件自带 / 静态链接 / 打包数据）。
- 新增 `scripts/package_dpk.sh` 作为发布封装。
- 完成 DPK 安装/启动/停止/更新/卸载/日志/数据清理生命周期验证（需 DJI Pilot 2 开发者流程与真实凭据）。
- 记录受支持的固件、机型、相机源、模型与性能边界。

## 决策触发条件

以下决策需要实测证据，不在早期阶段预先固定：

| 决策 | 触发条件 |
|---|---|
| 引入 FFmpeg / GStreamer | ImageStream 不可用，或产品需以 H.264 作为模型输入 |
| 引入 OpenCV / VPI | 实测预处理复杂度或性能超出预算 |
| 编写自定义 `.cu` 内核 | 初始预处理路径无法达到实测延迟目标 |
| 使用 crosstool-ng | NVIDIA Bootlin 工具链存在已记录且阻塞构建的限制 |
| 静态链接 libstdc++/libgcc | 目标机验证出现真实 GLIBCXX 兼容问题 |
| 从妙算 3 覆盖文件 | 实测与 r35.5.0 sysroot 存在 ABI 或依赖差异 |
| 使用 DLA / FP16 / INT8 | 模型精度、吞吐与功耗测试显示收益 |
| 支持多路相机流 | 单路采集与推理稳定且硬件能力测试通过 |
| 向 Pilot 发送 AI 元数据/视频 | 产品流程需要 Pilot 展示且对应 PSDK 路径已验证 |
