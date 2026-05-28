# 产测固件平台化完整方案

> 版本: v1.0  
> 日期: 2026-05-28  
> 范围: 从 RV1126B 专用固件演进为跨平台产测框架  

---

## 目录

1. [执行摘要](#1-执行摘要)
2. [平台化总体架构](#2-平台化总体架构)
3. [详细目录结构](#3-详细目录结构)
4. [接口层设计](#4-接口层设计)
5. [配置系统设计](#5-配置系统设计)
6. [CMake 构建系统](#6-cmake-构建系统)
7. [代码迁移映射](#7-代码迁移映射)
8. [分阶段实施计划](#8-分阶段实施计划)
9. [风险与缓解](#9-风险与缓解)

---

## 1. 执行摘要

当前 `Falcon_Air_Factory` 是面向 **RV1126B + 双路 GC4663 + TMI8152 电机 + CW221X 电池 + AIC8800 WiFi/BT** 的专用产测固件。平台化的目标是：

- **同一套代码框架**支持多种 SoC（RV1126B、RK3588、未来全志/海思等）
- **配置文件驱动**硬件资源（GPIO、I2C、摄像头、电机、电池等）
- **平台插件化**：新平台只需实现一组纯虚接口，2-4 周完成适配
- **硬件改版零侵入**：换摄像头/电池/电机驱动 = 改配置 + 新 driver 实现

**核心设计原则**：

| 原则 | 说明 |
|------|------|
| **接口先行** | 先定义跨平台契约（纯虚接口），再迁移实现 |
| **配置驱动** | 所有硬件资源（GPIO号、设备路径、芯片参数）从 JSON 读取 |
| **渐进重构** | 每阶段保持 RV1126B 全量产测通过，逐步替换 |
| **测试解耦** | 测试逻辑与平台实现分离，通过配置表动态注册 |

---

## 2. 平台化总体架构

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              Application 层                                  │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  main.cpp — 通用入口，零平台相关代码                                   │   │
│  │  • 加载 platform.json → 创建 IPlatform 实例                           │   │
│  │  • 初始化 HAL → 注册测试 → 主循环                                     │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────────┘
                                       │
                    ┌──────────────────┼──────────────────┐
                    ▼                  ▼                  ▼
┌─────────────────────────┐  ┌─────────────────┐  ┌─────────────────────────┐
│      Core 层 (通用)      │  │  Common 层(通用) │  │      Config 层          │
│  ┌─────────────────┐    │  │  ┌───────────┐  │  │  ┌─────────────────┐    │
│  │   TestEngine    │    │  │  │ Types.h   │  │  │  │ platform.json   │    │
│  │  • MQTT/路由/异步 │    │  │  │ • 协议常量 │  │  │  │ • GPIO映射表     │    │
│  │  └──────────────┘    │  │  │ • 全局状态 │  │  │  │ • I2C设备表      │    │
│  │  AsyncTaskQueue   │  │  │  └───────────┘  │  │  │ • 摄像头参数     │    │
│  │  └──────────────┘    │  │  ┌───────────┐  │  │  │ • 测试项开关     │    │
│  │  TestRegistry     │  │  │  │ShellUtils │  │  │  └─────────────────┘    │
│  │  • 动态测试注册   │    │  │  │(通用工具) │  │  │  ┌─────────────────┐    │
│  └─────────────────┘    │  │  └───────────┘  │  │  │  tests.json      │    │
└─────────────────────────┘  └─────────────────┘  │  │  • topic映射     │    │
                                                  │  │  • 平台覆盖参数  │    │
                                                  │  └─────────────────┘    │
                                                  └─────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                     平台抽象层 (Platform Abstraction)                         │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────────────────┐  │
│  │  IPlatform      │  │  IEncoder       │  │      IDisplayDriver         │  │
│  │  • init()       │  │  • init()       │  │  • init()/taskHandler()     │  │
│  │  • createHAL()  │  │  • encode()     │  │  • setBatteryPercent()      │  │
│  │  • getConfig()  │  │  • deinit()     │  │  • deinit()                 │  │
│  └─────────────────┘  └─────────────────┘  └─────────────────────────────┘  │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────────────────┐  │
│  │  ICameraDriver  │  │  IBatteryDriver │  │      IMotorDriver           │  │
│  │  • probe()      │  │  • readVoltage()│  │  • init()/move()/stop()     │  │
│  │  • checkOtp()   │  │  • getPercent() │  │  • getPosition()            │  │
│  └─────────────────┘  └─────────────────┘  └─────────────────────────────┘  │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────────────────┐  │
│  │  IRecorder      │  │  IGpioDriver    │  │      IWifiManager           │  │
│  │  • start/stop   │  │  • export/write │  │  • scan/connect/disconnect  │  │
│  │  • isRecording  │  │  • read/unexport│  │  • registerStateCallback    │  │
│  └─────────────────┘  └─────────────────┘  └─────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────┘
                                       │
                    ┌──────────────────┼──────────────────┐
                    ▼                  ▼                  ▼
┌─────────────────────────┐  ┌─────────────────┐  ┌─────────────────────────┐
│   platforms/rv1126b/    │  │ platforms/rk3588│  │    platforms/common/    │
│  ┌─────────────────┐    │  │  (未来扩展)      │  │  ┌─────────────────┐    │
│  │ Rv1126bPlatform │    │  │                 │  │  │ NullPlatform    │    │
│  │ • 实现IPlatform │    │  │                 │  │  │ • x86 CI测试用  │    │
│  └─────────────────┘    │  │                 │  │  └─────────────────┘    │
│  ┌─────────────────┐    │  │                 │  │  ┌─────────────────┐    │
│  │ RkMppEncoder    │    │  │                 │  │  │ SoftEncoder     │    │
│  │ • 实现IEncoder  │    │  │                 │  │  │ • ffmpeg/openh264│   │
│  └─────────────────┘    │  │                 │  │  └─────────────────┘    │
│  ┌─────────────────┐    │  │                 │  │  ┌─────────────────┐    │
│  │ Gc4663CameraDrv │    │  │                 │  │  │ NullXxxDriver   │    │
│  │ • I2C+OTP       │    │  │                 │  │  │ • 各接口空实现   │    │
│  └─────────────────┘    │  │                 │  │  └─────────────────┘    │
└─────────────────────────┘  └─────────────────┘  └─────────────────────────┘
```

---

## 3. 详细目录结构

### 3.1 顶层目录

```
FACTORY_GIT/
├── CMakeLists.txt                 # 根 CMake，平台选择入口
├── toolchain.cmake                # 工具链配置（可选平台覆盖）
├── main.cpp                       # 通用入口（平台无关）
├── readme.md                      # 项目说明
│
├── .gitignore
├── build.sh                       # 通用构建脚本（包装 cmake）
├── build_factory.sh               # 产测固件打包脚本（按平台分支）
│
├── cmake/
│   ├── modules/
│   │   ├── FindRockchipMpp.cmake  # 查找 rockchip_mpp
│   │   ├── FindMosquitto.cmake    # 查找 mosquitto
│   │   └── FindLVGL.cmake         # 查找 LVGL
│   └── platforms/
│       ├── rv1126b.cmake          # RV1126B 编译选项
│       ├── rk3588.cmake           # RK3588 编译选项
│       └── null.cmake             # x86/null 编译选项
│
├── platforms/                     # ★ 平台实现层（核心新增）
│   ├── common/                    # 通用接口 + 默认空实现
│   │   ├── CMakeLists.txt
│   │   ├── interface/             # 纯虚接口定义
│   │   │   ├── IPlatform.h
│   │   │   ├── IEncoder.h
│   │   │   ├── ICameraDriver.h
│   │   │   ├── IBatteryDriver.h
│   │   │   ├── IMotorDriver.h
│   │   │   ├── IHallDriver.h
│   │   │   ├── IDisplayDriver.h
│   │   │   ├── IGpioDriver.h
│   │   │   ├── IRecorder.h
│   │   │   └── IWifiManager.h
│   │   └── impl/                  # 通用/空实现（CI测试用）
│   │       ├── NullPlatform.cpp
│   │       ├── NullEncoder.cpp
│   │       ├── NullCameraDriver.cpp
│   │       ├── NullBatteryDriver.cpp
│   │       ├── NullMotorDriver.cpp
│   │       ├── NullHallDriver.cpp
│   │       ├── NullDisplayDriver.cpp
│   │       ├── SysfsGpioDriver.cpp    # sysfs GPIO 通用实现
│   │       ├── NullRecorder.cpp
│   │       └── NullWifiManager.cpp
│   │
│   ├── rv1126b/                   # RV1126B 专用实现
│   │   ├── CMakeLists.txt
│   │   ├── platform.json          # RV1126B 硬件资源配置
│   │   ├── Rv1126bPlatform.cpp    # IPlatform 实现
│   │   ├── Rv1126bPlatform.h
│   │   ├── hal/
│   │   │   ├── RkMppEncoder.cpp   # IEncoder 实现（MPP硬编码）
│   │   │   ├── RkMppEncoder.h
│   │   │   ├── Gc4663CameraDriver.cpp  # ICameraDriver 实现
│   │   │   ├── Gc4663CameraDriver.h
│   │   │   ├── Cw221xBatteryDriver.cpp # IBatteryDriver 实现
│   │   │   ├── Cw221xBatteryDriver.h
│   │   │   ├── Tmi8152MotorDriver.cpp  # IMotorDriver 实现
│   │   │   ├── Tmi8152MotorDriver.h
│   │   │   ├── Tmi8152MotorHal.c       # 原有 MotorHal.c 迁移
│   │   │   ├── Tmi8152MotorHal.h
│   │   │   ├── HallSwitchDriver.cpp    # IHallDriver 实现
│   │   │   ├── HallSwitchDriver.h
│   │   │   ├── LvglDisplayDriver.cpp   # IDisplayDriver 实现
│   │   │   ├── LvglDisplayDriver.h
│   │   │   ├── Rv1126bRecorder.cpp     # IRecorder 封装（V4L2+MPP）
│   │   │   └── Rv1126bWifiManager.cpp  # IWifiManager 实现（Rk_wifi）
│   │   └── stress/                # RV1126B 专用压测二进制
│   │       ├── gpu_test
│   │       ├── rknn_matmul_api_demo
│   │       ├── stress
│   │       └── rwcheck
│   │
│   └── rk3588/                    # 未来扩展：RK3588
│       ├── CMakeLists.txt
│       ├── platform.json
│       ├── Rk3588Platform.cpp
│       ├── Rk3588Platform.h
│       └── hal/
│           ├── RkMppEncoderV2.cpp
│           ├── Imx415CameraDriver.cpp
│           └── DrmDisplayDriver.cpp
│
├── core/                          # ★ 核心框架层（从 src/core/ 迁移，完全通用）
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── TestEngine.h
│   │   ├── AsyncTaskQueue.h
│   │   └── TestRegistry.h         # 新增：动态测试注册中心
│   └── src/
│       ├── TestEngine.cpp
│       ├── AsyncTaskQueue.cpp
│       └── TestRegistry.cpp
│
├── common/                        # ★ 通用工具层（从 src/common/ + include/common/ 迁移）
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── Types.h                # 协议常量、全局状态、LED模式
│   │   └── ShellUtils.h           # shell_exec、read_sysfs、read_file
│   └── src/
│       └── ShellUtils.cpp
│
├── config/                        # ★ 配置中心（新增）
│   ├── CMakeLists.txt
│   ├── include/
│   │   └── PlatformConfig.h       # JSON配置读取、全局访问点
│   └── src/
│       └── PlatformConfig.cpp
│
├── hal/                           # ★ HAL 通用层（平台无关的抽象和封装）
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── RecorderController.h   # Singleton，持有 IRecorder* 注入点
│   │   ├── I2cController.h        # i2cget/i2cset 通用封装（平台无关）
│   │   └── V4l2Recorder.h         # 标准V4L2录像（需注入 IEncoder）
│   └── src/
│       ├── RecorderController.cpp
│       ├── I2cController.cpp
│       └── V4l2Recorder.cpp
│
├── tests/                         # ★ 测试模块层（平台无关的测试逻辑）
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── ITestModule.h          # 新增：测试模块接口
│   │   ├── TestContext.h          # 新增：测试上下文（注入平台driver）
│   │   └── BatteryTest.h          # 各测试模块头文件...
│   └── src/
│       ├── TestModuleRegistry.cpp # 新增：自注册中心
│       ├── BatteryTest.cpp
│       ├── CameraTest.cpp
│       ├── WifiTest.cpp
│       ├── RtcTest.cpp
│       ├── KeyTest.cpp
│       ├── MicTest.cpp
│       ├── SocTest.cpp
│       ├── TfCardTest.cpp
│       ├── MotorTest.cpp
│       ├── HallTest.cpp
│       ├── AgingTest.cpp
│       └── SysTest.cpp
│
├── ble/                           # ★ BLE 模块（从 src/ble/ 迁移，大部分通用）
│   ├── CMakeLists.txt             # 替代原有 Makefile
│   ├── include/
│   │   ├── BleAdvertiser.h
│   │   ├── BleGattServer.h
│   │   ├── BleMqttBridge.h
│   │   ├── BleDeviceInfo.h
│   │   ├── BleConstants.h
│   │   ├── BleTypes.h
│   │   ├── BleUtility.h
│   │   └── IWifiManager.h         # 新增：WiFi管理抽象接口
│   └── src/
│       ├── BleAdvertiser.cpp
│       ├── BleGattServer.cpp
│       ├── BleMqttBridge.cpp
│       ├── BleDeviceInfo.cpp
│       ├── BleUtility.cpp
│       └── main.cpp               # ble_factory_advertise 入口
│
├── btwifi/                        # ★ BT/WiFi 初始化脚本（按平台拆分）
│   ├── rv1126b/
│   │   ├── confs/
│   │   │   └── wps_hostapd_aic.conf
│   │   └── scripts/
│   │       ├── bt_init.sh         # 原 dragonfly_bt_init.sh
│   │       └── wifi_init.sh       # 原 dragonfly_wifi_init.sh
│   └── rk3588/
│       └── (未来扩展)
│
├── configs/                       # ★ 配置文件（按平台组织）
│   ├── rv1126b/
│   │   ├── platform.json          # 硬件资源映射
│   │   ├── tests.json             # 测试项注册表
│   │   ├── recorder.json          # V4L2录像配置
│   │   ├── hall_threshold.ini     # 霍尔测试阈值
│   │   └── gc4663_default.json    # ISP校准数据
│   └── common/
│       └── tests_schema.json      # 测试配置 JSON Schema
│
├── lunch/                         # 启动脚本（通用或按平台）
│   ├── start_mqtt.sh
│   ├── power_off.sh
│   └── get_cpuinfo.sh
│
├── firmware/                      # 固件资源
│   ├── lvgl_source/               # LVGL字体、图片资源
│   ├── Dragonfly_lunch_factory.sh # 工厂启动脚本
│   ├── factory_mode               # 工厂模式标志
│   └── iperf3_server_safe.sh
│
├── usb_gadget/                    # USB gadget 配置
│   ├── usb_config.sh
│   ├── usb_gadget_health.sh
│   └── usb_reconnect.sh
│
├── xbotgo_rootfs/                 # rootfs overlay
│   ├── etc/
│   ├── sbin/
│   ├── ssh/
│   └── usr/
│
├── sdk_patch/                     # SDK 补丁（按平台组织）
│   ├── rv1126b/
│   │   ├── kernel/
│   │   ├── buildroot/
│   │   ├── device/
│   │   ├── rkbin/
│   │   └── kernel_patch/
│   │       └── motor_tmi8152/
│   └── rk3588/
│
├── third_party/                   # 第三方库
│   ├── cJSON/
│   ├── lvgl/
│   ├── mosquitto/
│   ├── nlohmann/
│   └── tmi8152/                   # TMI8152 头文件（仅RV1126B用）
│
├── tools/                         # 工具程序
│   ├── factory_test_runner.cpp    # 主机端产测工具
│   └── soc_test.cpp               # 独立SoC测试客户端
│
└── doc/                           # 文档
    ├── architecture/
    │   ├── current_architecture.svg
    │   ├── platform_architecture.svg
    │   └── platformization_guide.md   # 本文档
    ├── factory_proto.md
    └── ble.md
```

### 3.2 关键目录说明

| 目录 | 说明 | 平台相关性 |
|------|------|-----------|
| `platforms/common/interface/` | 纯虚接口定义，跨平台契约 | 零相关 |
| `platforms/common/impl/` | 空实现 / sysfs GPIO 通用实现 | 零相关 |
| `platforms/rv1126b/` | RV1126B 全部平台相关代码 | 完全相关 |
| `core/` | TestEngine、异步队列、测试注册中心 | 零相关 |
| `common/` | 协议定义、Shell工具、全局状态 | 零相关 |
| `config/` | JSON配置解析、PlatformConfig单例 | 零相关 |
| `hal/` | 平台无关的HAL封装（V4L2、I2C、RecorderController） | 低相关 |
| `tests/` | 测试逻辑（通过TestContext访问平台driver） | 低相关 |
| `ble/` | BlueZ GATT/MQTT 通用代码 + IWifiManager接口 | 中相关 |
| `btwifi/` | 蓝牙/WiFi芯片初始化脚本 | 完全相关 |
| `configs/<platform>/` | 各平台配置文件 | 完全相关 |

---

## 4. 接口层设计

### 4.1 IPlatform — 平台总入口

```cpp
// platforms/common/interface/IPlatform.h
#pragma once
#include <memory>
#include <nlohmann/json.hpp>

namespace ft {

class IEncoder;
class ICameraDriver;
class IBatteryDriver;
class IMotorDriver;
class IHallDriver;
class IDisplayDriver;
class IGpioDriver;
class IRecorder;
class IWifiManager;

class IPlatform {
public:
    virtual ~IPlatform() = default;

    // 初始化平台，加载 platform.json
    virtual bool init(const nlohmann::json&amp; config) = 0;

    // 平台名称标识
    virtual const char* name() const = 0;

    // 创建各 HAL 组件（工厂方法）
    virtual std::unique_ptr<IEncoder>       createEncoder() = 0;
    virtual std::unique_ptr<ICameraDriver>  createCameraDriver() = 0;
    virtual std::unique_ptr<IBatteryDriver> createBatteryDriver() = 0;
    virtual std::unique_ptr<IMotorDriver>   createMotorDriver() = 0;
    virtual std::unique_ptr<IHallDriver>    createHallDriver() = 0;
    virtual std::unique_ptr<IDisplayDriver> createDisplayDriver() = 0;
    virtual std::unique_ptr<IGpioDriver>    createGpioDriver() = 0;
    virtual std::unique_ptr<IRecorder>      createRecorder() = 0;
    virtual std::unique_ptr<IWifiManager>   createWifiManager() = 0;

    // 产测压测工具路径（不同平台二进制不同）
    virtual const char* gpuTestPath() const { return nullptr; }
    virtual const char* npuTestPath() const { return nullptr; }
    virtual const char* stressTestPath() const { return nullptr; }
    virtual const char* emmcTestPath() const { return nullptr; }
};

// 平台工厂注册（各平台在编译单元中注册）
using PlatformFactoryFunc = std::unique_ptr<IPlatform>(*)();
bool registerPlatformFactory(const char* name, PlatformFactoryFunc factory);
std::unique_ptr<IPlatform> createPlatform(const char* name);

} // namespace ft
```

### 4.2 IEncoder — 视频编码抽象

```cpp
// platforms/common/interface/IEncoder.h
#pragma once
#include <cstdint>
#include <cstddef>

namespace ft {

struct EncoderConfig {
    int width        = 1280;
    int height       = 720;
    int fps          = 30;
    int bitrate_kbps = 2000;
    int gop          = 60;
};

class IEncoder {
public:
    virtual ~IEncoder() = default;
    virtual bool init(const EncoderConfig&amp; cfg) = 0;
    virtual void deinit() = 0;
    virtual bool isInitialized() const = 0;

    // 输入 NV12 帧，输出 H.264 AnnexB ES
    // out_data/out_size 有效直到下一次 encode() 或 deinit()
    virtual bool encode(const uint8_t* nv12_data, size_t nv12_size,
                        const uint8_t** out_data, size_t* out_size) = 0;
};

} // namespace ft
```

### 4.3 ICameraDriver — 摄像头探测抽象

```cpp
// platforms/common/interface/ICameraDriver.h
#pragma once
#include <string>
#include <cstdint>

namespace ft {

struct CameraProbeResult {
    bool found      = false;
    bool otpValid   = false;
    bool i2cOk      = false;
    bool mipiOk     = false;
    std::string chipId;
};

class ICameraDriver {
public:
    virtual ~ICameraDriver() = default;

    // 探测摄像头是否存在
    virtual CameraProbeResult probe(int cam_index) = 0;

    // 检查 OTP 烧录状态
    virtual bool checkOtp(int cam_index) = 0;

    // 读取 I2C 寄存器验证 chip id
    virtual uint32_t readChipId(int bus, int addr) = 0;

    // 获取 MIPI-CSI 错误状态
    virtual bool hasMipiError() = 0;

    // 获取视频节点是否存在
    virtual bool videoNodesExist() = 0;
};

} // namespace ft
```

### 4.4 IBatteryDriver — 电池读取抽象

```cpp
// platforms/common/interface/IBatteryDriver.h
#pragma once
#include <string>

namespace ft {

struct BatteryInfo {
    double voltage_v   = 0.0;
    int    percent     = 0;
    std::string model;
    bool   valid       = false;
};

class IBatteryDriver {
public:
    virtual ~IBatteryDriver() = default;
    virtual bool init() = 0;
    virtual BatteryInfo read() = 0;
    virtual std::string getSysfsPath() const = 0;
};

} // namespace ft
```

### 4.5 IMotorDriver — 电机控制抽象

```cpp
// platforms/common/interface/IMotorDriver.h
#pragma once
#include <cstdint>

namespace ft {

enum MotorDirection { MOTOR_HORIZONTAL = 0, MOTOR_VERTICAL = 1 };
enum MotorSpeed { SPEED_LOW = 0, SPEED_MID, SPEED_HIGH };
enum MotorDirect { DIRECT_FORWARD = 0, DIRECT_BACKWARD };

class IMotorDriver {
public:
    virtual ~IMotorDriver() = default;
    virtual bool init() = 0;
    virtual void deinit() = 0;

    virtual bool move(MotorDirection dir, float angleDeg,
                      MotorSpeed speed, MotorDirect direct) = 0;
    virtual bool startBoardTest(MotorDirection dir, unsigned int cycles,
                                int subdivide, MotorSpeed speed, MotorDirect direct) = 0;
    virtual void stop(MotorDirection dir) = 0;
    virtual float getPosition(MotorDirection dir) = 0;
    virtual bool isInitialized() const = 0;
};

} // namespace ft
```

### 4.6 IHallDriver — 霍尔传感器抽象

```cpp
// platforms/common/interface/IHallDriver.h
#pragma once

namespace ft {

class IHallDriver {
public:
    virtual ~IHallDriver() = default;
    virtual bool init() = 0;
    virtual void deinit() = 0;

    // 读取当前磁场强度值
    virtual float readValue() = 0;

    // 是否已初始化
    virtual bool isInitialized() const = 0;
};

} // namespace ft
```

### 4.7 IDisplayDriver — 显示抽象

```cpp
// platforms/common/interface/IDisplayDriver.h
#pragma once
#include <cstdint>

namespace ft {

class IDisplayDriver {
public:
    virtual ~IDisplayDriver() = default;
    virtual bool init() = 0;
    virtual void deinit() = 0;

    // 每帧调用，返回下次调用前可睡眠的毫秒数
    virtual uint32_t taskHandler() = 0;

    virtual void setBatteryPercent(int pct) = 0;
    virtual void setBatteryModel(const char* model) = 0;
    virtual void setKeyValid(bool valid) = 0;
    virtual bool isInitialized() const = 0;
};

} // namespace ft
```

### 4.8 IGpioDriver — GPIO 抽象

```cpp
// platforms/common/interface/IGpioDriver.h
#pragma once
#include <string>

namespace ft {

class IGpioDriver {
public:
    virtual ~IGpioDriver() = default;

    virtual bool exportGpio(int gpio) = 0;
    virtual bool setDirection(int gpio, const std::string&amp; dir) = 0;
    virtual bool setEdge(int gpio, const std::string&amp; edge) = 0;
    virtual bool write(int gpio, int value) = 0;
    virtual int  read(int gpio) = 0;
    virtual bool unexport(int gpio) = 0;
};

} // namespace ft
```

### 4.9 IRecorder — 录像抽象（从现有 IRecorder.h 迁移，基本不变）

```cpp
// platforms/common/interface/IRecorder.h
#pragma once
#include <cstdint>

namespace ft {

struct RecorderCmd {
    uint8_t cmd        = 0;  // 0=start, 1=pause, 2=recover, 3=stop
    uint8_t cmd_origin = 0;
    uint8_t ai_flag    = 0;
    uint8_t fence_flag = 0;
};

class IRecorder {
public:
    virtual ~IRecorder() = default;
    virtual bool start(const RecorderCmd&amp; cmd) = 0;
    virtual bool stop() = 0;
    virtual bool isRecording() const = 0;
};

} // namespace ft
```

### 4.10 IWifiManager — WiFi 管理抽象（BLE模块用）

```cpp
// ble/include/IWifiManager.h
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace ft {

struct WifiCfg {
    char ssid[64];
    char pwd[64];
    char ip_addr[16];
};

struct WifiInfo {
    char ssid[64];
    int  rssi;
};

class IWifiManager {
public:
    virtual ~IWifiManager() = default;

    virtual std::string getStaIp(const char* ifname) = 0;
    virtual int getStaCount(const char* interface) = 0;
    virtual std::vector<uint8_t> getWifiCfg(const char* interface, int&amp; outLen) = 0;
    virtual std::string scanWifi() = 0;
    virtual int connectHotspot(const uint8_t* data, size_t len) = 0;
    virtual int disconnectHotspot() = 0;

    using StateCallback = int (*)(uint8_t state, void* info);
    virtual int registerStateCallback(StateCallback handler) = 0;
};

} // namespace ft
```

---

## 5. 配置系统设计

### 5.1 platform.json — 硬件资源映射

```json
{
  "platform": "rv1126b",
  "version": 1,

  "gpio": {
    "led": {
      "white": 170,
      "red": 169,
      "status_a": 144,
      "status_b": 89
    },
    "buzzer": 93,
    "backlight": 92,
    "lock_detect": 98,
    "hall": {
      "upper_limit": 181,
      "lower_limit": 182
    }
  },

  "i2c": {
    "cam0": { "bus": 3, "addr": "0x29", "otp": "/proc/otp_eeprom-3-50" },
    "cam1": { "bus": 4, "addr": "0x29", "otp": "/proc/otp_eeprom-4-50" },
    "charger": { "bus": 2, "addr": "0x6a", "probe_reg": "0x14", "probe_mask": "0x38", "probe_expect": 4 },
    "fuel_gauge": { "bus": 5, "addr": "0x64", "probe_reg": "0x00", "probe_expect": "0xa0" }
  },

  "battery": {
    "driver": "cw221x",
    "sysfs_path": "/sys/class/power_supply/cw221X-bat/uevent",
    "model_path": "/sys/class/power_supply/cw221X-bat/model_name",
    "dev_node": "/dev/cw221X-bat"
  },

  "motor": {
    "driver": "tmi8152",
    "device_path": "/dev/tmi8152",
    "spi_bus": 0,
    "directions": {
      "horizontal": { "channel": "CH12" },
      "vertical":   { "channel": "CH34" }
    }
  },

  "display": {
    "driver": "lvgl_fb",
    "width": 428,
    "height": 156,
    "font_path": "D:Rajdhani/Rajdhani-SemiBold-5.ttf",
    "font_label_size": 48,
    "font_timer_size": 52
  },

  "audio": {
    "mic_card": 1,
    "mic_device": "hw:1,0",
    "sample_rate": 48000,
    "format": "S32_LE",
    "channels": 2,
    "pdm_gain_control": "PDM0 Gain Volume 0"
  },

  "video": {
    "encoder": "rockchip_mpp",
    "recorder_config": "/oem/usr/conf/recorder.json"
  },

  "storage": {
    "tf_partition": "/dev/mmcblk1p1",
    "tf_mount_point": "/mnt/sdcard",
    "prod_dir": "/userdata/prod"
  },

  "paths": {
    "sn_file": "/device_data/pcba.txt",
    "info_ini": "/device_data/info.ini",
    "factory_flag": "/userdata/factory_mode",
    "aging_time_conf": "/userdata/aging_time.conf",
    "aging_result": "/userdata/aging_result.json",
    "hall_threshold_ini": "/userdata/hall_threshold.ini",
    "key_valid_flag": "/userdata/key_valid",
    "version_file": "/oem/usr/conf/version.txt",
    "power_off_script": "/oem/usr/scripts/power_off.sh"
  },

  "input": {
    "key_devices": [
      { "path": "/dev/input/event0", "name": "adc-m-keys", "keys": ["F15","F16","F17"] },
      { "path": "/dev/input/event1", "name": "adc-ab-keys", "keys": ["F13","F14"] },
      { "path": "/dev/input/event2", "name": "gpio-keys", "keys": ["power"] }
    ],
    "key_codes": ["0x290", "KEY_F13", "KEY_F14", "KEY_F15", "KEY_F16", "KEY_F17"]
  },

  "soc_test": {
    "gpu": {
      "devfreq_path": "/sys/class/devfreq/27800000.gpu",
      "load_path": "/sys/class/devfreq/27800000.gpu/load",
      "load_keyword": "100@"
    },
    "ddr": {
      "devfreq_path": "/sys/class/devfreq/dmc",
      "target_freq_hz": 2112000000
    },
    "cpu": {
      "governor_path": "/sys/devices/system/cpu/cpu5/cpufreq/scaling_governor"
    },
    "npu": {
      "load_path": "/sys/kernel/debug/rknpu/load",
      "load_keyword": "Core0: 99%, Core1: 99%"
    },
    "stress_tools_dir": "/oem/usr/bin"
  },

  "wifi": {
    "sta_interface": "wlan0",
    "ap_interface": "wlan1",
    "hostapd_conf": "/tmp/wps_hostapd.conf",
    "ap_ip": "192.168.5.1",
    "usb_rndis": {
      "interface": "usb0",
      "ip": "172.16.110.6"
    }
  },

  "ble": {
    "name_prefix": "Xbt-F-",
    "manufacturer_id": 65535
  }
}
```

### 5.2 tests.json — 测试项注册表

```json
{
  "version": 1,
  "tests": [
    { "topic": "10R", "module": "rtc",        "async": false, "desc": "RTC + charger IC + fuel gauge" },
    { "topic": "11R", "module": "mic",        "async": true,  "desc": "Mic record + buzzer + audio analysis" },
    { "topic": "12R", "module": "soc",        "async": true,  "desc": "GPU/DDR/CPU/NPU/eMMC stress test" },
    { "topic": "14R", "module": "tfcard",     "async": true,  "desc": "TF card write speed test" },
    { "topic": "15R", "module": "battery",    "async": false, "desc": "Battery voltage check" },
    { "topic": "16R", "module": "wifi_scan",  "async": false, "desc": "WiFi scan test" },
    { "topic": "17R", "module": "key",        "async": true,  "desc": "Key test (PCBA mode, any 2 of 6)" },
    { "topic": "18R", "module": "camera",     "async": true,  "desc": "Camera0 board test", "params": { "cam_index": 0 } },
    { "topic": "20R", "module": "key",        "async": true,  "desc": "Key test (system mode, all 6 keys)" },
    { "topic": "21R", "module": "hall_limit", "async": true,  "desc": "Vertical hall limit switch test" },
    { "topic": "22R", "module": "camera",     "async": true,  "desc": "Camera1 system test", "params": { "cam_index": 1 } },
    { "topic": "23R", "module": "mic",        "async": true,  "desc": "Mic test (variant)" },
    { "topic": "24R", "module": "battery",    "async": false, "desc": "Battery voltage check (variant)" },
    { "topic": "26R", "module": "lock_detect","async": true,  "desc": "Lock toggle detect (GPIO 98)" },
    { "topic": "27R", "module": "hall_360",   "async": true,  "desc": "Horizontal hall 360° calibration" },
    { "topic": "28R", "module": "camera",     "async": true,  "desc": "Both cameras comprehensive test" },
    { "topic": "30R", "module": "aging_full", "async": false, "desc": "Full aging test start" },
    { "topic": "31R", "module": "heartbeat",  "async": false, "desc": "Heartbeat echo" },
    { "topic": "32R", "module": "aging_motor","async": false, "desc": "Motor-only aging start" },
    { "topic": "34R", "module": "aging_stop", "async": false, "desc": "Aging stop (normal)" },
    { "topic": "36R", "module": "aging_fail", "async": false, "desc": "Aging mark failed" },
    { "topic": "37R", "module": "wifi_connect","async": true, "desc": "WiFi connect with SSID/PWD" },
    { "topic": "50R", "module": "shutdown",   "async": false, "desc": "System shutdown" }
  ]
}
```

### 5.3 PlatformConfig — C++ 配置访问类

```cpp
// config/include/PlatformConfig.h
#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace ft {

class PlatformConfig {
public:
    static PlatformConfig&amp; instance();

    bool loadFromFile(const std::string&amp; path);
    bool load(const nlohmann::json&amp; j);

    // GPIO
    int gpioLedWhite() const;
    int gpioLedRed() const;
    int gpioStatusA() const;
    int gpioStatusB() const;
    int gpioBuzzer() const;
    int gpioBacklight() const;
    int gpioLockDetect() const;

    // I2C
    struct I2cDevice { int bus; int addr; std::string otp; };
    I2cDevice i2cCam(int index) const;

    // Battery
    std::string batterySysfsPath() const;
    std::string batteryModelPath() const;

    // Motor
    std::string motorDevicePath() const;

    // Display
    int displayWidth() const;
    int displayHeight() const;
    std::string displayFontPath() const;

    // Paths
    std::string pathSnFile() const;
    std::string pathFactoryFlag() const;
    std::string pathAgingTimeConf() const;
    std::string pathHallThresholdIni() const;
    std::string pathVersionFile() const;
    std::string pathPowerOffScript() const;

    // WiFi
    std::string wifiStaInterface() const;
    std::string wifiApInterface() const;

    // Raw access for extensibility
    const nlohmann::json&amp; raw() const { return root_; }

private:
    nlohmann::json root_;
    bool loaded_ = false;
};

} // namespace ft
```

---

## 6. CMake 构建系统

### 6.1 根 CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)
project(FactoryTestFirmware LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

# =================================================================
# 平台选择
# =================================================================
set(TARGET_PLATFORM "rv1126b" CACHE STRING "Target platform: rv1126b, rk3588, null")
set_property(CACHE TARGET_PLATFORM PROPERTY STRINGS rv1126b rk3588 null)

message(STATUS "Target platform: ${TARGET_PLATFORM}")

# 加载平台专用编译选项
include(cmake/platforms/${TARGET_PLATFORM}.cmake OPTIONAL)

# =================================================================
# 第三方库
# =================================================================
add_subdirectory(third_party/lvgl)

# Mosquitto（预编译或从源码）
find_package(Mosquitto REQUIRED)

# =================================================================
# 子模块（按依赖顺序）
# =================================================================
add_subdirectory(common)           # 通用工具
add_subdirectory(config)           # 配置系统
add_subdirectory(core)             # 核心框架
add_subdirectory(platforms/common) # 通用接口 + 空实现

# 平台实现（根据 TARGET_PLATFORM 选择）
add_subdirectory(platforms/${TARGET_PLATFORM})

add_subdirectory(hal)              # 平台无关 HAL
add_subdirectory(tests)            # 测试模块
add_subdirectory(ble)              # BLE模块

# =================================================================
# 主可执行文件
# =================================================================
add_executable(Falcon_Air_Factory main.cpp)

target_link_libraries(Falcon_Air_Factory
    factory_common
    factory_config
    factory_core
    factory_platform_common
    factory_platform_${TARGET_PLATFORM}
    factory_hal
    factory_tests
    factory_ble
    lvgl
    mosquitto
)

# 平台额外链接库（由 platforms/<platform>/CMakeLists.txt 导出变量）
if(PLATFORM_EXTRA_LIBS)
    target_link_libraries(Falcon_Air_Factory ${PLATFORM_EXTRA_LIBS})
endif()

# =================================================================
# 工具程序
# =================================================================
add_executable(factory_test_runner tools/factory_test_runner.cpp)
target_link_libraries(factory_test_runner mosquitto)

add_executable(soc_test tools/soc_test.cpp)
target_link_libraries(soc_test mosquitto)
```

### 6.2 platforms/rv1126b/CMakeLists.txt

```cmake
# platforms/rv1126b/CMakeLists.txt

# Rockchip MPP 硬编码库
find_package(RockchipMpp REQUIRED)

set(RV1126B_HAL_SRCS
    Rv1126bPlatform.cpp
    hal/RkMppEncoder.cpp
    hal/Gc4663CameraDriver.cpp
    hal/Cw221xBatteryDriver.cpp
    hal/Tmi8152MotorDriver.cpp
    hal/Tmi8152MotorHal.c
    hal/HallSwitchDriver.cpp
    hal/LvglDisplayDriver.cpp
    hal/Rv1126bRecorder.cpp
    hal/Rv1126bWifiManager.cpp
)

add_library(factory_platform_rv1126b STATIC ${RV1126B_HAL_SRCS})

target_include_directories(factory_platform_rv1126b PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/hal
    ${CMAKE_SOURCE_DIR}/platforms/common/interface
    ${CMAKE_SOURCE_DIR}/config/include
    ${CMAKE_SOURCE_DIR}/common/include
    ${CMAKE_SOURCE_DIR}/third_party/tmi8152
    ${CMAKE_SOURCE_DIR}/third_party/mosquitto/out/include
)

# RK WiFi API 头文件路径（从 SDK 获取）
target_include_directories(factory_platform_rv1126b PRIVATE
    $ENV{SDK_DIR}/external/rkwifibt/include
)

target_link_libraries(factory_platform_rv1126b PUBLIC
    factory_platform_common
    rockchip_mpp
)

# 导出额外链接库给主 CMake
set(PLATFORM_EXTRA_LIBS "rockchip_mpp" PARENT_SCOPE)
```

### 6.3 platforms/null/CMakeLists.txt（x86 CI 测试）

```cmake
# platforms/null/CMakeLists.txt

set(NULL_SRCS
    NullPlatform.cpp
)

add_library(factory_platform_null STATIC ${NULL_SRCS})

target_include_directories(factory_platform_null PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/platforms/common/interface
    ${CMAKE_SOURCE_DIR}/config/include
)

target_link_libraries(factory_platform_null PUBLIC
    factory_platform_common
)
```

---

## 7. 代码迁移映射

### 7.1 现有文件 → 新位置 完整映射表

| 现有文件 | 新位置 | 迁移说明 |
|----------|--------|----------|
| `main.cpp` | `main.cpp` | 重写：从 PlatformConfig 读取配置，创建 IPlatform，通过 TestRegistry 动态注册测试 |
| `include/common/Types.h` | `common/include/Types.h` | 直接迁移，移除 LED GPIO 硬编码（改为从配置读取） |
| `include/common/ShellUtils.h` | `common/include/ShellUtils.h` | 直接迁移 |
| `src/common/ShellUtils.cpp` | `common/src/ShellUtils.cpp` | 直接迁移（注意 `src/common/` 目录实际不存在，需新建） |
| `include/core/TestEngine.h` | `core/include/TestEngine.h` | 直接迁移 |
| `include/core/AsyncTaskQueue.h` | `core/include/AsyncTaskQueue.h` | 直接迁移 |
| `src/core/TestEngine.cpp` | `core/src/TestEngine.cpp` | 修改：LED GPIO 从配置读取；`startLedThread()` 不再硬编码 144/89 |
| `src/core/AsyncTaskQueue.cpp` | `core/src/AsyncTaskQueue.cpp` | 直接迁移 |
| `include/hal/GpioController.h` | → 删除，替换为 `IGpioDriver` | `SysfsGpioDriver` 实现相同功能 |
| `src/hal/GpioController.cpp` | `platforms/common/impl/SysfsGpioDriver.cpp` | 迁移为 `IGpioDriver` 实现 |
| `include/hal/I2cController.h` | `hal/include/I2cController.h` | 保留（通用 i2cget/i2cset 封装） |
| `src/hal/I2cController.cpp` | `hal/src/I2cController.cpp` | 直接迁移 |
| `include/hal/MotorController.h` | → 删除，替换为 `IMotorDriver` | 测试代码直接调用 `IMotorDriver` |
| `src/hal/MotorController.cpp` | `platforms/rv1126b/hal/Tmi8152MotorDriver.cpp` | 重写：实现 `IMotorDriver` 接口 |
| `include/hal/MotorHal.h` | `platforms/rv1126b/hal/Tmi8152MotorHal.h` | 直接迁移 |
| `src/hal/MotorHal.c` | `platforms/rv1126b/hal/Tmi8152MotorHal.c` | 直接迁移 |
| `include/hal/HallSwitch.h` | `platforms/rv1126b/hal/HallSwitchDriver.h` | 改写为 C++ class，实现 `IHallDriver` |
| `src/hal/HallSwitch.c` | `platforms/rv1126b/hal/HallSwitchDriver.cpp` | 改写为 C++ class |
| `include/hal/FactoryDisplay.h` | `platforms/common/interface/IDisplayDriver.h` + `platforms/rv1126b/hal/LvglDisplayDriver.h` | 抽象为接口 + 实现 |
| `src/hal/FactoryDisplay.cpp` | `platforms/rv1126b/hal/LvglDisplayDriver.cpp` | 迁移：分辨率/字体/背光GPIO从配置读取 |
| `include/hal/MppEncoder.h` | `platforms/rv1126b/hal/RkMppEncoder.h` | 保留为 RV1126B 专用 |
| `src/hal/MppEncoder.cpp` | `platforms/rv1126b/hal/RkMppEncoder.cpp` | 直接迁移，实现 `IEncoder` |
| `include/hal/V4l2Recorder.h` | `hal/include/V4l2Recorder.h` | 保留：改为持有 `IEncoder*` 注入点 |
| `src/hal/V4l2Recorder.cpp` | `hal/src/V4l2Recorder.cpp` | 修改：不再直接创建 MppEncoder，通过注入 |
| `include/hal/IRecorder.h` | `platforms/common/interface/IRecorder.h` | 直接迁移 |
| `include/hal/RecorderController.h` | `hal/include/RecorderController.h` | 直接迁移 |
| `src/hal/RecorderController.cpp` | `hal/src/RecorderController.cpp` | 直接迁移 |
| `src/hal/NullRecorder.cpp` | `platforms/common/impl/NullRecorder.cpp` | 迁移为空实现 |
| `include/hal/AudioCapture.h` | `hal/include/AudioCapture.h` | 保留（通用 ALSA 封装） |
| `src/hal/AudioCapture.cpp` | `hal/src/AudioCapture.cpp` | 直接迁移 |
| `include/hal/G711Encoder.h` | `hal/include/G711Encoder.h` | 保留（纯软件算法） |
| `src/hal/G711Encoder.cpp` | `hal/src/G711Encoder.cpp` | 直接迁移 |
| `include/hal/Mp4Muxer.h` | `hal/include/Mp4Muxer.h` | 保留（通用封装） |
| `src/hal/Mp4Muxer.cpp` | `hal/src/Mp4Muxer.cpp` | 直接迁移 |
| `include/tests/*.h` | `tests/include/*.h` | 直接迁移 |
| `src/tests/BatteryTest.cpp` | `tests/src/BatteryTest.cpp` | 修改：电池路径从 PlatformConfig 读取 |
| `src/tests/CameraTest.cpp` | `tests/src/CameraTest.cpp` | 修改：摄像头参数从 PlatformConfig / ICameraDriver 读取 |
| `src/tests/WifiTest.cpp` | `tests/src/WifiTest.cpp` | 修改：网卡名从 PlatformConfig 读取 |
| `src/tests/RtcTest.cpp` | `tests/src/RtcTest.cpp` | 修改：I2C 地址从 PlatformConfig 读取 |
| `src/tests/KeyTest.cpp` | `tests/src/KeyTest.cpp` | 修改：input 设备路径从 PlatformConfig 读取 |
| `src/tests/MicTest.cpp` | `tests/src/MicTest.cpp` | 修改：buzzer GPIO / ALSA card 从 PlatformConfig 读取 |
| `src/tests/SocTest.cpp` | `tests/src/SocTest.cpp` | 修改：devfreq 路径 / 压测工具路径从 PlatformConfig 读取 |
| `src/tests/TfCardTest.cpp` | `tests/src/TfCardTest.cpp` | 修改：分区路径从 PlatformConfig 读取 |
| `src/tests/MotorTest.cpp` | `tests/src/MotorTest.cpp` | 修改：调用 `IMotorDriver` 接口 |
| `src/tests/HallTest.cpp` | `tests/src/HallTest.cpp` | 修改：GPIO号优先从 PlatformConfig 读取（已有 ini 回退） |
| `src/tests/AgingTest.cpp` | `tests/src/AgingTest.cpp` | 修改：WiFi/录像路径从 PlatformConfig 读取 |
| `src/tests/SysTest.cpp` | `tests/src/SysTest.cpp` | 修改：GPIO号 / 脚本路径从 PlatformConfig 读取 |
| `include/ble/*.h` | `ble/include/*.h` | 直接迁移 |
| `src/ble/BleAdvertiser.cpp` | `ble/src/BleAdvertiser.cpp` | 直接迁移（通用 BlueZ D-Bus） |
| `src/ble/BleGattServer.cpp` | `ble/src/BleGattServer.cpp` | 直接迁移（通用 BlueZ D-Bus） |
| `src/ble/BleMqttBridge.cpp` | `ble/src/BleMqttBridge.cpp` | 直接迁移（通用 mosquitto） |
| `src/ble/BleDeviceInfo.cpp` | `ble/src/BleDeviceInfo.cpp` | 修改：路径从 PlatformConfig 读取 |
| `src/ble/BleUtility.cpp` | `ble/src/BleUtility.cpp` | 直接迁移 |
| `src/ble/BleWifiManager.cpp` | `platforms/rv1126b/hal/Rv1126bWifiManager.cpp` | 重写：实现 `IWifiManager` 接口，调用 `Rk_wifi_*` |
| `src/ble/BleWifiManager.h` | → 删除，替换为 `IWifiManager.h` | BLE 模块通过接口调用 |
| `src/ble/main.cpp` | `ble/src/main.cpp` | 修改：创建对应平台的 `IWifiManager` 实例 |
| `src/ble/Makefile` | `ble/CMakeLists.txt` | 重写：使用 CMake，条件链接平台库 |
| `btwifi/confs/wps_hostapd_aic.conf` | `btwifi/rv1126b/confs/wps_hostapd_aic.conf` | 直接迁移 |
| `btwifi/scripts/dragonfly_bt_init.sh` | `btwifi/rv1126b/scripts/bt_init.sh` | 重命名 |
| `btwifi/scripts/dragonfly_wifi_init.sh` | `btwifi/rv1126b/scripts/wifi_init.sh` | 重命名 |
| `configs/recorder.json` | `configs/rv1126b/recorder.json` | 直接迁移 |
| `configs/hall_threshold.ini` | `configs/rv1126b/hall_threshold.ini` | 直接迁移 |
| `configs/gc4663_default_default.json` | `configs/rv1126b/gc4663_default.json` | 重命名 |
| `build_factory.sh` | `build_factory.sh` | 修改：根据 TARGET_PLATFORM 分支打包逻辑 |
| `CMakeLists.txt` | `CMakeLists.txt` | 重写：平台选择 + 子目录添加 |

---

## 8. 分阶段实施计划

### Phase 0: 准备工作（3天）

- [ ] 创建 `platforms/common/interface/` 目录，定义所有纯虚接口
- [ ] 创建 `config/` 模块，实现 `PlatformConfig` 单例
- [ ] 设计 `platforms/rv1126b/platform.json` 和 `configs/rv1126b/tests.json`
- [ ] 搭建新 CMake 框架（先不迁移代码，仅建目录和空文件）

**验收标准**：`cmake -DTARGET_PLATFORM=rv1126b ..` 能配置通过（空 target）

### Phase 1: 配置外提（1周）

- [ ] 将 `main.cpp` 中所有硬编码资源改为 `PlatformConfig::instance()` 读取
- [ ] `TestEngine::startLedThread()` LED GPIO 从配置读取
- [ ] `FactoryDisplay` 分辨率/字体/背光GPIO 从配置读取
- [ ] `BatteryTest` 电池路径从配置读取
- [ ] `TfCardTest` 分区路径从配置读取
- [ ] `SysTest` GPIO号/脚本路径从配置读取
- [ ] `KeyTest` input 设备路径从配置读取
- [ ] `MicTest` buzzer GPIO / ALSA card 从配置读取

**验收标准**：RV1126B 全量产测通过，所有测试项无回归

### Phase 2: HAL 接口化（2周）

- [ ] 实现 `SysfsGpioDriver`（替换 `GpioController`）
- [ ] 实现 `Tmi8152MotorDriver`（实现 `IMotorDriver`）
- [ ] 实现 `HallSwitchDriver`（实现 `IHallDriver`）
- [ ] 实现 `Cw221xBatteryDriver`（实现 `IBatteryDriver`）
- [ ] 实现 `Gc4663CameraDriver`（实现 `ICameraDriver`）
- [ ] 实现 `LvglDisplayDriver`（实现 `IDisplayDriver`）
- [ ] 实现 `RkMppEncoder`（实现 `IEncoder`）
- [ ] 实现 `Rv1126bWifiManager`（实现 `IWifiManager`）
- [ ] `V4l2Recorder` 改为注入 `IEncoder*`

**验收标准**：RV1126B 全量产测通过；编译产物大小变化 < 10%

### Phase 3: 测试模块动态注册（1周）

- [ ] 新增 `TestRegistry` 类，支持从 `tests.json` 动态加载
- [ ] 每个测试模块实现 `ITestModule` 接口
- [ ] 新增 `REGISTER_TEST_MODULE` 宏，支持自注册
- [ ] `main.cpp` 废除 `register_xxx_tests()` 硬编码调用，改为 `TestRegistry::loadFromJson()`

**验收标准**：`tests.json` 中增减测试项无需改 C++ 代码即可生效

### Phase 4: 目录重构 + BLE CMake 化（1周）

- [ ] 按新目录结构移动所有源文件
- [ ] 重写根 `CMakeLists.txt`，实现平台选择
- [ ] `src/ble/Makefile` 迁移为 `ble/CMakeLists.txt`
- [ ] `btwifi/` 按平台拆分为 `btwifi/rv1126b/`
- [ ] 更新 `build_factory.sh`，支持 `TARGET_PLATFORM` 参数

**验收标准**：`./build.sh rv1126b` 一键编译通过；产测全量通过

### Phase 5: x86/null 平台验证（1周）

- [ ] 实现 `NullPlatform` + 所有 `Null*Driver`
- [ ] 在 x86/Linux 上编译通过
- [ ] 编写 MQTT 协议层单元测试（无需硬件）
- [ ] 接入 CI（GitHub Actions / 内部 Jenkins）

**验收标准**：`cmake -DTARGET_PLATFORM=null .. && make` 在 x86 编译通过；单元测试通过

### Phase 6: RK3588 适配验证（2-4周，未来）

- [ ] 新建 `platforms/rk3588/` 目录
- [ ] 实现 `Rk3588Platform` + `RkMppEncoderV2`
- [ ] 实现 `Imx415CameraDriver`（或复用 `Gc4663CameraDriver`）
- [ ] 实现 `DrmDisplayDriver`（替代 LVGL framebuffer）
- [ ] 编写 `platforms/rk3588/platform.json`
- [ ] 全量产测验证

---

## 9. 风险与缓解

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|----------|
| 接口设计不满足下平台 | 高 | 中 | Phase 5 用 x86/null 验证接口完备性；Phase 6 用 RK3588 验证 |
| 重构引入 regression | 高 | 中 | 每 Phase 保持 RV1126B 全量产测通过；增量替换，不回退 |
| 二进制体积增大 | 低 | 高 | 纯虚函数仅增加一次间接调用；链接器剔除未引用符号 |
| 性能损耗 | 低 | 高 | 纯虚函数调用在产测场景无感知；必要时可用 CRTP 替代 |
| 配置 JSON 解析失败 | 中 | 低 | `PlatformConfig::load()` 带完整校验；失败时打印明确错误并退出 |
| 并行开发冲突 | 中 | 中 | 先冻结 `main` 分支，在 `feature/platformization` 开发；每日 rebase |
| BLE 模块 WiFi 接口化延迟 | 中 | 低 | 可先保留 `BleWifiManager` 在 `ble/` 内，后续迁移到 `platforms/rv1126b/` |

---

## 附录 A：术语表

| 术语 | 说明 |
|------|------|
| **HAL** | Hardware Abstraction Layer，硬件抽象层 |
| **MPP** | Rockchip Media Process Platform，瑞芯微媒体处理平台 |
| **V4L2** | Video4Linux 2，Linux 视频捕获框架 |
| **GATT** | Generic Attribute Profile，BLE 通用属性协议 |
| **BlueZ** | Linux 官方蓝牙协议栈 |
| **CRTP** | Curiously Recurring Template Pattern，奇异递归模板模式（零开销多态） |
| **SoC** | System on Chip，片上系统 |

## 附录 B：参考资料

- `doc/factory_proto.md` — 产测协议文档
- `doc/ble.md` — BLE 模块文档
- `platforms/common/interface/*.h` — 跨平台接口定义（以代码为准）
- `configs/rv1126b/platform.json` — RV1126B 硬件资源配置（以实际文件为准）
