# 项目状态

本文汇总各阶段完成状态与目标机（Manifold 3）验证记录。路线图见 [roadmap.md](roadmap.md)；AI 侧的完整里程碑计划与退出标准见 `.agents/docs/plan.md`（英文）。

## 阶段概览

| 阶段 | 内容 | 状态 |
|---|---|---|
| Phase 1 | 仓库骨架、PSDK 子模块、基线文档 | ✅ 完成 |
| Phase 2 | 可复现的交叉编译（工具链 + sysroot + ELF 验证） | ✅ 完成 |
| Phase 3 | 最小 PSDK 生命周期与 DPK 应用 | ✅ 完成 |
| Phase 4 | 单路视频采集（NV12） | ✅ 完成 |
| Phase 5A | 合成 TensorRT 推理管线 | ✅ 完成 |
| Phase 5B | 接入真实 YOLO11-seg 模型 | ⏳ 待办 |
| Phase 6 | 产品输出与 DPK 打包发布 | ⏸ 待办 |

## 各阶段要点与验证记录

### Phase 2 — 交叉编译（已验证）

- NVIDIA Bootlin GCC 9.3.0 工具链（SHA256 `7725b460...`）+ Jetson Linux r35.5.0 sysroot。
- C/C++ 冒烟程序在妙算 3 上运行通过；目标环境与基线一致：
  Jetson Linux R35.5.0、kernel 5.10.192-tegra、glibc 2.31、CUDA 11.4.19、TensorRT 8.5.2、cuDNN 8.6.0。
- 动态依赖全部解析；`GLIBC_2.17` 需求满足；无需 sysroot overlay。

### Phase 3 — PSDK 生命周期（已验证，除 DPK 生命周期）

- platform 层移植（OSAL / FS / Socket / USB Bulk）、`src/core/` 最小生命周期、`src/app/` 入口已完成。
- 二进制在目标机运行通过；占位凭据被拒绝并返回退出码 1（按设计）。
- 开发 DPK 可构建（`manifold3-vision-detect_v01.00.00.00.dpk`）。
- DPK 安装/启动/停止/更新/卸载生命周期**推迟到 Phase 6**：需要 DJI Pilot 2 开发者流程与真实开发者凭据。
  开发迭代使用 `scripts/deploy.sh` 直连部署。

### Phase 4 — 单路视频采集（已验证）

- `DjiLiveview_StartImageStream()`（可见光源 `DJI_LIVEVIEW_CAMERA_SOURCE_M4T_VIS`，NV12）稳定输出
  **1440x1080 @ 30fps**，2,332,800 字节/帧。
- 观测 1251 帧 **0 丢帧**，`frameId` 连续；回调间隔 min 1.8ms / avg 33.3ms / max 42.6ms。
- 进程 RSS 稳定在 ~73 MB，无无界增长。
- 设计：回调缓冲不跨回调保留；`LatestFrameSlot` latest-wins 有界移交 + 三类丢帧计数（源/移交/无效）。
- H.264 采集路径（录像与回退能力）**尚未验证**。

### Phase 5A — 合成推理管线（已验证，合成引擎）

- CUDA/TensorRT/cuDNN sysroot 扩展：`scripts/extend_sysroot_from_device.sh`（staging 安装、版本硬校验）、
  `scripts/check_inference_sysroot.sh`（链接/SONAME/ELF 校验）。
- CPU 预处理 NV12→1280x1280 NCHW；合成 FP16 engine 按名契约加载（`synthetic_engine_contract.h`）；
  YOLO11-seg 形状后处理解码；逐秒分阶段指标（avg/p95/max）。
- 目标机实测（658 窗口 / ~11 分钟，~30fps）：
  - 端到端 avg ~26.2ms、p95 ~27.3ms、max ~30ms；
  - 各阶段 avg/p95：pre 12.8/13.0ms、h2d 6.6/6.8ms、exec 1.26/1.3ms、d2h 4.4/4.5ms、eng 12.2/12.9ms、post 0.15/0.16ms；
  - source_drop=0、invalid=0；handoff_drop 恒定 3/窗口；
  - RSS 614,588→634,132 kB（~1.8 MB/min 缓慢增长，归因于驱动/SDK 惰性分配，无应用泄漏路径）；
  - SIGTERM 干净退出。
- ⚠️ **以上均为合成 dummy 引擎数据，不代表真实模型性能**；合成运行 detections=0 属于预期
  （随机权重输出低于 0.25 置信度阈值），不验证检测正确性。

## 已知注意事项

- 运行应用前必须停止机上 `Smart3DExplore`（`dji_app_ctl stop Smart3DExplore`），否则 local channel 绑定报
  "Address already in use"；结束后恢复 `dji_app_ctl start Smart3DExplore`。
- 仓库不提交开发凭据；占位凭据启动即报错退出。真实凭据放 git 忽略的 `.local/credentials.env`（权限 600）。
- 目标环境实测为准：妙算 3 固件实际版本以 `cat /etc/nv_tegra_release` 为准。

## 下一步（Phase 5B）

- 冻结真实 YOLO11-seg 模型 ABI（标准 2 输出或自定义多任务契约）；
- 匹配 PC 与设备端预处理（resize、padding、颜色约定）；
- 实现 box 的逆 letterbox 几何与源帧实例 mask（crop、upscale、unpad）；
- 与 ONNX Runtime 做数值对比；重测真实模型延迟/吞吐/内存/丢帧。

详细内容见 [roadmap.md](roadmap.md) 与 `.agents/docs/plan.md`。
