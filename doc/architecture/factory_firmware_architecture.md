# 产测固件架构分析与平台化方案

> 本文档基于 `Falcon_Air_Factory` 代码库分析，目标是从 **RV1126B 专用固件** 演进为 **跨平台产测框架**。

---

## 一、当前架构总览（RV1126B 专用）

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        外部交互层 (External Interfaces)                       │
│  ┌──────────────┐  ┌──────────────┐  ┌─────────────────────────────────────┐ │
│  │  Host PC     │  │  BLE模块     │  │  MQTT Broker (mosquitto @ localhost)│ │
│  │  (产测上位机) │  │ (独立进程)   │  │  # 通配订阅 + topic路由              │ │
│  └──────────────┘  └──────────────┘  └─────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                           主入口层 (main.cpp)                                │
│  • 版本/SN 打印                                                              │
│  • TestEngine 初始化 + MQTT 启动                                              │
│  • LED 线程启动                                                              │
│  • Motor + Hall 初始化                                                       │
│  • GPIO 初始化 (white=170, red=169)                                          │
│  • LVGL FactoryDisplay 初始化                                                 │
│  • V4L2 Recorder 配置解析 (/oem/usr/conf/recorder.json)                       │
│  • 注册全部测试模块 (12 个 register_xxx_tests)                                 │
│  • 主循环: LVGL taskHandler + 信号处理                                        │
└─────────────────────────────────────────────────────────────────────────────┘
                                       │
                    ┌──────────────────┼──────────────────┐
                    ▼                  ▼                  ▼
┌─────────────────────────┐  ┌─────────────────┐  ┌─────────────────────────┐
│      Core 层            │  │   Common 层     │  │      第三方库            │
│  ┌─────────────────┐    │  │  ┌───────────┐  │  │  ┌─────────────────┐    │
│  │   TestEngine    │    │  │  │ Types.h   │  │  │  │   mosquitto     │    │
│  │  • MQTT客户端    │    │  │  │ • 协议头   │  │  │  │   (MQTT lib)    │    │
│  │  • 命令路由分发   │    │  │  │ • SN/BizID │  │  │  └─────────────────┘    │
│  │  • 异步任务调度   │    │  │  │ • LED模式  │  │  │  ┌─────────────────┐    │
│  │  • LED状态机    │    │  │  └───────────┘  │  │  │   nlohmann/json │    │
│  └─────────────────┘    │  │  ┌───────────┐  │  │  │   (配置解析)     │    │
│  ┌─────────────────┐    │  │  │ShellUtils │  │  │  └─────────────────┘    │
│  │ AsyncTaskQueue  │    │  │  │• shell_exec│  │  │  ┌─────────────────┐    │
│  │  • 2线程线程池   │    │  │  │• read_sysfs│  │  │  │   LVGL          │    │
│  └─────────────────┘    │  │  └───────────┘  │  │  │   (工厂UI)       │    │
│  ┌─────────────────┐    │  └─────────────────┘  │  └─────────────────┘    │
│  │    MsgQueue     │    │                         │  ┌─────────────────┐    │
│  │  • 线程安全队列  │    │                         │  │  rockchip_mpp   │    │
│  └─────────────────┘    │                         │  │  (H.264硬编码)   │    │
└─────────────────────────┘                         │  └─────────────────┘    │
                                                    └─────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                        测试模块层 (Test Modules)                              │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐   │
│  │Battery  │ │ Camera  │ │  Wifi   │ │   RTC   │ │   Key   │ │   Mic   │   │
│  │ (15/24R)│ │(18/22/28│ │(16/37R) │ │  (10R)  │ │(17/20R) │ │(11/23R) │   │
│  └─────────┘ └─────────┘ └─────────┘ └─────────┘ └─────────┘ └─────────┘   │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐               │
│  │   SoC   │ │ TF Card │ │  Motor  │ │  Hall   │ │  Aging  │               │
│  │  (12R)  │ │  (14R)  │ │(191-296R│ │(21/27R) │ │(30-36R) │               │
│  └─────────┘ └─────────┘ └─────────┘ └─────────┘ └─────────┘               │
│  ┌─────────┐                                                                 │
│  │   Sys   │  注册方式: engine.registerTest() / registerRaw()               │
│  │ (26/31/50│  每个测试 = topic + handler + async_flag                       │
│  └─────────┘                                                                 │
└─────────────────────────────────────────────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                          HAL 硬件抽象层                                       │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐ │
│  │GpioController│  │I2cController│  │MotorController│  │   FactoryDisplay   │ │
│  │• sysfs GPIO  │  │• i2c-dev    │  │• TMI8152驱动 │  │• LVGL framebuffer │ │
│  │• 硬编码号    │  │• 读写寄存器  │  │• 水平/垂直   │  │• TTF字体渲染      │ │
│  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────────────┘ │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐ │
│  │  MotorHal   │  │  HallSwitch │  │RecorderCtrl │  │   V4l2Recorder     │ │
│  │• SPI/TMI8152│  │• 霍尔检测    │  │• IRecorder  │  │• V4L2多路采集      │ │
│  └─────────────┘  └─────────────┘  │  注入点      │  │• MppEncoder硬编码  │ │
│  ┌─────────────┐  ┌─────────────┐  │             │  │• AudioCapture      │ │
│  │ MppEncoder  │  │  Mp4Muxer   │  │             │  │• G711Encoder       │ │
│  │• RK MPP API │  │• MP4封装    │  │             │  │• NullRecorder(回退)│ │
│  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                         Linux Kernel / 硬件                                   │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐           │
│  │  GPIO子系统│ │ I2C总线  │ │ V4L2驱动 │ │  ALSA   │ │ Framebuffer│          │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────┘           │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐                       │
│  │ MIPI-CSI │ │ GC4663×2 │ │ CW221X   │ │ TMI8152  │                       │
│  │  (dphy0/3)│ │(I2C3/4)  │ │ 电池管理 │ │ 电机驱动 │                       │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘                       │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 二、当前架构的 RV1126B 专用耦合点

| 层级 | 耦合点 | 具体问题 |
|------|--------|----------|
| **Build** | `CMakeLists.txt` | 硬链接 `rockchip_mpp`，平台判断缺失 |
| **main.cpp** | 初始化顺序 | GPIO号(170/169/144/89)、SN路径、recorder配置路径全部写死 |
| **Core** | LED线程 | GPIO 144/89 硬编码，无抽象 |
| **HAL** | `MppEncoder` | 直接 `#include <rockchip/rk_mpi.h>`，强依赖RK MPP |
| **HAL** | `V4l2Recorder` | 虽然V4L2标准，但MppEncoder注入使其与RK强耦合 |
| **HAL** | `FactoryDisplay` | LVGL配置(`lv_conf.h`)、字体路径、framebuffer设备写死 |
| **HAL** | `MotorController` | 直接依赖 `MotorHal.h` / TMI8152 寄存器定义 |
| **Test** | `CameraTest` | GC4663 I2C地址(0x29)、OTP路径(`/proc/otp_eeprom-*`)、CSI dphy写死 |
| **Test** | `BatteryTest` | `cw221X-bat` 路径写死 |
| **Test** | `WifiTest` | 可能依赖具体WiFi芯片的`iw`/`wpa_cli`命令行 |
| **Test** | `SocTest` | 可能读取RV1126B特定的温度/频率节点 |
| **Common** | `ShellUtils` | 多处直接调用 `shell_exec("dmesg | grep ...")`，可移植性差 |

---

## 三、平台化目标架构

核心思想：**配置驱动 + 接口抽象 + 平台插件**

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          应用层 (Application)                                 │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                     main.cpp (通用入口)                              │   │
│  │  • 解析 platform.json 选择目标平台                                    │   │
│  │  • 加载 Platform 插件 (.so / 静态链接)                                │   │
│  │  • 初始化 HAL → 注册测试 → 主循环                                    │   │
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
│  └─────────────────┘    │  │  │ • 全局状态 │  │  │  │ • I2C设备表      │    │
│  ┌─────────────────┐    │  │  └───────────┘  │  │  │ • 摄像头参数     │    │
│  │ AsyncTaskQueue  │    │  │  ┌───────────┐  │  │  │ • 测试项开关     │    │
│  └─────────────────┘    │  │  │ShellUtils │  │  │  └─────────────────┘    │
│  ┌─────────────────┐    │  │  │(通用工具) │  │  │  ┌─────────────────┐    │
│  │  TestRegistry   │    │  │  └───────────┘  │  │  │  hw_config.json  │    │
│  │  • 动态测试注册   │◄───┤  │                 │  │  │ • 阈值参数       │    │
│  └─────────────────┘    │  └─────────────────┘  │  │ • 平台特有路径   │    │
└─────────────────────────┘                       │  └─────────────────┘    │
                                                  └─────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                     平台抽象层 (Platform Abstraction)                         │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────────────────┐  │
│  │  IPlatform      │  │  IEncoder       │  │      IDisplayDriver         │  │
│  │  • init()       │  │  • init()       │  │  • init() / taskHandler()   │  │
│  │  • createHAL()  │  │  • encode()     │  │  • setBatteryPercent()      │  │
│  │  • getConfig()  │  │  • deinit()     │  │  • deinit()                 │  │
│  └─────────────────┘  └─────────────────┘  └─────────────────────────────┘  │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────────────────┐  │
│  │  ICameraDriver  │  │  IBatteryDriver │  │      IMotorDriver           │  │
│  │  • probe()      │  │  • readVoltage()│  │  • init() / move() / stop() │  │
│  │  • checkOtp()   │  │  • getPercent() │  │  • getPosition()            │  │
│  └─────────────────┘  └─────────────────┘  └─────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────┘
                                       │
                    ┌──────────────────┼──────────────────┐
                    ▼                  ▼                  ▼
┌─────────────────────────┐  ┌─────────────────┐  ┌─────────────────────────┐
│   platforms/rv1126b/    │  │ platforms/rk3588│  │    platforms/xxx/       │
│  ┌─────────────────┐    │  │  (待扩展)        │  │  ┌─────────────────┐    │
│  │ Rv1126bPlatform │    │  │                 │  │ │ XxxPlatform     │    │
│  │ • 实现IPlatform │    │  │                 │  │ │ • 实现IPlatform │    │
│  └─────────────────┘    │  │                 │  │ └─────────────────┘    │
│  ┌─────────────────┐    │  │                 │  │ ┌─────────────────┐    │
│  │ RkMppEncoder    │    │  │                 │  │ │ SoftEncoder     │    │
│  │ • 实现IEncoder  │    │  │                 │  │ │ • 实现IEncoder  │    │
│  └─────────────────┘    │  │                 │  │ └─────────────────┘    │
│  ┌─────────────────┐    │  │                 │  │ ┌─────────────────┐    │
│  │ Rv1126bDisplay  │    │  │                 │  │ │ XxxDisplay      │    │
│  │ • LVGL+fb       │    │  │                 │  │ │ • DRM/wayland   │    │
│  └─────────────────┘    │  │                 │  │ └─────────────────┘    │
│  ┌─────────────────┐    │  │                 │  │ ┌─────────────────┐    │
│  │ Gc4663CameraDrv │    │  │                 │  │ │ UsbCameraDrv    │    │
│  │ • I2C+OTP       │    │  │                 │  │ │ • UVC标准       │    │
│  └─────────────────┘    │  │                 │  │ └─────────────────┘    │
└─────────────────────────┘  └─────────────────┘  └─────────────────────────┘
```

---

## 四、平台化实施路线图

### Phase 1: 配置外提（低风险，立即可做）

1. **创建 `configs/platform/rv1126b.json`**
   ```json
   {
     "platform": "rv1126b",
     "gpio": {
       "led_white": 170,
       "led_red": 169,
       "led_status_a": 144,
       "led_status_b": 89
     },
     "i2c": {
       "cam0": {"bus": 3, "addr": "0x29", "otp": "/proc/otp_eeprom-3-50"},
       "cam1": {"bus": 4, "addr": "0x29", "otp": "/proc/otp_eeprom-4-50"}
     },
     "battery": {
       "sysfs_path": "/sys/class/power_supply/cw221X-bat/uevent"
     },
     "motor": {
       "driver": "tmi8152",
       "spi_bus": 0
     },
     "display": {
       "backend": "lvgl_fb",
       "font_path": "/oem/usr/fonts/Rajdhani-SemiBold.ttf"
     },
     "encoder": {
       "backend": "rockchip_mpp"
     }
   }
   ```

2. **修改 `main.cpp`**：所有硬编码资源从 `PlatformConfig::instance()` 读取。

3. **修改 `TestEngine::startLedThread()`**：GPIO号从配置读取，不再写死 144/89。

### Phase 2: HAL 接口化（中等风险）

1. **定义纯虚接口**
   - `hal/interface/IEncoder.h` — 视频编码抽象
   - `hal/interface/ICameraDriver.h` — 摄像头探测抽象
   - `hal/interface/IBatteryDriver.h` — 电池读取抽象
   - `hal/interface/IMotorDriver.h` — 电机控制抽象
   - `hal/interface/IDisplayDriver.h` — 显示抽象

2. **现有 HAL 类实现接口**
   - `MppEncoder` → 实现 `IEncoder`
   - `FactoryDisplay` → 实现 `IDisplayDriver`
   - 新增 `Rv1126bBatteryDriver` → 实现 `IBatteryDriver`
   - `MotorController` 拆分：通用逻辑 + `Tmi8152MotorDriver`

3. **引入 `IPlatform` 工厂**
   ```cpp
   class IPlatform {
   public:
       virtual ~IPlatform() = default;
       virtual bool init(const nlohmann::json& config) = 0;
       virtual std::unique_ptr<IEncoder> createEncoder() = 0;
       virtual std::unique_ptr<ICameraDriver> createCameraDriver() = 0;
       virtual std::unique_ptr<IBatteryDriver> createBatteryDriver() = 0;
       virtual std::unique_ptr<IMotorDriver> createMotorDriver() = 0;
       virtual std::unique_ptr<IDisplayDriver> createDisplayDriver() = 0;
   };
   ```

### Phase 3: 测试模块去耦合（中等风险）

1. **测试配置表化**
   - 不再在 `main.cpp` 中逐个 `register_xxx_tests(engine)`
   - 改为从 JSON 加载测试列表：
     ```json
     {
       "tests": [
         {"topic": "15R", "module": "battery", "async": false},
         {"topic": "18R", "module": "camera", "async": true, "params": {"cam_index": 0}},
         {"topic": "22R", "module": "camera", "async": true, "params": {"cam_index": 1}}
       ]
     }
     ```

2. **每个 Test Module 自注册**
   ```cpp
   // BatteryTest.cpp
   static TestModule battery_module{
       .name = "battery",
       .factory = [](TestEngine& e, const json& cfg) {
           e.registerTest(cfg["topic"], [cfg](const ProtoHeader& h) {
               return BatteryDriver::instance()->readTest(cfg, h);
           });
       }
   };
   REGISTER_TEST_MODULE(battery_module);
   ```

### Phase 4: 目录重构与 CMake 平台化（高风险，需完整回归测试）

```
FACTORY_GIT/
├── CMakeLists.txt              # 通用编译入口
├── platforms/
│   ├── common/                 # 通用HAL接口 + 默认实现
│   │   ├── interface/
│   │   ├── NullEncoder.cpp
│   │   └── SoftEncoder.cpp
│   └── rv1126b/                # RV1126B 专用实现
│       ├── CMakeLists.txt
│       ├── Rv1126bPlatform.cpp
│       ├── RkMppEncoder.cpp
│       ├── Gc4663CameraDriver.cpp
│       ├── Tmi8152MotorDriver.cpp
│       └── lv_conf.h
├── core/                       # 完全通用，不依赖平台
├── tests/                      # 通用测试框架 + 平台无关测试逻辑
├── configs/
│   ├── rv1126b/
│   │   ├── platform.json
│   │   └── tests.json
│   └── rk3588/
│       └── ...
└── third_party/                # 第三方库
```

**CMake 平台选择示例：**
```cmake
set(TARGET_PLATFORM "rv1126b" CACHE STRING "Target platform")

if(TARGET_PLATFORM STREQUAL "rv1126b")
    add_subdirectory(platforms/rv1126b)
    target_link_libraries(Falcon_Air_Factory platform_rv1126b rockchip_mpp)
elseif(TARGET_PLATFORM STREQUAL "rk3588")
    add_subdirectory(platforms/rk3588)
    target_link_libraries(Falcon_Air_Factory platform_rk3588 rockchip_mpp)
else()
    add_subdirectory(platforms/common)
    target_link_libraries(Falcon_Air_Factory platform_common)
endif()
```

---

## 五、平台化收益与风险

### 收益
| 维度 | 收益 |
|------|------|
| **新平台适配** | 新SoC（如RK3588、全志）只需实现 `IPlatform` 接口，2-4周可完成 |
| **硬件改版** | 摄像头、电池芯片、电机驱动更换只需改配置 + 对应 driver，无需动框架 |
| **自动化测试** | 配置驱动后，可在x86上运行 `NullPlatform` 做协议层CI测试 |
| **并行开发** | 平台团队与算法/测试团队解耦，接口契约开发 |

### 风险与缓解
| 风险 | 缓解措施 |
|------|----------|
| 重构引入regression | Phase 1/2 逐步替换，每阶段保持RV1126B全量测试通过 |
| 接口设计不满足下平台 | 先完成2个平台（rv1126b + null/x86）验证接口再推广 |
| 二进制体积增大 | 平台代码编译为静态库，链接器会剔除未引用符号 |
| 性能损耗 | 接口为纯虚函数，一次间接调用，产测场景无感知 |

---

## 六、建议的下一步行动

1. **本周**：完成 Phase 1 — 将 `main.cpp` 和 `TestEngine` 中的 GPIO 号、路径等外提到 `platform.json`
2. **下周**：完成 Phase 2 — 定义 `IEncoder` 接口，将 `MppEncoder` 迁移为 `RkMppEncoder`
3. **第3周**：完成 `IBatteryDriver`、`ICameraDriver` 接口化
4. **第4周**：目录重构，验证 `cmake -DTARGET_PLATFORM=rv1126b` 编译通过且产测全通过
5. **后续**：在 x86/Linux 上实现 `NullPlatform`，跑通 MQTT 协议层单元测试

---

*文档生成时间: 2026-05-28*
*基于 commit: `$(git rev-parse --short HEAD)`*
