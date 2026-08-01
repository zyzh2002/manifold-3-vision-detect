# Capture Demo - 手动演示说明

## 前置条件

- 无人机已开机，Pilot 已连接（PSDK 需要飞机在线）
- Manifold 3 通过 USB 连接开发主机（IP 192.168.42.120）
- 演示分支 `demo/capture-demo`（含 `capture_demo` 可执行文件，已在设备 `~/vision-detect/capture_demo`）

## 演示流程（在开发主机上执行）

### 1. 停掉占用 PSDK 通道的 Smart3DExplore

```bash
ssh -i config/manifold3_id_rsa -o StrictHostKeyChecking=no dji@192.168.42.120 \
  "dji_app_ctl stop Smart3DExplore; pkill -f Smart3DExplore 2>/dev/null; sleep 1"
```

> 若 `dji_app_ctl stop` 报错 257 属正常，`pkill` 兜底会生效。

### 2. 运行 demo（终端画面 + 统计）

```bash
ssh -t -i config/manifold3_id_rsa -o StrictHostKeyChecking=no dji@192.168.42.120 \
  "cd ~/vision-detect && ./capture_demo"
```

效果：终端顶部实时显示 NV12 缩略图（100x25 字符，约 10 fps 刷新），下方每秒一行统计：
`fps / size / source_drop / handoff_drop / invalid / waited / rendered / rss_kb`

`Ctrl-C` 干净退出。

### 3. 可选参数

| 参数 | 说明 |
|---|---|
| `--no-render` | 只显示统计，不渲染画面（纯文本终端用） |
| `--grid=WxH` | 调整渲染网格字符数（默认 100x25） |

### 4. 演示结束恢复设备

```bash
ssh -i config/manifold3_id_rsa -o StrictHostKeyChecking=no dji@192.168.42.120 \
  "dji_app_ctl start Smart3DExplore"
```

## 常见问题

| 现象 | 原因与处理 |
|---|---|
| 提示 "Address already in use" | Smart3DExplore 未停干净，重跑第 1 步 |
| "PSDK credentials not configured" | 凭据未注入，先运行 `scripts/configure_cross_with_credentials.sh` 再重新构建部署 |
| 终端画面不显示/乱码 | 终端不支持 ANSI truecolor，加 `--no-render` 只展示统计 |
| 统计行 fps=0 持续 | 飞机/Pilot 不在线，确认无人机开机并连接 Pilot |

## 重新构建部署（如需重编）

```bash
# 在 demo/capture-demo 分支上
source scripts/setup_env.sh
scripts/configure_cross_with_credentials.sh   # 注入真实凭据
cmake --build --preset manifold3-cross-release --target capture_demo
scp -i config/manifold3_id_rsa -o StrictHostKeyChecking=no \
  build-cross/src/app/capture_demo dji@192.168.42.120:~/vision-detect/
```

## 代码位置

- `src/app/capture_demo.cpp`（demo 分支新增，`main.cpp` 未改动）
- `src/app/CMakeLists.txt` 末尾新增 `capture_demo` 目标（仅 cross 构建，无 TensorRT 依赖）
