# FALCON Factory Firmware

This page describes FALCON factory runtime packaging and SDK firmware
integration. The runtime is intentionally smaller than the original Dragonfly
business startup and keeps only the factory path.

For the current board bring-up notes and the exact firmware packaging command
sequence, see:

- `docs/usage/FALCON_TEST_FIXES.md`
- `docs/usage/FALCON_FIRMWARE_FLOW.md`

## Scope

The package contains:

- `bin/factory_test`
- `bin/mosquitto`
- `conf/platform.json`, `conf/tests.json`
- `conf/mosquitto.conf`
- LVGL display resources under `conf/lvgl_source`, including
  `Rajdhani/Rajdhani-SemiBold-5.ttf`
- `scripts/factory_start.sh`
- `scripts/factory_rndis.sh`
- `init.d/S90factory_fw`
- D-Bus helper runtime: `bin/dbus-daemon`, `bin/dbus-uuidgen`
- BlueZ helper runtime: `libexec/bluetooth/bluetoothd`
- USB/ADB helper runtime: `bin/adbd`, root `adb_keys`, and
  `etc/profile.d/adbd.sh`
- key shared libraries copied from the FALCON SDK target
- optional `battery.ko` and `motor_tmi8152.ko` when they are available
- self-contained FALCON SDK patch subset under `sdk_patch/`

The package does not start or depend on normal business applications such as
`misc_app`, `prod_test`, `normal_lvgl_app`, `charge_lvgl_app`, `multi_media`,
`file_mng`, or `http_agent`.

During SDK integration the factory overlay removes normal business and monitor
init scripts from rootfs, including `S90dragonfly`, `S51otaupdate`,
`xbotgo_app_monitor.sh`, `S99-auto-reboot`, `S50nginx`, `S50fcgiwrap`, and
`S60security`. These scripts are not part of the factory boot path. In
particular, `xbotgo_app_monitor.sh` can reboot the board when the normal
`ota_update` process is absent, which matches a board reboot shortly after
`factory_test` starts.

## Modes

`platforms/falcon/build_factory.sh` is the FALCON firmware packaging entry. It
supports two levels:

- runtime package: build `build/falcon/factory_firmware.tar.gz`
- SDK integration: copy the runtime into the RK3576 SDK `extra-parts`, apply
  factory SDK patches, and optionally run SDK `./build.sh`

`factory_fw/build.sh` always compiles `factory_test` first, then calls this
script. `FALCON_SDK` only selects the cross toolchain and source runtime
libraries. `SDK_DIR` is the switch that enables SDK firmware integration.

## Runtime Package Only

```bash
cd /tmp/factory_fw_base_control
export FALCON_SDK=/home/gdh/falcon/Omni3576-sdk/buildroot/output/rockchip_rk3576_ipc/host
PLATFORM=falcon ./build.sh
```

Output:

```text
build/falcon/factory_firmware.tar.gz
```

This is useful for board-side quick replacement under `/oem/usr`; it is not a
full flashable firmware image and does not write the RK3576 SDK.

## Firmware Integration Dry Run

Use dry-run before touching the SDK:

```bash
cd factory_fw
export SDK_DIR=/home/gdh/falcon/Omni3576-sdk
export FALCON_SDK=$SDK_DIR/buildroot/output/rockchip_rk3576_ipc/host

FACTORY_SDK_DRY_RUN=1 PLATFORM=falcon ./build.sh
```

Dry-run prints the SDK paths and the operations that would be performed.

## Full Firmware Build

To integrate the factory runtime into the RK3576 SDK and run SDK build:

```bash
cd factory_fw
export SDK_DIR=/home/gdh/falcon/Omni3576-sdk
export FALCON_SDK=$SDK_DIR/buildroot/output/rockchip_rk3576_ipc/host

FACTORY_BUILD_SDK=1 PLATFORM=falcon ./build.sh
```

The FALCON platform carries its own LVGL resources at
`platforms/falcon/resources/lvgl_source`. To override them temporarily:

```bash
export FACTORY_LVGL_SOURCE_DIR=/path/to/lvgl_source
```

The script copies the runtime into:

```text
$SDK_DIR/device/rockchip/common/extra-parts/oem/normal/usr
$SDK_DIR/device/rockchip/common/extra-parts/userdata/testdata
```

It also installs `S90factory_fw` into the current Buildroot target when the
target rootfs exists:

```text
$SDK_DIR/buildroot/output/rockchip_rk3576_ipc/target/etc/init.d
```

After integration it applies the factory SDK patch helper and calls:

```bash
cd $SDK_DIR
./build.sh
```

Set `FACTORY_APPLY_SDK_PATCH=0` when the SDK patches have already been applied
and should not be re-applied.

The script installs `S90factory_fw` into the SDK Buildroot overlay before SDK
build, then patches the current target rootfs again after SDK build. This keeps
the factory startup script in the generated rootfs image and removes the normal
business startup and reboot-monitor entries.

The flashable images are produced by the SDK build flow under the RK3576 SDK
output directories, for example:

```text
$SDK_DIR/output/firmware
$SDK_DIR/buildroot/output/rockchip_rk3576_ipc/images
```

## SDK Patch Helper

Before building a clean firmware image, apply the FALCON hardware and runtime
patch subset carried by this repository:

```bash
cd /oem/usr/sdk_patch
./apply_factory_patch.sh /path/to/Omni3576-sdk
```

For audit only:

```bash
FACTORY_PATCH_DRY_RUN=1 ./apply_factory_patch.sh /path/to/Omni3576-sdk
```

The patch subset includes the FALCON device tree, kernel config and kernel
patches, Buildroot runtime config, extra-parts script, and RK3576 device
defconfig files. It excludes the original business startup path.

## Deploy

For runtime-package board-side verification, unpack the tarball into `/oem/usr`:

```bash
cd /oem/usr
tar xzf /path/to/factory_firmware.tar.gz
```

Manual start:

```bash
/oem/usr/scripts/factory_start.sh start
```

Install init script:

```bash
/oem/usr/scripts/install_init.sh
```

Stop:

```bash
/oem/usr/scripts/factory_start.sh stop
```

## Startup Order

`factory_start.sh` runs:

1. create factory directories and `/userdata/factory_mode`
2. stop leftover normal business processes when running on a non-factory rootfs
3. load optional battery and motor modules
4. configure USB gadget as ADB + RNDIS, with `usb0=172.16.110.6`
5. start `mosquitto`
6. start D-Bus system bus when it is not already running
7. initialize Bluetooth HCI and `bluetoothd`
8. optionally start WiFi AP
9. start `factory_test`

BLE advertising is started inside `factory_test` on FALCON builds.

Factory BLE waits for a valid 14-byte SN before registering the advertisement.
The normal factory path is: the host publishes MQTT topic `AZA`, the device
extracts `sn_pcba`, writes it to ManufacturerData, and the host scans for that
advertisement.

## Runtime Switches

- `FACTORY_ENABLE_BLE=0`: do not start BLE from `factory_test`
- `FACTORY_ENABLE_RNDIS=0`: skip RNDIS but still start USB as ADB-only
- `FACTORY_USB_MODE=adb_rndis|adb|rndis`: choose the USB gadget mode
- `FACTORY_LOAD_MODULES=0`: skip optional kernel module loading
- `FACTORY_ENABLE_WIFI_AP=1`: start the WiFi AP helper path
- `FACTORY_RNDIS_IP=172.16.110.6`: override RNDIS IP
- `FACTORY_LOG_DIR=/userdata/logs`: override log path

The default mode is `adb_rndis` (`idProduct=0x0013`). If the Rockchip flashing
tool does not show ADB in the composite mode, switch the board to ADB-only:

```bash
FACTORY_USB_MODE=adb /oem/usr/scripts/factory_rndis.sh restart
```

To make the mode persistent:

```bash
echo adb > /device_data/factory_usb_mode
```

For ADB/remount failures, check:

```bash
adb devices
tail -f /userdata/logs/factory_rndis.log
ls /dev/usb-ffs/adb/ep0
ps | grep adbd
```

## Logs

Runtime logs are written under `/userdata/logs`:

- `factory_test.log`
- `mosquitto.log`
- `factory_rndis.log`
- `dbus.log`
- `bluetooth.log`
- `bluetoothd.log`
- `modules.log`

## Reboot Check

If the board still reboots just after boot logo or within seconds of starting
`factory_test`, check the flashed rootfs:

```bash
ls /etc/init.d | grep -E 'otaupdate|monitor|auto-reboot|nginx|fcgi|security'
```

The command should print nothing for a clean factory image. If any of those
entries remain, rebuild with this packager or inspect whether the flashed image
came from an older SDK output.
