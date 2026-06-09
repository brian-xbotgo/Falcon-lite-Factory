# 构建说明

## 依赖

- CMake >= 3.16
- GCC / G++（base 平台本地构建）
- FALCON_SDK（falcon 平台交叉编译）
- vendored 依赖：`third_party/nlohmann`、`third_party/mosquitto`、`third_party/lvgl`

## base 平台

base 平台用于 x86 本地编译和框架回归。

```bash
cd factory_fw
PLATFORM=base ./build.sh
```

产物：`build/base/factory_test`

## falcon 平台

falcon 平台用于 RK3576 交叉编译。

```bash
cd factory_fw
export FALCON_SDK=/home/gdh/falcon/Omni3576-sdk/buildroot/output/rockchip_rk3576_ipc/host
PLATFORM=falcon ./build.sh
```

产物：`build/falcon/factory_test`，打包产物由 `platforms/falcon/build_factory.sh` 生成。

## 构建入口

`build.sh` 根据 `PLATFORM` 选择：

- 平台目录：`platforms/<platform>`
- toolchain：`cmake/platforms/<platform>.cmake`
- 构建目录：`build/<platform>`

`factory_fw/src/CMakeLists.txt` 是 src 内唯一 CMake 入口；平台 CMake 不再逐个引入 `src/core`、`src/control` 等子目录。

## 清理

```bash
rm -rf build/
```

## FALCON firmware integration

The FALCON build produces a runtime package by default. `platforms/falcon/build_factory.sh`
is also the firmware packaging entry: with `SDK_DIR` it integrates the runtime
into the RK3576 SDK, and with `FACTORY_BUILD_SDK=1` it runs the SDK firmware
build.

See `docs/usage/FALCON_FACTORY_PACKAGE.md` for the package overview,
`docs/usage/FALCON_FIRMWARE_FLOW.md` for the full firmware packaging flow, and
`docs/usage/FALCON_TEST_FIXES.md` for the FALCON board test-item fixes and
validation notes.
