# FALCON 测试项修复记录

本文记录 FALCON 产测平台化过程中，已经根据板端实测日志修复过的测试项、判定依据和复测入口。这里写的是当前代码实际行为，不按旧 `plans` 文档推演。

当前目标平台是 FALCON（RK3576）。部分平台层沿用了 FALCON_AIR 的抽象，但 FALCON 的充电 IC、电量计、按键、霍尔和打包环境已经按当前板子单独配置。

## 关联文件

- `platforms/falcon/platform.json`：FALCON 硬件配置，GPIO、I2C、音频、TF、输入设备等判定都从这里取。
- `platforms/falcon/tests.json`：MQTT topic 到测试模块的绑定。
- `docs/proto/factory_proto.md`：上位机协议和 topic/error_code 约定。
- `src/tests/*.cpp`：各测试项实现。
- `platforms/falcon/drivers/*.cpp`：FALCON 平台驱动。

## 总体验收标准

单项测试以 `factory_test` 的发布结果为准：

```text
[Result] topic=<xxR> status=PASS detail=
[TestEngine] publish <xxA> payload_len=42 error_code=0
```

如果 `status=PASS` 但 `error_code` 不是 0，需要优先按协议检查返回包组装逻辑；正常情况下 PASS 应该对应 `error_code=0`。板端调试时保留 `factory_test.log` 中的模块日志，因为当前修复重点就是把判定依据打印出来。

## 按键测试

Topic：

- `17R`：按键抽测，当前要求任意 2 个有效按键。
- `20R`：按键全测，要求 6 个按键全部触发。

当前配置：

```json
"input": {
  "key_devices": [
    { "path": "/dev/input/event0", "name": "rk805 pwrkey", "keys": ["KEY_POWER"] },
    { "path": "/dev/input/event2", "name": "adc-keys", "keys": ["KEY_F13","KEY_F14","KEY_F15","KEY_F16","KEY_F17"] }
  ],
  "key_codes": ["KEY_POWER", "KEY_F13", "KEY_F14", "KEY_F15", "KEY_F16", "KEY_F17"]
}
```

修复点：

- 不再只假设固定 event 节点可用，启动时会打开配置里的 input 设备，并打印设备名和支持的 key code。
- `KEY_MACRO1` 会归一到 `KEY_POWER`，兼容部分内核映射。
- `17R` 使用 `required_count=2`，适合产线快速确认；`20R` 使用 `require_all=true`，适合完整按键确认。
- 全测模式会写 `/userdata/key_valid`，用于后续流程判断按键是否已完成。

通过依据：

```text
[KeyTest] device /dev/input/event2 name=adc-keys supports POWER=0 F13=1 F14=1 F15=1 F16=1 F17=1 MACRO1=0
[KeyTest] key accepted raw=184 normalized=184 count=1
[KeyTest] key accepted raw=183 normalized=183 count=2
[Result] topic=17R status=PASS detail=
```

如果没有上位机响应，先看是否出现 `[TestEngine] publish 17A`。如果有 publish 而上位机没显示，问题在协议/上位机链路；如果没有 publish，继续看 `[KeyTest] opened ...` 和 key event 是否到达。

## RTC、充电 IC、电量计

Topic：`10R`

FALCON 与 FALCON_AIR 的充电和电量计不同，不能沿用 FALCON_AIR 的 I2C 配置。当前 FALCON 配置：

```json
"charger": {
  "bus": 6,
  "addr": "0x75",
  "probe_reg": "0x01",
  "probe_expect_any": ["0x00", "0x01"]
},
"fuel_gauge": {
  "bus": 6,
  "addr": "0x38",
  "probe_reg": "0x00",
  "probe_expect": "0xb1"
},
"battery": {
  "dev_node": "/dev/om70X0X-bat"
}
```

修复点：

- RTC 不只返回 PASS，会打印 `/dev/rtc` 时间、系统时间和比较结果。
- 充电 IC、电量计从 `platform.json` 读取 bus、addr、reg、期望值。
- 支持 `probe_expect_any`，充电 IC 当前允许 `0x00/0x01`。
- 电量计额外检查 `/dev/om70X0X-bat` 可读。

通过依据：

```text
[RtcTest] rtc_time=2026-06-08 16:29:51 min_year=2026
[RtcTest] sys_time=2026-06-08 16:29:51
[RtcTest] compare rtc/system same year-month-day-hour result=PASS
[RtcTest] charger bus=6 addr=0x75 reg=0x01 value=0x00 mask=0x00 actual=0x0 expect=0x0/0x1 result=PASS
[RtcTest] fuel_gauge bus=6 addr=0x38 reg=0x00 value=0xb1 mask=0x00 actual=0xb1 expect=0xb1 result=PASS
[RtcTest] fuel_gauge_dev=/dev/om70X0X-bat readable=1
[RtcTest] final error_code=0x00000000
```

失败时重点看 bus 是否存在。如果日志里还有 `/dev/i2c-2` 或 FALCON_AIR 的地址，说明板端配置或打包资源没有更新。

## 电池测试

Topic：`15R`、`24R`

当前 FALCON 使用 OM70x0x 电量计：

```json
"battery": {
  "driver": "om70x0x",
  "sysfs_path": "/sys/class/power_supply/om70X0X-bat/uevent",
  "model_path": "/sys/class/power_supply/om70X0X-bat/model_name",
  "dev_node": "/dev/om70X0X-bat"
}
```

通过依据：

- sysfs 可读。
- 电池信息有效。
- 电压不低于 `3.600V`。

典型日志：

```text
[BatteryTest] sysfs=/sys/class/power_supply/om70X0X-bat/uevent model=SS_0x11_V2026031301 voltage=4.004V min=3.600V percent=85 result=PASS
```

如果 init 失败，先确认 `battery.ko` 是否已加载，再确认 SDK patch 是否带入 OM70x0x 电池驱动。

## TF 卡测试

Topic：`14R`

协议要求不能只判断卡在不在，还要做读写速度测试。当前配置：

```json
"storage": {
  "tf_partition": "/dev/mmcblk1p1",
  "tf_mount_point": "/mnt/sdcard",
  "tf_speed_test_file": "/mnt/sdcard/factory_tf_speed_test.bin",
  "tf_speed_test_mb": 64,
  "tf_min_write_speed_mb_s": 10,
  "tf_min_read_speed_mb_s": 10
}
```

修复点：

- 去掉 `lsblk` 依赖，Buildroot 上没有 `lsblk` 时不会误失败。
- 从 `/sys/block/mmcblk*` 扫描候选块设备，优先可移动设备，避免把 eMMC 当 TF。
- 支持 `exfat`、`vfat`、`ext4` 自动挂载。
- 写入 64 MB 测速文件、读取校验数据模式，并分别检查读写速度是否达到 10 MB/s。

通过依据：

```text
[TfCardTest] block candidates: mmcblk1(removable=1 parts=1) ...
[TfCardTest] mounted partition=/dev/mmcblk1p1 mount_point=/mnt/sdcard fs=exfat
[TfCardTest] speed file=/mnt/sdcard/factory_tf_speed_test.bin write=xx.xMB/s read=xx.xMB/s min_write=10.0MB/s min_read=10.0MB/s result=PASS
```

不要再退回到“存在 `/dev/mmcblk1` 就 PASS”的逻辑；这会漏掉分区损坏、挂载失败和速度不达标的问题。

## 麦克风和蜂鸣器

Topic：`11R`、`23R`

当前配置：

```json
"audio": {
  "mic_device": "hw:0,0",
  "record_path": "/userdata/prod/test.wav",
  "record_seconds": 5,
  "sample_rate": 48000,
  "format": "S32_LE",
  "channels": 2,
  "pdm_gain_control": "PDM1 Gain",
  "gain_percent": 100
},
"gpio": {
  "buzzer": 93
}
```

修复点：

- 录音前创建 `/userdata/prod`，删除旧 wav。
- 录音前用 `amixer` 设置 `PDM1 Gain`。
- 使用 `arecord` 录制 WAV，文件大小必须大于 WAV 头部 44 字节。
- 录音完成后执行 `chmod 644 /userdata/prod/test.wav` 并 `sync()`，保证上位机通过 ADB 拉文件时可读。
- 录音期间蜂鸣器 GPIO 93 按节奏鸣叫，用同一个测试覆盖蜂鸣器可控性。

通过依据：

```text
[MicTest] set_gain rc=0 result=PASS
[MicTest] record rc=0 result=PASS
[MicTest] buzzer pulse=1 state=on result=PASS
[MicTest] wav path=/userdata/prod/test.wav ready=1 size=<大于44> buzzer=PASS error_code=0x00000000
```

如果上位机播放不了音频，先确认不是 ADB 拉取失败：

```sh
adb devices
adb shell ls -l /userdata/prod/test.wav
adb pull /userdata/prod/test.wav .
```

上位机报 `no devices/emulators found` 时，麦克风本身可能已经录音成功，问题在 USB ADB 没枚举。先按固件打包流程文档里的 USB/ADB 排查处理，再看 WAV 文件大小和采样格式是否为 S32_LE/48k/2ch。

## 侧灯

侧灯不属于单独 topic，但会跟随产测状态变化。当前 FALCON GPIO：

```json
"gpio": {
  "led": {
    "white": 89,
    "red": 144
  }
}
```

当前控制逻辑：

- `Normal`：白灯和红灯交替。
- `Test`：白灯闪烁。
- `TestDone`：白灯常亮。
- `TestFail`：红灯闪烁。
- `Off`：红白灯关闭。

初始化日志：

```text
[StatusLed] init red=144 white=89 result=PASS active_high=1
```

如果侧灯不亮，先检查 GPIO 89/144 是否被旧业务进程占用，再确认当前固件没有启动 `normal_lvgl_app`、`charge_lvgl_app` 等旧业务程序。

## 霍尔测试

Topic：

- `21R`：垂直方向上下限位。
- `27R`：水平方向霍尔电压。

当前配置：

```json
"gpio": {
  "hall": {
    "upper_limit": 83,
    "lower_limit": 82
  }
},
"i2c": {
  "hall": {
    "driver": "auto",
    "bus_candidates": [3, 2, 0, 1, 4, 5, 6, 7, 8, 9],
    "addr": "0x48",
    "uart_candidates": ["/dev/ttyS9", "/dev/ttyS11"],
    "sample_count": 120,
    "sample_interval_ms": 50,
    "min_voltage": 1.6,
    "max_voltage": 2.0
  }
}
```

`21R` 修复点：

- 不再走旧的 I2C 霍尔初始化路径。
- 使用 GPIO 83/82 读取上下限位，默认低电平有效。
- 驱动垂直电机先找下限，再找上限，最后回中。

`21R` 通过依据：

```text
[HallTest] vertical_limit start upper_gpio=83 upper_value=1 lower_gpio=82 lower_value=1 active_level=0
[HallTest] vertical_limit move lower direct=forward angle=420.0 started=1
[HallTest] limit active gpio=82 value=0 active_level=0
[HallTest] vertical_limit move upper direct=backward angle=420.0 started=1
[HallTest] limit active gpio=83 value=0 active_level=0
[HallTest] vertical_limit final ... error_code=0x00000000 result=PASS
```

`27R` 修复点：

- `HallSwitchDriver` 支持 I2C ADS1110 多 bus 候选，不会因为 `/dev/i2c-3` 不存在直接失败。
- `driver=auto` 时，I2C 不可用会继续尝试 ADS122U04 UART 候选。
- 水平电机运动时采样 120 次，检查最小电压和最大电压达到阈值。

`27R` 通过依据：

```text
[HallSwitch] initialized driver=ADS1110 bus=<实际bus> addr=0x48 ...
[HallTest] horizontal_voltage motor angle=420.0 direct=forward started=1
[HallTest] horizontal_voltage samples=120/120 min=<值>V expect_min>=1.6000V max=<值>V expect_max>=2.0000V ... result=PASS
```

如果仍然看到所有 I2C 候选失败，要回 SDK patch 检查设备树、I2C 控制器和霍尔 ADC 实际接线；不要只在应用层固定改成某一个 bus。

## 电机测试

Topic：

- `191R`、`192R`：板级电机测试，垂直/水平。
- `291R`、`292R`：循环运动。
- `293R`、`294R`：停止运动。

当前配置：

```json
"motor": {
  "driver": "tmi8152",
  "device_path": "/dev/tmi8152"
}
```

修复和打包要求：

- 板端必须存在 `/dev/tmi8152`。
- 固件包会带 `motor_tmi8152.ko`，启动脚本会尝试加载。
- 如果在旧业务固件环境里替换运行，需要先停掉可能占用电机或复位系统的业务进程。

如果电机不动，先看：

```sh
lsmod | grep tmi8152
ls -l /dev/tmi8152
grep -n "motor" /userdata/logs/factory_test.log
```

## 通信和启动环境

当前测试已经能通过 MQTT 通信，但完整固件还需要独立启动环境。相关修复在打包流程文档中记录：

- 启动 `mosquitto`。
- 配置 USB gadget，默认 `adb_rndis`，其中 RNDIS 为 `usb0=172.16.110.6`，ADB 用于上位机 remount 和拉取录音文件。
- 启动 D-Bus、Bluetooth HCI、`bluetoothd`。
- FALCON 的 BLE 广播在 `factory_test` 里启动，收到 MQTT `AZA` 的 14 字节 `sn_pcba` 后才注册有效广播。
- 停掉旧业务进程和监控脚本，避免开机 logo 后重启或程序运行 1 秒后重启。

USB/ADB 通过依据：

```text
[factory_usb] start adbd from /usr/bin/adbd
[factory_usb] adb FunctionFS ready
[factory_usb] bound udc=<udc> mode=adb_rndis
```

PC 侧：

```sh
adb devices
adb shell "mount -o remount,rw /device_data"
```

如果瑞芯微烧录工具在 ADB + RNDIS 组合模式下识别不稳定，板端切纯 ADB：

```sh
FACTORY_USB_MODE=adb /oem/usr/scripts/factory_rndis.sh restart
```

BLE 通过依据：

```text
[ble_wifi] MQTT connected, subscribed to topics
[ble_wifi] Waiting for 14-byte SN via MQTT topic AZA/sn_pcba before BLE advertising...
[ble_wifi] Got 14-byte SN from [AZA]: <14位SN>
[ble_wifi] BLE advertisement registered
```

## 板端复测清单

运行：

```sh
cd /oem/usr/bin
./factory_test
```

或走完整启动脚本：

```sh
/oem/usr/scripts/factory_start.sh restart
tail -f /userdata/logs/factory_test.log
```

每次修复后至少确认：

- `[TestEngine] MQTT connected 127.0.0.1:1883`
- 目标 topic 已 subscribe。
- 收到 `xxR` 后有 dispatch 日志。
- 模块日志打印了判定依据。
- publish 的 `xxA` 里 PASS 对应 `error_code=0`。
- 上位机显示与板端 publish 一致。

## 不要回退的点

- TF 卡不能只看块设备是否存在，必须做读写和校验。
- FALCON 不能使用 FALCON_AIR 的充电 IC、电量计 bus/address。
- 打包资源不能依赖个人目录，例如 `/home/gdh/falcon/app/...`。
- 固件启动不能依赖旧业务进程；产测固件需要能独立启动 MQTT、RNDIS、BLE 和 `factory_test`。
- SDK patch 必须自包含设备树、Buildroot 配置、kernel 配置和必要驱动，保证其他开发者环境可复现。
