# Stream Demo - 手动演示说明

## 前置条件

- 无人机已开机，Pilot 已连接（PSDK 需要飞机在线）
- Manifold 3 通过 USB 连接开发主机（IP 192.168.42.120）
- 演示分支 `demo/capture-demo`（含 `stream_demo` 可执行文件，部署到设备 `~/vision-detect/stream_demo`）

## 演示流程

### 1. 停掉占用 PSDK 通道的 Smart3DExplore

```bash
ssh -i config/manifold3_id_rsa -o StrictHostKeyChecking=no dji@192.168.42.120 \
  "dji_app_ctl stop Smart3DExplore 2>/dev/null || true"
ssh -i config/manifold3_id_rsa -o StrictHostKeyChecking=no dji@192.168.42.120 \
  "pkill -f '[S]mart3DExplore' 2>/dev/null || true; sleep 1"
```

> 若 `dji_app_ctl stop` 报错 257 属正常，`pkill` 兜底会生效。

### 2. 运行 demo（MJPEG 推流 + 统计）

```bash
ssh -i config/manifold3_id_rsa -o StrictHostKeyChecking=no dji@192.168.42.120 \
  "cd ~/vision-detect && ./stream_demo"
```

效果：设备在 8081 端口提供 MJPEG 流。开发主机浏览器打开
`http://192.168.42.120:8081/`，F11 全屏观看（1440x1080 全彩，约 25 fps）。
SSH 会话每秒一行统计：
`fps / size / source_drop / handoff_drop / invalid / enc_frames / enc_fail / clients / avg_encode_ms / avg_interval_ms / rss_kb`

`Ctrl-C` 干净退出。

### 3. 可选参数

| 参数 | 说明 |
|---|---|
| `--port=8081` | 推流端口（默认 8081；设备 8080 被系统服务占用） |
| `--quality=80` | JPEG 质量 1..100（默认 80） |
| `--max-fps=25` | 最大帧率 1..60（默认 25） |
| `--scale=0.89` | 输出缩放（默认 1.0；0.89 输出约 1280x960） |

### 4. 演示结束恢复设备

```bash
ssh -i config/manifold3_id_rsa -o StrictHostKeyChecking=no dji@192.168.42.120 \
  "dji_app_ctl start Smart3DExplore"
```

## 常见问题

| 现象 | 原因与处理 |
|---|---|
| 浏览器画面卡住/空白 | 等 1-2 秒自动恢复（MJPEG 丢帧后浏览器等待下一帧）；确认飞机在线 |
| 启动报 "bind failed on port 8081" | 端口被占用，检查残留进程或换其他端口 |
| 提示 "Address already in use"（PSDK） | Smart3DExplore 未停干净，重跑第 1 步 |
| "PSDK credentials not configured" | 凭据未注入，先运行 `scripts/configure_cross_with_credentials.sh` 再重新构建部署 |
| 统计行 fps=0 持续 | 飞机/Pilot 不在线，确认无人机开机并连接 Pilot |

## 重新构建部署（如需重编）

```bash
# 在 demo/capture-demo 分支上
source scripts/setup_env.sh
scripts/configure_cross_with_credentials.sh   # 注入真实凭据
cmake --build --preset manifold3-cross-release --target stream_demo
scp -i config/manifold3_id_rsa -o StrictHostKeyChecking=no \
  build-cross/src/app/stream_demo dji@192.168.42.120:~/vision-detect/
```

## 代码位置

- `src/app/stream_demo.cpp`（demo 分支新增，`main.cpp` 未改动）
- `src/stream/`（`mjpeg_streamer` 推流实现；`yuv_to_rgb`/`mjpeg_framing` 有宿主单元测试）
- `src/app/CMakeLists.txt` 末尾新增 `stream_demo` 目标（仅 cross 构建，无 TensorRT 依赖）
