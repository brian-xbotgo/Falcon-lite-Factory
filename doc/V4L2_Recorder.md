# V4L2 录像模块设计文档

## 一、概述

录像模块是 Falcon_Air_Factory 产测固件的核心子系统，负责老化测试期间的双路摄像头视频录制。整体链路为：

```
GC4663 传感器 → CSI-2 → ISP (rkisp, rkaiq 3A) → /dev/video23/31 (NV12)
                  → V4L2 mmap 抓流 → MPP H.264 硬编码 → /userdata/cam*_output.h264
```

- **平台**: RV1126B (rkisp_v11 驱动, Buildroot rootfs)
- **摄像头**: 双路 GC4663 @ I2C3(0x29) + I2C4(0x29)
- **ISP**: rkaiq_3A_server v6.0x32.0 (不运行 ISP 则 STREAMON 失败且画面为 Bayer 绿色)
- **编码**: Rockchip MPP 硬编码 H.264 CBR 2000kbps
- **接口**: Linux 标准 V4L2 + MEMORY_MMAP (非 Rockchip 私有 API)

---

## 二、模块架构

### 2.1 文件清单

| 文件 | 职责 |
|------|------|
| `include/hal/IRecorder.h` | 录像接口抽象 (start/stop/isRecording)、CameraConfig/RecorderConfig 数据结构 |
| `include/hal/V4l2Recorder.h` | V4L2 + MPP 实现头文件 (CameraWorker 持有 encoder 实例) |
| `include/hal/MppEncoder.h` | MPP H.264 硬编码封装 (init/encode/deinit + 码率统计) |
| `include/hal/RecorderController.h` | 单例控制器，持有 IRecorder 指针，解耦测试层与实现层 |
| `include/hal/RecorderImpl.h` | 工厂方法 (nullRecorder / createV4l2Recorder) |
| `src/hal/V4l2Recorder.cpp` | V4L2 操作实现 (open/format/mmap/QBUF/DQBUF/STREAMON/STREAMOFF) |
| `src/hal/MppEncoder.cpp` | MPP 编码实现 (mpp_create / mpp_enc_cfg / encode_put_frame / encode_get_packet) |
| `src/hal/RecorderController.cpp` | 控制器实现 + nullRecorder 回退 |
| `src/hal/NullRecorder.cpp` | 空录像实现 (stub) |
| `configs/recorder.json` | JSON 摄像头配置 (设备路径、分辨率、格式、输出文件) |
| `firmware/Dragonfly_lunch_factory.sh` | 固件启动脚本 (Phase 4: 拉起 rkaiq + 等待 ISP 就绪) |
| `CMakeLists.txt` | 链接 Mosquitto + LVGL + librockchip_mpp |

### 2.2 类关系

```
IRecorder (接口)
  ├── NullRecorder     ← 默认回退 (无操作)
  └── V4l2Recorder     ← 实际录像实现
          ├── CameraWorker[]
          │      ├── cfg     (CameraConfig, 来自 recorder.json)
          │      ├── fd      (V4L2 设备文件描述符)
          │      ├── outfile (FILE* 输出 H.264)
          │      ├── thread  (std::thread 采集线程)
          │      ├── mplane  (MPLANE 标志位)
          │      └── encoder (MppEncoder, H.264 硬编码)
          └── m_cfg          (RecorderConfig)

RecorderController (单例)
  └── m_recorder: IRecorder*  → 委托给 V4l2Recorder 或 NullRecorder
```

### 2.3 数据流

```
                     ┌─────────────┐
  recorder.json  →   │   main()    │  解析 JSON → RecorderConfig
                     └──────┬──────┘
                            │ createV4l2Recorder(cfg)
                     ┌──────▼──────┐
                     │V4l2Recorder │  start() → startCamera() × N
                     └──────┬──────┘
                            │
              ┌─────────────┼─────────────┐
              │             │             │
         Worker[0]     Worker[1]     ... (每路独立线程)
              │             │
    ┌─────────▼─────────┐   ┌─────────▼─────────┐
    │ 1. v4l2Open()     │   │ 1. v4l2Open()     │
    │ 2. v4l2SetFormat()│   │ 2. v4l2SetFormat()│
    │ 3. v4l2SetFps()   │   │ 3. v4l2SetFps()   │
    │ 4. v4l2ReqBufs()  │   │ 4. v4l2ReqBufs()  │
    │ 5. MppEncoder.init│   │ 5. MppEncoder.init│
    │ 6. cameraLoop()   │   │ 6. cameraLoop()   │
    └─────────┬─────────┘   └─────────┬─────────┘
              │                       │
    ┌─────────▼─────────┐   ┌─────────▼─────────┐
    │ mapBuffers()      │   │ mapBuffers()      │
    │ QBUF x N          │   │ QBUF x N          │
    │ STREAMON          │   │ STREAMON          │
    │                    │   │                    │
    │ loop:              │   │ loop:              │
    │  select(fd)        │   │  select(fd)        │
    │  DQBUF → NV12 buf  │   │  DQBUF → NV12 buf  │
    │  encoder.encode()  │   │  encoder.encode()  │
    │  writeH264()       │   │  writeH264()       │
    │  QBUF              │   │  QBUF              │
    │                    │   │                    │
    │ STREAMOFF          │   │ STREAMOFF          │
    │ unmapBuffers()     │   │ unmapBuffers()     │
    └────────────────────┘   └────────────────────┘
              │                       │
              ▼                       ▼
    /userdata/cam0_output.h264  /userdata/cam1_output.h264
```

---

## 三、ISP 链路

### 3.1 为什么需要 rkaiq

rkisp_mainpath 是 Rockchip ISP 的输出节点。如果没有 rkaiq_3A_server 初始化 3A 算法并配置 media pipeline：
1. `VIDIOC_STREAMON` 返回 `Invalid argument`
2. 即使强行拉流，输出为 Bayer raw（画面绿色），未经过 demosaicing / 白平衡 / 降噪

### 3.2 Media 拓扑

| ISP 实例 | Media 设备 | isp-subdev | input-params | statistics | mainpath | 对应传感器 |
|---------|-----------|------------|-------------|-----------|---------|----------|
| rkisp-vir0 | /dev/media3 | /dev/v4l-subdev6 | /dev/video29 | /dev/video28 | /dev/video23 | m00_b_gc4663 (I2C3) |
| rkisp-vir1 | /dev/media4 | /dev/v4l-subdev11 | /dev/video37 | /dev/video36 | /dev/video31 | m01_b_gc4663 (I2C4) |

### 3.3 启动流程

固件启动脚本 `Dragonfly_lunch_factory.sh` Phase 4:

```
1. killall -9 rkaiq_3A_server    # 清理残留
2. rm -f IPC 残片 + ipcrm -a     # 清理抽象 socket / SV IPC / POSIX 信号量
3. rkaiq_3A_server &             # 后台启动
4. 轮询 (最长 15s):              # 等待 ISP pipeline 就绪
   - killall -0 检查 rkaiq 存活
   - segfault 则重新拉起
   - v4l2-ctl -D 检查 video23/31 可访问
```

**关键约束**:
- rkipc 与独立 rkaiq_3A_server 互斥 (抽象命名空间 Unix socket 冲突) → 固件打包时删除 `/usr/bin/rkipc`
- `/tmp/aiq0.lock` `/tmp/aiq1.lock` `/tmp/.rkaiq_3A` `/var/tmp/rkipc` 残留会导致 `bind: Address already in use`
- System V 共享内存 (shm) 和信号量 (sem) 也应 `ipcrm -a` 清理

---

## 四、V4L2 采集层

### 4.1 设备配置

recorder.json:
```json
{
    "cameras": [
        {
            "device": "/dev/video23",       // cam0: rkisp-vir0 mainpath
            "width": 1280, "height": 720,
            "format": "NV12",                // ISP 输出 YUV 4:2:0
            "fps": 30,
            "output": "/userdata/cam0_output.h264",
            "buffer_count": 4
        },
        {
            "device": "/dev/video31",       // cam1: rkisp-vir1 mainpath
            "width": 1280, "height": 720,
            "format": "NV12",
            "fps": 30,
            "output": "/userdata/cam1_output.h264",
            "buffer_count": 4
        }
    ]
}
```

### 4.2 MPLANE 自适应

`v4l2Open()` 通过 `VIDIOC_QUERYCAP → device_caps` 自动检测 `V4L2_CAP_VIDEO_CAPTURE_MPLANE`。
`bufType()` 返回正确的 `V4L2_BUF_TYPE_*` 枚举值，后续所有 V4L2 ioctl (S_FMT/REQBUFS/QBUF/DQBUF/STREAMON) 均使用此类型。

### 4.3 采集循环

```cpp
cameraLoop() {
    mapBuffers(count)       // mmap 映射内核缓冲区到用户空间
    for i in 0..count: QBUF // 入队所有 buffer (V4L2 规范: 需在 STREAMON 前完成)
    STREAMON                // 首次 STREAMON 的 Q 序号需正确 (rkisp 驱动限制)

    while (running):
        select(fd, timeout=1s)     // 非 busy-wait，1s 超时可响应 stop()
        DQBUF → NV12 原始数据
        encoder.encode(NV12) → H.264
        fwrite(H.264, outfile)
        QBUF                       // 归还 buffer

    STREAMOFF
    unmapBuffers()
}
```

**关键约束**:
- `V4L2_FIELD_NONE`: rkisp 驱动不接受隔行/交替场模式
- `VIDIOC_S_PARM` (设置帧率): rkisp 不支持此 ioctl，作为非致命错误忽略
- 输出路径从 `/tmp`(tmpfs ~495MB) 改为 `/userdata`(eMMC)

---

## 五、MPP H.264 硬编码

### 5.1 编码链路

```
NV12 帧 (V4L2 DQBUF) → mpp_buffer (ION DMA) → encode_put_frame()
    → [RK VENC 硬件] → encode_get_packet() → H.264 Annex B → fwrite()
```

### 5.2 初始化流程

```cpp
mpp_create()            → MppCtx + MppApi
mpp_init(MPP_CTX_ENC, MPP_VIDEO_CodingAVC)
mpp_enc_cfg_init()      → MppEncCfg (新 API, 旧 struct 返回 MPP_NOK)
mpp_enc_cfg_set_s32()   × N  (配置 prep / rc / h264)
mpi->control(MPP_ENC_SET_CFG)
mpp_buffer_group_get(MPP_BUFFER_TYPE_ION, INTERNAL)
```

### 5.3 码率控制参数

| 参数 | 值 | 说明 |
|------|---|------|
| rc:mode | 1 (CBR) | 恒定码率 |
| rc:bps_target | 2000 | **单位 KBPS** (非 bps), 等于 2Mbps |
| rc:bps_max | 2125 | bps_target × 17/16 (CBR 上限 ~2.125Mbps) |
| rc:bps_min | 1875 | bps_target × 15/16 (CBR 下限 ~1.875Mbps) |
| rc:gop | 60 | 2 秒一个 I 帧 (@30fps) |
| rc:qp_init / qp_min | 24 | 初始/最小 QP |
| rc:qp_max | 40 | 最大 QP (上限保护) |
| h264:profile | 100 (High) | H.264 High Profile |
| h264:cabac_en | 1 | CABAC 熵编码 |

### 5.4 关键陷阱

- **rc:bps_target 单位是 KBPS**: 配置 `2000000` 会被解释为 2,000,000 Kbps = ~2Gbps，硬件限幅到 ~145Mbps 后输出仍然远超预期。正确值 `2000` = 2000 Kbps = 2 Mbps。
- **旧 API 废弃**: `MppEncPrepCfg` / `MppEncRcCfg` / `MppEncCodecCfg` 结构体 + `SET_PREP_CFG` / `SET_RC_CFG` / `SET_CODEC_CFG` 在 mpp_enc_v2 返回 `MPP_NOK`，必须用 `mpp_enc_cfg_set_s32()` + `MPP_ENC_SET_CFG`。
- **Strides 16 对齐**: width/height 原始值 + hor_stride/ver_stride 对齐值均需正确配置。
- **ION 缓冲区类型**: rv1126 MPP 编码器需要 `MPP_BUFFER_TYPE_ION` (非 DRM)。
- **Buffer group**: `mpp_buffer_group_get` + `mpp_buffer_get_with_tag` 管理 ION DMA 缓冲区。
- **码率统计**: 每 5 秒打印实际 Mbps 和 fps，便于验证老化测试稳定性。

---

## 六、运行时流程

### 6.1 固件启动

```
内核启动 → S51otaupdate (mosquitto) → Dragonfly_lunch.sh (工厂入口脚本)
    → Phase 1: WiFi
    → Phase 2: USB RNDIS / ADB
    → Phase 3: Bluetooth
    → Phase 4: rkaiq_3A_server (ISP 初始化)
    → Phase 5: USB gadget 健康检查
    → Phase 6: BLE factory advertiser + Falcon_Air_Factory
```

### 6.2 main() 初始化

```cpp
main() {
    TestEngine 初始化 (MQTT + LED)
    MotorController / HallSwitch / GpioController 初始化
    FactoryDisplay 初始化 (LVGL)
    读取 recorder.json → RecorderConfig
    RecorderController::setRecorder(createV4l2Recorder(cfg))
    注册 12 个测试模块 → engine.start()
    LVGL 主循环 (10ms tick)
}
```

### 6.3 老化测试触发

```
MQTT R30 指令 → AgingTest::fullAgingStart()
    → RecorderController::start()
    → V4l2Recorder::start()
        → startCamera(worker[0])  // cam0
        │   ├── fopen(cam0_output.h264)
        │   ├── MppEncoder.init(H.264 CBR 2000kbps)
        │   ├── v4l2Open(/dev/video23)
        │   ├── v4l2SetFormat(NV12 1280x720)
        │   ├── v4l2ReqBufs(4)
        │   └── thread(cameraLoop) → 拉流 + 编码 + 写文件
        → startCamera(worker[1])  // cam1 (同上, /dev/video31)
        → m_active = true

    → MotorController 循环 (水平 + 垂直)
    → 每 10s 检测: dmesg MIPI ERR / wpa_cli scan Failed
    → 4h 后 (或 MQTT R34 停止) → RecorderController::stop()
        → worker.running = false
        → thread.join() → STREAMOFF → fclose
        → MppEncoder.deinit()
```

---

## 七、关键决策记录

1. **rkisp (ISP) 而非 RKCIF (CIF)**：初始方案使用 RKCIF CSI 直采 Bayer raw (无需 rkaiq)，但输出为绿色 raw 数据且无压缩。当前方案将 rkaiq 集成进固件启动链 (rkipc 冲突已解决)，使用 rkisp 输出颜色正确的 NV12 并进行 H.264 硬编码。

2. **MPP 硬编码**：原始 raw 帧太大（2560×1440 RG10 ≈ 5MB/帧），改 NV12 (1280×720, 1.4MB/帧) + H.264 CBR 2Mbps 压缩后 ≈ 250KB/s，双路 ≈ 500KB/s，可稳定写入 eMMC。

3. **MPLANE 自动检测**：rkisp_v11 驱动统一使用 MPLANE 接口，代码在 `v4l2Open()` 时通过 `device_caps` 自动适配，无需硬编码。

4. **V4L2 → encode → write 同步流水线**：每帧 DQBUF 后立即编码并写入，不缓存多帧，避免内存压力。

5. **抽象 socket 冲突**：rkaiq 与 rkipc (链接同一 lib) 争用抽象命名空间 Unix socket。方案：固件打包删除 `/usr/bin/rkipc`。

---

## 八、配置示例

### recorder.json

```json
{
    "version": 1,
    "description": "Factory recorder configuration - V4L2-based, platform-independent",
    "cameras": [
        {
            "device": "/dev/video23",
            "comment": "cam0: GC4663 on I2C3/0x29 -> rkisp-vir0 mainpath (ISP NV12)",
            "width": 1280,
            "height": 720,
            "format": "NV12",
            "fps": 30,
            "output": "/userdata/cam0_output.h264",
            "buffer_count": 4
        },
        {
            "device": "/dev/video31",
            "comment": "cam1: GC4663 on I2C4/0x29 -> rkisp-vir1 mainpath (ISP NV12)",
            "width": 1280,
            "height": 720,
            "format": "NV12",
            "fps": 30,
            "output": "/userdata/cam1_output.h264",
            "buffer_count": 4
        }
    ],
    "max_duration_sec": 0,
    "comment_max_duration": "0 = unlimited, record until stop() is called"
}
```

### MppEncoder 默认参数 (硬编码)

| 参数 | 默认值 | 说明 |
|------|-------|------|
| bitrate_kbps | 2000 | 2 Mbps CBR |
| gop | 60 | I 帧间隔 (2s @30fps) |
| profile | High (100) | H.264 High Profile |
| cabac | 1 | CABAC 熵编码 |
| buffer_count | 4 | V4L2 mmap buffer 数量 |
