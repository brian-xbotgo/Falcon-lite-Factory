# FALCON 固件打包流程

本文记录 FALCON 产测固件从编译、runtime 打包、SDK patch 应用到完整固件集成的当前流程。这里的“固件打包”指把产测运行时和 FALCON SDK patch 集成进 RK3576 SDK，最终由 SDK 生成可烧录镜像；单独的 `factory_firmware.tar.gz` 只适合板端快速替换验证，不是完整可烧录固件。

## 入口

FALCON 平台打包入口是：

```text
platforms/falcon/build_factory.sh
```

正常不要直接调用它，统一通过顶层构建脚本进入：

```sh
cd factory_fw
PLATFORM=falcon ./build.sh
```

`build.sh` 会先交叉编译 `factory_test`，再调用 `platforms/falcon/build_factory.sh` 生成 runtime 包；如果设置了 `SDK_DIR`，会继续做 SDK 固件集成。

## 环境变量

必需：

```sh
export FALCON_SDK=/home/gdh/falcon/Omni3576-sdk/buildroot/output/rockchip_rk3576_ipc/host
```

完整固件集成需要：

```sh
export SDK_DIR=/home/gdh/falcon/Omni3576-sdk
```

常用开关：

| 变量 | 作用 |
|------|------|
| `PLATFORM=falcon` | 选择 FALCON 平台 |
| `FALCON_SDK` | 交叉编译 toolchain 和目标库来源 |
| `SDK_DIR` | RK3576 SDK 根目录；设置后进入固件集成流程 |
| `FACTORY_SDK_DRY_RUN=1` | 只打印 SDK 集成动作，不修改 SDK |
| `FACTORY_BUILD_SDK=1` | 集成后执行 `$SDK_DIR/build.sh` 生成固件 |
| `FACTORY_APPLY_SDK_PATCH=0` | 跳过 SDK patch，适合已经确认 patch 应用过的 SDK |
| `FACTORY_LVGL_SOURCE_DIR` | 临时覆盖 LVGL 资源目录 |
| `FACTORY_MOTOR_KO` | 临时覆盖 `motor_tmi8152.ko` 路径 |
| `FACTORY_BATTERY_KO` | 临时覆盖 `battery.ko` 路径 |

默认情况下，LVGL 资源和 SDK patch 都来自本仓库，不依赖个人工程目录。

## 只编 runtime 包

用于在已启动的板子上快速替换 `/oem/usr` 验证：

```sh
cd factory_fw
export FALCON_SDK=/home/gdh/falcon/Omni3576-sdk/buildroot/output/rockchip_rk3576_ipc/host
PLATFORM=falcon ./build.sh
```

输出：

```text
build/falcon/factory_test
build/falcon/factory_firmware.tar.gz
```

这个 tar 包会包含：

- `bin/factory_test`
- `bin/mosquitto`
- `conf/platform.json`
- `conf/tests.json`
- `conf/mosquitto.conf`
- `conf/lvgl_source`
- `scripts/factory_start.sh`
- `scripts/factory_rndis.sh`
- `init.d/S90factory_fw`
- D-Bus、BlueZ 辅助运行文件
- 必要共享库
- `lib/modules/battery.ko`
- `conf/motor_tmi8152.ko`
- `sdk_patch/`

板端快速替换：

```sh
cd /oem/usr
tar xzf /path/to/factory_firmware.tar.gz
/oem/usr/scripts/factory_start.sh restart
tail -f /userdata/logs/factory_test.log
```

注意：这只是 runtime 替换，不能验证设备树、kernel config、Buildroot 配置是否已经进入固件。

## SDK 集成 dry-run

先用 dry-run 检查路径和将要写入的内容：

```sh
cd factory_fw
export SDK_DIR=/home/gdh/falcon/Omni3576-sdk
export FALCON_SDK=$SDK_DIR/buildroot/output/rockchip_rk3576_ipc/host

FACTORY_SDK_DRY_RUN=1 PLATFORM=falcon ./build.sh
```

dry-run 应该能看到：

```text
[build_factory] Copying LVGL resources: .../platforms/falcon/resources/lvgl_source
[factory_patch] SOURCE_PATCH_DIR=.../platforms/falcon/sdk_patch
[factory_patch] dry run only
```

如果 manifest 文件缺失，dry-run 会直接失败。这是刻意设计的，防止其他开发者拿到不完整 patch 后打出不可复现固件。

## 完整固件构建

确认 dry-run 正常后执行：

```sh
cd factory_fw
export SDK_DIR=/home/gdh/falcon/Omni3576-sdk
export FALCON_SDK=$SDK_DIR/buildroot/output/rockchip_rk3576_ipc/host

FACTORY_BUILD_SDK=1 PLATFORM=falcon ./build.sh
```

脚本会完成：

1. 交叉编译 `factory_test`。
2. 生成 `build/falcon/factory_firmware.tar.gz`。
3. 把 runtime 拷贝进 SDK extra-parts：

```text
$SDK_DIR/device/rockchip/common/extra-parts/oem/normal/usr
$SDK_DIR/device/rockchip/common/extra-parts/userdata/testdata
```

4. 应用本仓库自带的 FALCON SDK patch。
5. 安装 Buildroot overlay：

```text
$SDK_DIR/buildroot/board/rockchip/common/overlays/factory_fw
```

6. 在目标 rootfs 存在时安装 `/etc/init.d/S90factory_fw`。
7. 删除旧业务启动和监控脚本。
8. 执行：

```sh
cd $SDK_DIR
./build.sh
```

SDK 生成镜像的位置仍以原 SDK 为准，常见目录：

```text
$SDK_DIR/output/firmware
$SDK_DIR/buildroot/output/rockchip_rk3576_ipc/images
```

## SDK patch 内容

自包含 patch 目录：

```text
platforms/falcon/sdk_patch
```

manifest：

```text
platforms/falcon/sdk_patch/factory_patch_manifest.txt
```

当前 patch 覆盖范围：

- `kernel_patch/dragonfly*.dts*`：FALCON 设备树。
- `kernel_patch/rockchip_linux_defconfig`：kernel 配置。
- `kernel_patch/*.patch`：摄像头、屏幕、WiFi、USB 充电检测、SD 卡异常保护等 kernel patch。
- `kernel_patch/gc4663.c`、`imx678.c`、`fb_nv3007.c`、`rk_fiq_debugger.c`：驱动源文件。
- `kernel_patch/motor_tmi8152/motor_tmi8152.ko`：电机模块。
- `kernel_patch/om70x0x_battery/battery.ko`：电量计模块。
- `buildroot_patch/`：syslog、BlueZ、post-build 配置。
- `external/`：`dnsmasq.conf`、`wpa_supplicant.conf`。
- `device/rockchip/common/scripts/mk-extra-parts.sh`：extra-parts 打包脚本。
- `device_rk3576_defconfig/`：RK3576 device/buildroot/u-boot/recovery/rkbin 相关配置。

`apply_factory_patch.sh` 默认使用自己所在目录作为 `SOURCE_PATCH_DIR`，因此其他开发者只要拿到本仓库，就不需要再依赖 `/home/gdh/falcon/app/...` 之类的个人路径。

单独应用 patch：

```sh
cd factory_fw/platforms/falcon/sdk_patch
FACTORY_PATCH_DRY_RUN=1 ./apply_factory_patch.sh /home/gdh/falcon/Omni3576-sdk
./apply_factory_patch.sh /home/gdh/falcon/Omni3576-sdk
```

## LVGL 资源

FALCON 屏幕初始化依赖 LVGL 字体资源，默认随仓库携带：

```text
platforms/falcon/resources/lvgl_source
```

打包后应出现在：

```text
conf/lvgl_source/Rajdhani/Rajdhani-SemiBold-5.ttf
```

构建后检查：

```sh
tar tzf build/falcon/factory_firmware.tar.gz | grep 'conf/lvgl_source/Rajdhani/Rajdhani-SemiBold-5.ttf'
```

烧录或替换到板端后检查：

```sh
ls /oem/usr/conf/lvgl_source/Rajdhani/Rajdhani-SemiBold-5.ttf
```

如果日志出现字体路径找不到，优先确认资源是否被打进 tar 包和 SDK extra-parts。

## 启动流程

固件启动脚本：

```text
/etc/init.d/S90factory_fw
/oem/usr/scripts/factory_start.sh
```

`factory_start.sh` 当前顺序：

1. 创建 `/userdata/logs`、`/userdata/prod`、`/device_data` 和 `/userdata/factory_mode`。
2. 停掉旧业务和监控进程。
3. `hwclock --hctosys` 同步 RTC。
4. 加载 `battery.ko`、`motor_tmi8152.ko`。
5. 配置 USB gadget，默认同时启用 ADB + RNDIS。
6. 启动 `mosquitto`。
7. 启动 D-Bus。
8. 初始化 Bluetooth HCI 和 `bluetoothd`。
9. 按需启动 WiFi AP。
10. 启动 `factory_test`。

默认 RNDIS：

```text
usb0=172.16.110.6
```

默认 USB 模式：

```text
FACTORY_USB_MODE=adb_rndis
idProduct=0x0013
ADB FunctionFS=/dev/usb-ffs/adb
RNDIS interface=usb0
```

如果上位机报：

```text
adb shell "mount -o remount,rw /device_data" ... no devices/emulators found
```

这不是设备树判定错误，含义是 PC 侧没有看到 ADB 设备。先在板端看 USB 初始化：

```sh
tail -f /userdata/logs/factory_rndis.log
ls /dev/usb-ffs/adb/ep1 /dev/usb-ffs/adb/ep2
cat /sys/kernel/config/usb_gadget/rockchip/configs/b.1/strings/0x409/configuration
cat /sys/kernel/config/usb_gadget/rockchip/idProduct
ps | grep adbd
```

PC 侧先确认：

```sh
adb devices
```

运行包会携带 `adb_keys` 和 `etc/profile.d/adbd.sh`。启动 USB 前脚本会把 `/oem/usr/adb_keys` 同步到 `/adb_keys`，并设置 `ADB_SECURE=1`，否则上位机无法授权，麦克风录音文件也无法通过 ADB 拉取。

如果 ADB + RNDIS 都能枚举，但瑞芯微烧录工具不显示 ADB，临时切纯 ADB：

```sh
FACTORY_USB_MODE=adb /oem/usr/scripts/factory_rndis.sh restart
```

需要开机默认纯 ADB 时：

```sh
echo adb > /device_data/factory_usb_mode
/oem/usr/scripts/factory_rndis.sh restart
```

恢复默认 ADB + RNDIS：

```sh
echo adb_rndis > /device_data/factory_usb_mode
/oem/usr/scripts/factory_rndis.sh restart
```

运行日志：

```text
/userdata/logs/factory_test.log
/userdata/logs/mosquitto.log
/userdata/logs/factory_rndis.log
/userdata/logs/dbus.log
/userdata/logs/bluetooth.log
/userdata/logs/bluetoothd.log
/userdata/logs/modules.log
```

## 删除旧业务启动项

产测固件不能依赖 FALCON 业务固件的启动环境，也不能被旧业务监控拉起或重启。SDK 集成时会删除这些 init 脚本：

```text
S90dragonfly
S50usbdevice
S50usbdevice.sh
S40bluetoothd
S36wifibt-init.sh
S51otaupdate
xbotgo_app_monitor.sh
S99-auto-reboot
S95watchdog
S95watchdog.sh
S50nginx
S50fcgiwrap
S60security
```

运行时也会尝试停掉这些旧业务进程：

```text
xbotgo_app_monitor.sh
ota_update
updateEngine
misc_app
prod_test
normal_lvgl_app
charge_lvgl_app
multi_media
file_mng
http_agent
nginx
fcgiwrap
rkipc
```

如果出现“开机 logo 后直接重启”或“`factory_test` 运行 1 秒左右重启”，优先检查上述 monitor、ota、watchdog、auto-reboot 是否还在 rootfs 或进程列表里。

## 板端验收

烧录或替换后执行：

```sh
ls /oem/usr/bin/factory_test
ls /oem/usr/conf/platform.json
ls /oem/usr/conf/tests.json
ls /oem/usr/conf/lvgl_source/Rajdhani/Rajdhani-SemiBold-5.ttf
ls /oem/usr/sdk_patch/factory_patch_manifest.txt
```

启动检查：

```sh
/oem/usr/scripts/factory_start.sh restart
ps | grep -E 'factory_test|mosquitto|bluetoothd'
ip addr show usb0
tail -f /userdata/logs/factory_test.log
```

不应该再看到旧业务进程：

```sh
ps | grep -E 'misc_app|prod_test|normal_lvgl_app|charge_lvgl_app|xbotgo_app_monitor|ota_update|updateEngine'
```

MQTT 正常时，日志应包含：

```text
[TestEngine] MQTT connected 127.0.0.1:1883
[TestEngine] mosquitto loop started
```

BLE 产测广播正常时，日志应包含：

```text
[ble_wifi] MQTT connected, subscribed to topics
[ble_wifi] Waiting for 14-byte SN via MQTT topic AZA/sn_pcba before BLE advertising...
[ble_wifi] Got 14-byte SN from [AZA]: <14位SN>
[ble_wifi] BLE advertisement registered
```

FALCON 的 BLE 产测流程是上位机先通过 MQTT 下发版本响应 `AZA`，设备从其中的 `sn_pcba` 取 14 字节 SN，再主动写入 ManufacturerData 广播；上位机随后扫描这个广播。若只看到等待 SN 的日志，先确认上位机是否真的发了 `AZA` 或普通测试 topic 的 38 字节请求包。

## 构建前检查

提交或交给别人打包前至少跑：

```sh
bash -n platforms/falcon/build_factory.sh
sh -n platforms/falcon/scripts/factory_start.sh
sh -n platforms/falcon/sdk_patch/apply_factory_patch.sh

export SDK_DIR=/home/gdh/falcon/Omni3576-sdk
export FALCON_SDK=$SDK_DIR/buildroot/output/rockchip_rk3576_ipc/host
FACTORY_SDK_DRY_RUN=1 PLATFORM=falcon ./build.sh

tar tzf build/falcon/factory_firmware.tar.gz | grep 'conf/lvgl_source/Rajdhani/Rajdhani-SemiBold-5.ttf'
tar tzf build/falcon/factory_firmware.tar.gz | grep 'sdk_patch/kernel_patch/rockchip_linux_defconfig'
tar tzf build/falcon/factory_firmware.tar.gz | grep 'sdk_patch/device_rk3576_defconfig/rk3576_defconfig'
```

这些检查能覆盖三类常见问题：

- LVGL 资源没打进去。
- SDK patch 还依赖个人目录或缺文件。
- runtime 能跑，但完整固件缺设备树、kernel config、Buildroot 配置。

## 常见问题

### 只生成了 tar 包，没有固件镜像

没有设置 `SDK_DIR` 或没有设置 `FACTORY_BUILD_SDK=1`。runtime 包不是完整固件。

### 板子开机后重启

通常是旧业务 monitor、OTA、watchdog 或 auto-reboot 还在运行。检查 `/etc/init.d` 和 `ps`，确认 `S90factory_fw` 是产测固件主启动入口。

### 屏幕初始化但字体缺失

检查 `conf/lvgl_source` 是否在 tar 包和 `/oem/usr/conf` 中。不要从个人业务工程路径临时拷贝资源，应该提交到 `platforms/falcon/resources/lvgl_source`。

### 别人的 SDK 环境应用不了 patch

先跑：

```sh
FACTORY_PATCH_DRY_RUN=1 platforms/falcon/sdk_patch/apply_factory_patch.sh $SDK_DIR
```

如果 manifest 缺文件，说明仓库没有带齐 patch 内容；如果 patch 冲突，说明对方 SDK 基线与当前基线不同，需要先对齐 SDK 版本或单独处理冲突。
