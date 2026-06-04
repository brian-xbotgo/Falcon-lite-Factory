# Platforms 模块说明

## 职责

`platforms/` 是平台实现层，包含：
- 纯虚接口定义（跨平台契约）
- 各平台的 driver 实现
- 平台硬件配置（JSON）

**设计原则**：主体框架只认接口，不关心底层是哪颗 SoC。

## 目录结构

```
platforms/
├── common/
│   ├── interface/          # 纯虚接口定义
│   │   ├── IPlatform.h
│   │   ├── IEncoder.h
│   │   ├── ICameraDriver.h
│   │   ├── IBatteryDriver.h
│   │   ├── IMotorDriver.h
│   │   ├── IHallDriver.h
│   │   ├── IDisplayDriver.h
│   │   ├── IGpioDriver.h
│   │   ├── IRecorder.h
│   │   └── IWifiManager.h
│   └── impl/               # 通用/空实现（CI 测试用）
│       ├── Null*.cpp
│       └── SysfsGpioDriver.cpp
├── falcon/                 # Falcon 平台（RK3576）
│   ├── platform.json
│   ├── tests.json
│   └── drivers/
│       ├── RkMppEncoder.cpp
│       ├── Gc4663CameraDriver.cpp
│       ├── Cw221xBatteryDriver.cpp
│       ├── Tmi8152MotorDriver.cpp
│       ├── HallSwitchDriver.cpp
│       ├── LvglDisplayDriver.cpp
│       ├── Rv1126bRecorder.cpp
│       └── Rv1126bWifiManager.cpp
└── null/                   # Null 平台（x86 CI 测试）
    └── (无 driver，全部用 Null* 实现)
```

## 接口清单

| 接口 | 职责 | 典型实现 |
|------|------|----------|
| `IPlatform` | 平台总入口，`registerDrivers()` | `FalconPlatform` |
| `IEncoder` | 视频编码（H.264） | `RkMppEncoder` |
| `ICameraDriver` | 摄像头探测、OTP 验证 | `Gc4663CameraDriver` |
| `IBatteryDriver` | 电池电压/百分比读取 | `Cw221xBatteryDriver` |
| `IMotorDriver` | 电机控制 | `Tmi8152MotorDriver` |
| `IHallDriver` | 霍尔传感器读取 | `HallSwitchDriver` |
| `IDisplayDriver` | 显示渲染 | `LvglDisplayDriver` |
| `IGpioDriver` | GPIO 操作 | `SysfsGpioDriver`（通用） |
| `IRecorder` | 录像启停 | `Rv1126bRecorder` |
| `IWifiManager` | WiFi 扫描/连接 | `Rv1126bWifiManager` |

## IPlatform 接口

```cpp
class IPlatform {
public:
    virtual ~IPlatform() = default;
    virtual bool init(const nlohmann::json& config) = 0;
    virtual const char* name() const = 0;
    virtual void registerDrivers(DriverRegistry& reg) = 0;

    // 产测压测工具路径（默认空实现 = 不支持）
    virtual const char* gpuTestPath() const    { return nullptr; }
    virtual const char* npuTestPath() const    { return nullptr; }
    virtual const char* stressTestPath() const { return nullptr; }
    virtual const char* emmcTestPath() const   { return nullptr; }
};
```

**关键**：只保留 `registerDrivers()`，没有 `createXxx()` 工厂方法——单一真相源。

## 平台注册示例

```cpp
void FalconPlatform::registerDrivers(DriverRegistry& reg) {
    reg.bind<IEncoder>      ([] { return std::make_unique<RkMppEncoder>(); });
    reg.bind<ICameraDriver> ([] { return std::make_unique<Gc4663CameraDriver>(); });
    reg.bind<IBatteryDriver>([] { return std::make_unique<Cw221xBatteryDriver>(); });
    reg.bind<IGpioDriver>   ([] { return std::make_unique<SysfsGpioDriver>(); });
    reg.bind<IDisplayDriver>([] { return std::make_unique<LvglDisplayDriver>(); });
    // 平台没有电机 → 不 bind<IMotorDriver>()
}
```

## platform.json

```json
{
  "platform": "falcon",
  "gpio": {
    "led": { "white": 170, "red": 169 },
    "buzzer": 93
  },
  "i2c": {
    "cam0": { "bus": 3, "addr": "0x29", "otp": "/proc/otp_eeprom-3-50" }
  },
  "battery": {
    "driver": "cw221x",
    "sysfs_path": "/sys/class/power_supply/cw221X-bat/uevent"
  }
}
```

**原则**：换一颗摄像头或改一根 GPIO，只动 `platform.json`，源码与二进制逻辑不变。
