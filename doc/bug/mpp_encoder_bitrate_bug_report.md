# MppEncoder 码率失控 Bug 复盘报告

| 项 | 值 |
|---|---|
| 模块 | `hal/MppEncoder.cpp` |
| 平台 | Rockchip RV1126B (Buildroot) |
| MPP 版本 | `958803d7`（2026-02-26，作者 Herman Chen）|
| 编码格式 | H.264 High@4.0，CBR，2000 kbps，1280×720@30fps，NV12 |
| 严重程度 | **高** —— 统计指标完全错误，并连带导致写盘文件异常膨胀 |
| 结论 | **不是真实码率超标，是统计代码取错了字段**；编码器本身 CBR 工作正常 |

---

## 1. 现象

启动 `Falcon_Air_Factory` 老化测试后，`MppEncoder` 内部的码率统计持续打印异常高的数值：

```
[mpp-stat 0x5584723e88] 185.09 Mbps  25.1 fps  (126 frames in 5019ms)
[mpp-stat 0x5584723f98] 143.37 Mbps  19.4 fps  (99 frames in 5091ms)
[mpp-stat 0x5584723e88] 150.38 Mbps  20.4 fps  (102 frames in 5001ms)
[mpp-stat 0x5584723f98] 150.28 Mbps  20.4 fps  (102 frames in 5004ms)
```

配置目标只有 **2 Mbps**，实测打印却接近 **150 Mbps**，相差约 75 倍。两路相机（`/dev/video23`、`/dev/video31`）均出现同样现象。

MPP 日志同时显示了几条 kmpp_obj setter 失败的告警：

```
kmpp_obj: obj MppEncCfg set rc:qp_max_step       s32 failed ret -1
kmpp_obj: obj MppEncCfg set h264:entropy_coding_mode s32 failed ret -1
kmpp_obj: obj MppEncCfg set h264:transform8x8_mode   s32 failed ret -1
kmpp_obj: obj MppEncCfg set h264:qp_max_step     s32 failed ret -1
```

这些告警一度误导了排查方向。

---

## 2. 排查过程

### 2.1 一次误诊

第一轮分析根据上述 setter 失败 + `qp_min=10` 偏低，得出的假设是：

- CABAC（`h264:entropy_coding_mode`）和 8x8 变换（`h264:transform8x8_mode`）未生效 → 编码效率下降
- `rc:qp_min = 10` 太低 → 编码器使用接近无损 QP，绕过 CBR 目标
- 实测 ~140 Mbps 恰好等于 RKVENC 硬件吞吐上限，符合"撞硬件天花板"假设

并据此给出修复建议：把 `qp_min` 从 10 提到 24。

### 2.2 修改后的现象（关键线索）

将 `rc:qp_min` / `h264:qp_min` 改为 24、`*_qp_min_i` 改为 22 之后重跑：

```
[mpp-stat 0x5584723e88] 185.09 Mbps  25.1 fps   ← 几乎没变化
[mpp-stat 0x5584723f98] 143.37 Mbps  19.4 fps   ← 几乎没变化
[mpp-stat 0x5584723e88] 150.38 Mbps  20.4 fps
```

**改 QP 完全没有影响输出"码率"**。这与"qp_min 控制了真实码率"的假设直接矛盾，说明前一轮分析的根因判断错了。

### 2.3 重新审视统计代码

把视线从 MPP 配置转回 `MppEncoder::encode()` 的输出端：

```cpp
packet_ = raw_packet;
*out_data = static_cast<const uint8_t*>(mpp_packet_get_data(raw_packet));
*out_size = mpp_packet_get_size(raw_packet);          // ← 这里

// —— 码率统计 ——
stat_bytes_  += *out_size;
stat_frames_ += 1;
```

`mpp_packet_get_size` 和 `mpp_packet_get_length` 在 MPP API 里语义完全不同：

| 函数 | 返回 | 用途 |
|---|---|---|
| `mpp_packet_get_size()` | 底层 buffer **分配容量**（最坏情况预留） | 资源管理 |
| `mpp_packet_get_length()` | 当前 packet **实际有效数据长度** | 写文件 / muxer payload / 统计码率 |

对 1280×720 NV12，packet buffer 容量 ≈ `1280 × 720 × 1.5` ≈ **1.38 MB**。
按这个值乘 fps 算"码率"，得到的就是**容量 × 帧率**，跟编码器真实输出无关：

```
1.38 MB × 30 fps × 8 ≈ 332 Mbps（理论上限）
1.38 MB × 19 fps × 8 ≈ 210 Mbps（实际帧率下）
```

实测打印的 140–185 Mbps 正好落在这个区间，与目标 2 Mbps 之间的 75× 差距，**就是 `get_size` 与 `get_length` 之间的比例**。

### 2.4 根因确认

`*out_size = mpp_packet_get_size(raw_packet)` 是错的：

1. **统计码率读到的是 buffer 容量而非真实编码长度** → stat 打印虚高
2. **下游若用 `out_size` 写文件 / 送 muxer**，会把整个未初始化 buffer 的尾部一起带上 → 落盘文件异常膨胀，看起来"视频很大"

CBR 本身工作正常。MPP 日志已经明确接受了配置：

```
mpp_enc: set rc cbr bps [2000:2125:1875] fps [30:1:fix] - [30:1:fix] gop 60
mpp_enc: mode cbr bps [1875:2000:2125] fps fix [30/1] -> fix [30/1] gop i [60] v [0]
```

---

## 3. 根本原因

**单行 API 误用**：`MppEncoder::encode()` 用 `mpp_packet_get_size` 而非 `mpp_packet_get_length` 取编码包大小。

误用导致两个连锁后果：

1. 自定义的 `[mpp-stat]` 码率统计输出错误数值
2. 上层（V4l2Recorder / RecorderController 链路）若按此 size 写盘，文件实际包含大量 buffer 内随机/历史字节

---

## 4. 修复

### 4.1 必改：取实际数据长度

```diff
   packet_ = raw_packet;
   *out_data = static_cast<const uint8_t*>(mpp_packet_get_data(raw_packet));
-  *out_size = mpp_packet_get_size(raw_packet);
+  *out_size = mpp_packet_get_length(raw_packet);
```

### 4.2 建议改：QP 与 setter 健壮性（次要，不影响本次主线）

虽然不是本次根因，但前一轮排查里发现的两个隐患仍然值得修：

- `qp_min = 10` 偏低，对 2 Mbps CBR 来说没有意义、还可能让编码器在静态画面时把 QP 拉得过低。建议 `rc:qp_min = 24`、`qp_min_i = 22`。
- `mpp_enc_cfg_set_s32` 单条失败时 MPP 不会让 `MPP_ENC_SET_CFG` 整体失败，只是这条不生效。建议封装：

  ```cpp
  auto must_set = [&](const char* k, int v) {
      if (mpp_enc_cfg_set_s32(cfg_handle, k, v) != MPP_OK)
          std::fprintf(stderr, "[mpp] WARN: set %s=%d failed\n", k, v);
  };
  ```

  这样下次 MPP 升级出现新的 schema 不兼容时能立刻发现，而不是等到生产环境码率不对才查。

- `h264:entropy_coding_mode` / `h264:transform8x8_mode` 这几个 key 在当前 RV1126B kmpp 上未被识别。后续可在板子上用 `mpp_enc_test --help` 或翻 `/usr/include/rockchip/rk_venc_cfg.h` 确认新 schema 的等价 key（可能是 `h264:cabac_en` / `h264:trans8x8` 一类），再补回。

---

## 5. 验证

修完 4.1 后，预期 stat 打印应回到：

```
[mpp-stat ...]  ~2.0 Mbps   ~30 fps   (~150 frames in 5000ms)
```

写盘文件大小也应回到正常水平：2 Mbps × 1 s ≈ **250 KB/s**，而非每帧 ~1.38 MB。

---

## 6. 经验教训

1. **"现象不变"是强信号**。改了假定中的根因变量（qp_min: 10 → 24），现象毫无变化，应当立刻怀疑根因判断错误，而不是继续在同一方向调参。本次排查在第二轮才回到这条线索上，本可以更快。

2. **不要被 MPP 的告警吸引走全部注意力**。`kmpp_obj … failed ret -1` 是真实问题，但和本次主诉无关。把次要告警当成主要嫌疑人，是典型的"在路灯下找钥匙"。

3. **统计 / 监控代码要和数据通路代码受同等审视**。本次 bug 是统计代码本身错了，但因为它打印的数字"看起来像码率"，整个团队都默认它正确，反而去怀疑编码器配置。生产环境里**测量工具自己出错**比"被测对象出错"更隐蔽。

4. **MPP API 命名陷阱**：`mpp_packet_get_size` vs `mpp_packet_get_length` 是经典踩坑点，类似的还有 `mpp_buffer_get_size` vs `mpp_buffer_get_*` 系列。封装一层 `MppEncoder` 时建议在内部用一个明确命名的本地变量，避免直接把模糊的 `size` 暴露到接口：

   ```cpp
   size_t encoded_len = mpp_packet_get_length(raw_packet);
   *out_size = encoded_len;
   ```

5. **数据通路上的 size 字段一律取 `length`**。这条可以作为团队约定写进编码规范，所有未来涉及 MPP packet 的代码都按此处理。

---

## 7. 影响范围

- 所有调用 `MppEncoder::encode()` 的链路（当前包括 `V4l2Recorder` → `RecorderController` → MQTT/落盘）
- 所有基于该 `*out_size` 计算或落盘的下游消费者
- 修复为单行改动，无 ABI 变化，无需重新设计接口
