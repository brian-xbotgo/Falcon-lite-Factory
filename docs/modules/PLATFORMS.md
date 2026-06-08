# Platforms 模块说明

## 职责

`platforms/` 保存平台实现层。框架主体只依赖 `platforms/common/interface` 的接口，不直接关心 SoC 或具体硬件差异。

## 目录结构

```text
platforms/
├── base/
│   ├── CMakeLists.txt
│   ├── build_factory.sh
│   ├── platform.json
│   └── tests.json
└── falcon/
    ├── CMakeLists.txt
    ├── build_factory.sh
    ├── platform.json
    ├── tests.json
    └── drivers/
        ├── RkMppEncoder.*
        ├── Gc4663CameraDriver.*
        ├── Cw221xBatteryDriver.*
        ├── Tmi8152MotorDriver.*
        ├── HallSwitchDriver.*
        ├── LvglDisplayDriver.*
        ├── Rk3576Recorder.*
        └── Rk3576WifiManager.*
```

通用平台代码位于 `src/platforms/common/`：

```text
src/platforms/common/
├── interface/                # IPlatform / IEncoder / ICameraDriver 等接口
├── Base*.cpp                 # 基础兜底实现
├── SysfsGpioDriver.cpp       # 通用 sysfs GPIO 实现
└── PlatformFactory.cpp       # 平台工厂与 base 平台自注册
```

## 命名约定

- 基础兜底实现统一使用 `Base` 前缀，例如 `BaseRecorder`、`BaseCameraDriver`。
- 基础平台目录和平台名统一为 `base`，构建命令为 `PLATFORM=base ./build.sh`。
- 平台真实实现使用产品或芯片相关前缀，例如 `Rk3576Recorder`、`Tmi8152MotorDriver`。

## 新增平台步骤

1. 新建 `platforms/<name>/`，包含 `CMakeLists.txt`、`build_factory.sh`、`platform.json`、`tests.json`。
2. 在 `platforms/<name>/drivers/` 实现平台 driver。
3. 实现 `<Name>Platform` 并在 `registerDrivers()` 中绑定平台能力。
4. 新增 `cmake/platforms/<name>.cmake`。
5. 运行 `PLATFORM=<name> ./build.sh` 验证。