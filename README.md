# hk-recording-service

海康 NVR 历史录像按时间段下载服务。C++17 / cpp-httplib / HCNetSDK / FFmpeg / spdlog / Ubuntu 24.04。只提供录像下载，不提供截图、查询、录像计划或异步任务管理。工程参考 `hk-snapshot-service`，回放协议和时间戳经验参考 `nvr-recording-service` 的 `src/media-engine/download/recording_download.*`；本项目重新组织并实现直接 MP4 封装。

## 启动

源码包不包含海康专有 SDK 二进制。使用你已拥有的 `hk-snapshot-service` 仓库（Linux x86_64 版本 SDK）：

```bash
# 在本项目根目录执行；如果本机已有参考仓库，可跳过 clone
 git clone https://github.com/qishuhua/hk-snapshot-service.git ../hk-snapshot-service
 ./scripts/import_sdk.sh ../hk-snapshot-service
 cp .env.example .env
 docker compose up -d --build
 curl http://127.0.0.1:8181/healthz
```

SDK 必须完整包含 `third_party/HCNetSDK/include/HCNetSDK.h`、`lib/libhcnetsdk.so` 及 `lib/HCNetSDKCom/` 等配套库。默认仅发布到宿主机 127.0.0.1；局域网使用需修改 `HKMEDIA_BIND_IP` 并配置可信网络、API Token 和 TLS 网关。容器运行 UID 10001，命名卷存储日志和匿名临时文件，不要把设备密码写入日志。

访问 `http://127.0.0.1:8181/` 可进行网页下载测试。

## API

`POST /recordings`，`Content-Type: application/json`。成功直接返回 `video/mp4` 文件，不返回 JSON 任务 ID。

```bash
curl --fail-with-body -sS --max-time 180 \
  -X POST http://127.0.0.1:8181/recordings \
  -H 'Content-Type: application/json' \
  -d '{"host":"192.168.1.64","port":8000,"username":"admin","password":"xxx","channel":33,"start_time":"2026-11-25 10:00:00","end_time":"2026-11-25 10:01:00","stream_type":0,"timeout_ms":120000}' \
  -D headers.txt -o recording.mp4
```

配置 `HKMEDIA_API_TOKEN` 后增加请求头 `Authorization: Bearer <token>`。HTTP 错误响应是 JSON，使用 curl 的 `--fail-with-body` 时仍可能将错误写入输出文件，请检查退出状态。

| 参数 | 类型 | 默认 / 说明 |
|---|---|---|
| host | string | 必填，NVR 地址 |
| port | int | 8000 |
| username / password | string | 必填，密码可为空 |
| channel | int | 1，海康实际 SDK 通道号 |
| start_time / end_time | string | 必填，设备本地时间，秒精度；空格或 T 分隔，不接受时区后缀 |
| stream_type | int | 0 主、1 子、2 三码流 |
| timeout_ms | int | 默认 120000，1000–14400000 |

响应头 `X-Request-Id`、`X-Recording-Start-Time`、`X-Recording-End-Time`（实际首末帧 SDK 日历时间）、`X-Recording-Duration-Ms`、`X-Recording-Frames`、`X-Recording-Resolution`、`X-Recording-Codec`、`X-Recording-Elapsed-Ms`。时间范围由设备回放接口决定，可能从请求起点之前的 I 帧开始或与结束时间有偏差；不保证逐帧精确裁剪。OSD 不参与媒体 PTS 计算。

失败：`{"error":{"code":"...","message":"..."}}`。400 参数错误，401 鉴权失败，404 没有可独立解码的录像帧，429 并发已满，501 不支持的码流/参数变化，502 SDK 或媒体错误，503 ES 队列失败，504 超时，507 文件容量或存储错误。回放打开失败统一 502 并附 SDK 错误码，避免把设备故障误报为无录像。

`GET /healthz`：进程存活检查，不代表 NVR 可达。

## 实现

1. 全局和每设备并发准入；一个请求独立登录 NVR，查询指定码流的 SDK 编码元数据。
2. `NET_DVR_PlayBackByTime_V40` + `NET_DVR_SetPlayBackESCallBack` + `NET_DVR_PLAYSTART`，回调深拷贝帧进入有界队列。每 250ms 查询进度、每 2s 心跳，队列高低水位暂停/恢复；溢出直接失败，不丢帧。
3. 首个 I 帧提取 Annex-B SPS/PPS/VPS，FFmpeg 直接创建 MP4，保留一个待写帧以确定真实帧间隔。严格使用 SDK 媒体毫秒 PTS，I/P 帧 DTS=PTS。最后一帧延用上一间隔（单帧 40ms）。不解码、不编码、无需 GPU。
4. 进度 100 且数据静默后停止回放，写 MP4 trailer。MP4 文件通过 `mkstemp` 创建并立即 unlink，FD 生命周期控制清理，不落裸流临时文件。HTTP provider 用 64KiB 分块 `pread` 返回，释放响应后关闭 FD 和并发名额。

并发名额覆盖 SDK 下载及 HTTP 文件发送，慢客户端也占名额，限制在途文件总量。队列有界，但 FFmpeg MP4 sample 索引随帧数增长；不是严格恒定内存。超时覆盖参数处理、登录、回放和封装，不包含 HTTP 文件发送；SDK 同步调用无法强制中断，实际响应可能超时。客户端在生成阶段断开不会主动取消 SDK 下载。默认停止宽限 150 秒，长请求可能在容器关闭时被终止。

## 配置

| 环境变量 | 默认 |
|---|---|
| HKMEDIA_LISTEN_ADDR / HKMEDIA_PORT | 0.0.0.0 / 8181 |
| HKMEDIA_MAX_CONCURRENT / HKMEDIA_MAX_DEVICE_CONCURRENT | 4 / 2 |
| HKMEDIA_DEFAULT_TIMEOUT_MS | 120000 |
| HKMEDIA_MAX_DURATION_SECONDS | 7200（最大 2 小时） |
| HKMEDIA_MAX_FILE_MIB | 8192（单请求 ES 输入和 MP4 分别限制） |
| HKMEDIA_QUEUE_MIB | 32（每路 ES 队列） |
| HKMEDIA_CONNECT_TIMEOUT_MS | 5000 |
| HKMEDIA_PLAYBACK_IDLE_MS | 15000 |
| HKMEDIA_EOF_QUIET_MS | 2000 |
| HKMEDIA_TEMP_DIR / LOGDIR | /data/temp / /data/logs |
| HKMEDIA_WEB_ROOT | ./web |
| HKMEDIA_API_TOKEN | 空，可信网络使用 |
| HKMEDIA_LOG_LEVEL / HKMEDIA_SDK_LOG | info / 0 |

默认 4 个 8GiB 文件额度意味着需预留最多约 32GiB 临时存储，另计日志与 FFmpeg 内存。不同 IP/域名别名不会自动归并为同一台设备。当前未提供主机白名单；此服务可连接请求指定的设备地址，勿直接暴露公网。

## 构建与测试

```bash
# 无 SDK/FFmpeg 依赖的核心测试
cmake -S . -B build-core -DHKMEDIA_CORE_TESTS_ONLY=ON
cmake --build build-core
ctest --test-dir build-core --output-on-failure

# 完整本地构建（Ubuntu 24.04 + 已导入 SDK）
sudo apt-get install -y build-essential cmake pkg-config libavformat-dev libavcodec-dev libavutil-dev nlohmann-json3-dev libcpp-httplib-dev libspdlog-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
HKMEDIA_TEMP_DIR=./data/temp LOGDIR=./data/logs ./build/hk-recording-service
# 另一个终端：
./scripts/smoke_test.sh
```

真机验收：H.264、H.265 各取 30 秒、5 分钟及长录像，检查 MP4 的首末帧、帧数、时长与原始录像一致；使用 `ffprobe -v error -show_streams -show_format recording.mp4` 和 `ffmpeg -v error -i recording.mp4 -f null -` 校验。测试无录像、错误密码、NVR 断网、超时、并发拒绝、队列溢出、磁盘满、客户端断开与停止服务。不得仅凭核心单元测试认定真机兼容性。

## 当前支持边界

- 仅 H.264/H.265 完整 Annex-B ES 视频帧，**不包含音频**；B 帧、FU-A 分片明确拒绝。海康私有 264 仅在兼容标准 Annex-B 时可用。
- SDK 查询的是当前通道编码参数，历史录像编码可能不同；首 I 帧必须带完整参数集，中途分辨率及可识别的参数集变化明确拒绝。先保证封装正确，再扩展跨编码变化和 B 帧支持。
- 不调用 SDK 原生 `NET_DVR_GetFileByTime_V40` 下载模式；只使用回放回调封装 MP4。
- 源码包生成环境只执行了无依赖核心测试；尚未进行实际 HCNetSDK/FFmpeg 联合编译、Docker 构建或 NVR 真机测试。
