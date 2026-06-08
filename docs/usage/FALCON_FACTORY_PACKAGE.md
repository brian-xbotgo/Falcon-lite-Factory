# FALCON Factory Package

This page describes the first independent FALCON factory firmware package.
The package is intentionally smaller than the original Dragonfly business
startup and keeps only the factory runtime path.

## Scope

The package contains:

- `bin/factory_test`
- `bin/mosquitto`
- `conf/platform.json`, `conf/tests.json`
- `conf/mosquitto.conf`
- `scripts/factory_start.sh`
- `scripts/factory_rndis.sh`
- `init.d/S90factory_fw`
- D-Bus helper runtime: `bin/dbus-daemon`, `bin/dbus-uuidgen`
- BlueZ helper runtime: `libexec/bluetooth/bluetoothd`
- key shared libraries copied from the FALCON SDK target
- optional `battery.ko` and `motor_tmi8152.ko` when they are available
- `sdk_patch/factory_patch_manifest.txt`

The package does not start or depend on normal business applications such as
`misc_app`, `prod_test`, `normal_lvgl_app`, `charge_lvgl_app`, `multi_media`,
`file_mng`, or `http_agent`.

## SDK Patch

Before building a clean firmware image, apply the minimal FALCON hardware and
runtime patches from the original FALCON tree:

```bash
cd /oem/usr/sdk_patch
./apply_factory_patch.sh /home/gdh/falcon/Omni3576-sdk \
  /home/gdh/falcon/app/lastcode20251210/XbotGo-Dragonfly-Embedded/sdk_patch
```

For audit only:

```bash
FACTORY_PATCH_DRY_RUN=1 ./apply_factory_patch.sh /home/gdh/falcon/Omni3576-sdk
```

The manifest keeps hardware enablement and runtime basics only. It excludes the
original business startup path.

## Build

```bash
cd /tmp/factory_fw_base_control
export FALCON_SDK=/home/gdh/falcon/Omni3576-sdk/buildroot/output/rockchip_rk3576_ipc/host
PLATFORM=falcon ./build.sh
```

Output:

```text
build/falcon/factory_firmware.tar.gz
```

## Deploy

On the board, unpack the tarball into `/oem/usr`:

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
2. load optional battery and motor modules
3. configure USB RNDIS as `usb0=172.16.110.6`
4. start `mosquitto`
5. start D-Bus system bus when it is not already running
6. initialize Bluetooth HCI and `bluetoothd`
7. optionally start WiFi AP
8. start `factory_test`

BLE advertising is started inside `factory_test` on FALCON builds.

## Runtime Switches

- `FACTORY_ENABLE_BLE=0`: do not start BLE from `factory_test`
- `FACTORY_ENABLE_RNDIS=0`: skip USB RNDIS setup
- `FACTORY_LOAD_MODULES=0`: skip optional kernel module loading
- `FACTORY_ENABLE_WIFI_AP=1`: start the WiFi AP helper path
- `FACTORY_RNDIS_IP=172.16.110.6`: override RNDIS IP
- `FACTORY_LOG_DIR=/userdata/logs`: override log path

## Logs

Runtime logs are written under `/userdata/logs`:

- `factory_test.log`
- `mosquitto.log`
- `factory_rndis.log`
- `dbus.log`
- `bluetooth.log`
- `bluetoothd.log`
- `modules.log`
