# 产测固件平台化设计文档

> 版本: v1.0
> 日期: 2026-06-03
> 范围: 从 RV1126B 专用固件演进为跨平台产测框架
> 权威来源: `产测固件架构.docx`（根目录）

---

## 目录

1. [执行摘要](#1-执行摘要)
2. [目录结构与编译入口](#2-目录结构与编译入口)
3. [核心注册机制](#3-核心注册机制)
4. [测试框架](#4-测试框架)
5. [构建系统](#5-构建系统)
6. [Phase 1 实现待办清单](#6-phase-1-实现待办清单)

---

## 1. 执行摘要

当前 `Falcon_Air_Factory` 是面向 **RV1126B + 双路 GC4663 + TMI8152 电机 + CW221X 电池 + AIC8800 WiFi/BT** 的专用产测固件。平台化的目标是：

- **独立化**：厂测脱离用户固件单独存在，成为 Linux + 自包含厂测 app
- **同一套代码框架**支持多种 SoC（RV1126B、RK3576、未来全志/海思等）
- **配置文件驱动**硬件资源（GPIO、I2C、摄像头、电机、电池等）
- **平台插件化**：新平台只需实现一组纯虚接口，2-4 周完成适配
- **硬件改版零侵入**：换摄像头/电池/电机驱动 = 改配置 + 新 driver 实现

**核心设计原则**：

| 原则 | 说明 |
|------|------|
| **接口先行** | 先定义跨平台契约（纯虚接口），再迁移实现 |
| **配置驱动** | 所有硬件资源从 JSON 读取 |
| **渐进重构** | 每阶段保持 RV1126B 全量产测通过，逐步替换 |
| **测试解耦** | 测试逻辑与平台实现分离，通过配置表动态注册 |

---

## 2. 目录结构与编译入口

### 2.1 顶层目录

```
FACTORY_GIT/
├── build.sh                          # 统一编译入口
│   # 用法: PLATFORM=falcon ./build.sh
│   # 产物: build/falcon/
│
├── cmake/
│   └── platforms/
│       ├── falcon.cmake              # RV1126B 交叉编译工具链
│       └── null.cmake                # x86 本地编译工具链
│
├── src/                              ← 现有旧代码（不动）
├── include/                          ← 现有旧代码（不动）
├── ...
│
└── factory_fw/                       ← ★ 新建平台化框架目录
    ├── main.cpp                      # ★ 唯一一份通用入口
    ├── CMakeLists.txt                # 聚合 CMake（可选，IDE 用）
│
    ├── core/                         # TestEngine、AsyncTaskQueue、Registry
    ├── common/                       # Types.h、ShellUtils
    ├── config/                       # PlatformConfig（JSON 配置解析）
    ├── hal/                          # 通用 HAL（I2cController、V4l2Recorder）
    ├── tests/                        # ITestModule、TestContext、各测试模块
    ├── ble/                          # BlueZ GATT/MQTT（通用）
│
    └── platforms/
        ├── common/
        │   ├── interface/            # 9 个纯虚接口
        │   ├── impl/                 # Null* 空实现 + SysfsGpioDriver
        │   └── CMakeLists.txt
        │
        └── falcon/                   # Falcon (RV1126B)
            ├── CMakeLists.txt        # 自包含 CMake（独立/聚合两用）
            ├── build_factory.sh      # 平台打包脚本
            ├── platform.json         # 硬件资源配置
            └── drivers/              # 平台 driver 实现
                ├── RkMppEncoder.cpp
                ├── Gc4663CameraDriver.cpp
                ├── Cw221xBatteryDriver.cpp
                └── ...
```

### 2.2 关键约定

- `factory_fw/` 与现有 `src/`/`include/` **完全并行**，旧代码不动
- `factory_fw/main.cpp` 是**唯一一份**通用入口，平台 CMake 通过绝对路径引用
- 平台 driver 目录命名 `drivers/`（与通用 `hal/` 区分）
- 构建产物落在 `build/${PLATFORM}/`，不污染源码树

### 2.3 build.sh（统一编译入口）

```bash
#!/bin/bash
set -euo pipefail

PLATFORM="${PLATFORM:-falcon}"
PLATFORM_LC=$(echo "$PLATFORM" | tr '[:upper:]' '[:lower:]')
PLATFORM_DIR="factory_fw/platforms/${PLATFORM_LC}"
BUILD_DIR="build/${PLATFORM_LC}"

[[ -d "$PLATFORM_DIR" ]] || { echo "Error: platform '$PLATFORM' not found" >&2; exit 1; }

TOOLCHAIN_FILE="$(realpath "cmake/platforms/${PLATFORM_LC}.cmake")"
[[ -f "$TOOLCHAIN_FILE" ]] || { echo "Error: toolchain not found: $TOOLCHAIN_FILE" >&2; exit 1; }

cmake -S "$PLATFORM_DIR" -B "$BUILD_DIR" \
      -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
      -DFW_ROOT="$(realpath factory_fw)"

cmake --build "$BUILD_DIR" --parallel

"$(realpath "$PLATFORM_DIR/build_factory.sh")" "$(realpath "$BUILD_DIR")"
```

### 2.4 聚合 CMake（factory_fw/CMakeLists.txt）

```cmake
cmake_minimum_required(VERSION 3.16)
project(FactoryFirmware)

# 通用框架（只 add 一次）
add_subdirectory(core)
add_subdirectory(common)
add_subdirectory(config)
add_subdirectory(hal)
add_subdirectory(tests)
add_subdirectory(platforms/common)

# 平台选择
set(TARGET_PLATFORM "falcon" CACHE STRING "Target platform")
add_subdirectory("platforms/${TARGET_PLATFORM}")
```

### 2.5 平台自包含 CMake（platforms/falcon/CMakeLists.txt）

```cmake
cmake_minimum_required(VERSION 3.16)
project(FalconFactory LANGUAGES C CXX)

set(FW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# 独立编译模式：如果上层没 add，就自己 add
if(NOT TARGET factory_core)
    add_subdirectory(${FW_ROOT}/core          ${CMAKE_BINARY_DIR}/fw_core)
    add_subdirectory(${FW_ROOT}/common        ${CMAKE_BINARY_DIR}/fw_common)
    add_subdirectory(${FW_ROOT}/config        ${CMAKE_BINARY_DIR}/fw_config)
    add_subdirectory(${FW_ROOT}/hal           ${CMAKE_BINARY_DIR}/fw_hal)
    add_subdirectory(${FW_ROOT}/tests         ${CMAKE_BINARY_DIR}/fw_tests)
    add_subdirectory(${FW_ROOT}/platforms/common ${CMAKE_BINARY_DIR}/fw_platform_common)
endif()

# 平台 driver（显式列源文件，不用 GLOB）
set(FALCON_DRIVER_SRCS
    drivers/RkMppEncoder.cpp
    drivers/Gc4663CameraDriver.cpp
    drivers/Cw221xBatteryDriver.cpp
    drivers/Tmi8152MotorDriver.cpp
    drivers/HallSwitchDriver.cpp
    drivers/LvglDisplayDriver.cpp
    drivers/SysfsGpioDriver.cpp
    drivers/Rv1126bRecorder.cpp
    drivers/Rv1126bWifiManager.cpp
)
add_library(platform_falcon STATIC ${FALCON_DRIVER_SRCS})

# 主可执行文件
add_executable(factory_test ${FW_ROOT}/main.cpp)
target_link_libraries(factory_test
    factory_core
    factory_common
    factory_config
    factory_hal
    factory_tests
    factory_platform_common
    platform_falcon
)
```

### 2.6 main.cpp（唯一一份通用入口）

```cpp
#include "config/PlatformConfig.h"
#include "core/DriverRegistry.h"
#include "core/TestEngine.h"
#include "core/ModuleRegistry.h"
#include "platforms/common/interface/IPlatform.h"

int main() {
    auto& cfg = PlatformConfig::instance();
    if (!cfg.loadFromFile("/oem/usr/conf/platform.json")) return -1;

    auto platform = ft::createPlatform(cfg.platformName());
    if (!platform || !platform->init(cfg.raw())) return -1;

    ft::DriverRegistry reg;
    platform->registerDrivers(reg);

    ft::TestEngine engine(reg, cfg);
    engine.loadTestConfig("/oem/usr/conf/tests.json");

    engine.run();
    return 0;
}
```

---

## 3. 核心注册机制

### 3.1 FactoryStore — 唯一一份"键→工厂"存储

```cpp
// core/include/FactoryStore.h
#pragma once
#include <unordered_map>
#include <functional>

namespace ft {

template<class Key>
class FactoryStore {
public:
    using FactoryFunc = std::function<void*()>;

    void put(Key k, FactoryFunc f) {
        store_[std::move(k)] = std::move(f);
    }

    void* get(const Key& k) const {
        auto it = store_.find(k);
        return (it != store_.end()) ? it->second() : nullptr;
    }

    bool has(const Key& k) const {
        return store_.count(k);
    }

private:
    std::unordered_map<Key, FactoryFunc> store_;
};

} // namespace ft
```

### 3.2 DriverRegistry — 类型键、编译期类型安全

```cpp
// core/include/DriverRegistry.h
#pragma once
#include "FactoryStore.h"
#include <typeindex>
#include <memory>

namespace ft {

class DriverRegistry {
public:
    template<class Interface>
    void bind(std::function<std::unique_ptr<Interface>()> factory) {
        store_.put(typeid(Interface), [f = std::move(factory)]() -> void* {
            return f().release();
        });
    }

    template<class Interface>
    std::unique_ptr<Interface> create() const {
        void* raw = store_.get(typeid(Interface));
        return std::unique_ptr<Interface>(
            raw ? static_cast<Interface*>(raw) : nullptr);
    }

    template<class Interface>
    bool has() const {
        return store_.has(typeid(Interface));
    }

private:
    FactoryStore<std::type_index> store_;
};

} // namespace ft
```

### 3.3 IPlatform — 能力注册契约

```cpp
// platforms/common/interface/IPlatform.h
#pragma once
#include <nlohmann/json.hpp>

namespace ft {
class DriverRegistry;

class IPlatform {
public:
    virtual ~IPlatform() = default;
    virtual bool init(const nlohmann::json& config) = 0;
    virtual const char* name() const = 0;

    // 核心：平台把自己拥有的 driver 按接口类型登记进 registry
    virtual void registerDrivers(DriverRegistry& reg) = 0;

    virtual const char* gpuTestPath() const    { return nullptr; }
    virtual const char* npuTestPath() const    { return nullptr; }
    virtual const char* stressTestPath() const { return nullptr; }
    virtual const char* emmcTestPath() const   { return nullptr; }
};

using PlatformFactoryFunc = std::unique_ptr<IPlatform>(*)();
bool registerPlatformFactory(const char* name, PlatformFactoryFunc factory);
std::unique_ptr<IPlatform> createPlatform(const char* name);

} // namespace ft
```

### 3.4 平台侧登记示例

```cpp
void Rv1126bPlatform::registerDrivers(DriverRegistry& reg) {
    reg.bind<IEncoder>      ([] { return std::make_unique<RkMppEncoder>(); });
    reg.bind<ICameraDriver> ([] { return std::make_unique<Gc4663CameraDriver>(); });
    reg.bind<IBatteryDriver>([] { return std::make_unique<Cw221xBatteryDriver>(); });
    reg.bind<IGpioDriver>   ([] { return std::make_unique<SysfsGpioDriver>(); });
    // 平台没有电机 → 不 bind<IMotorDriver>()，上层 create<IMotorDriver>() 返回 nullptr
}
```

### 3.5 ModuleRegistry — 字符串键、数据驱动

```cpp
// core/include/ModuleRegistry.h
#pragma once
#include "FactoryStore.h"
#include <string>
#include <memory>

namespace ft {

class ITestModule;

class ModuleRegistry {
public:
    static ModuleRegistry& instance();

    void add(const std::string& name, std::function<std::unique_ptr<ITestModule>()> factory);
    std::unique_ptr<ITestModule> create(const std::string& name) const;
    bool has(const std::string& name) const;

private:
    FactoryStore<std::string> store_;
};

#define REGISTER_TEST_MODULE(name, T) \
    static bool _reg_##T = []{ \
        ModuleRegistry::instance().add(name, []{ return std::make_unique<T>(); }); \
        return true; \
    }();

} // namespace ft
```

### 3.6 组合效果

```
DriverRegistry（按接口类型）          ModuleRegistry（按 module 名）
    │                                        │
    ▼                                        ▼
平台在 registerDrivers() 中登记        测试模块在 .cpp 中用宏自注册
    │                                        │
    ▼                                        ▼
TestContext::create<ICameraDriver>()   ModuleRegistry::create("camera")
    │                                        │
    └── 两者正交：同一个 CameraTest ──────────┘
        在 RV1126B 拿到 Gc4663CameraDriver
        在 RK3576  拿到 Imx415CameraDriver
```

---

## 4. 测试框架

### 4.1 三个核心抽象

```cpp
// tests/include/ITestModule.h
#pragma once
#include "core/TestResult.h"
#include "core/TestContext.h"

namespace ft {

class ITestModule {
public:
    virtual ~ITestModule() = default;
    virtual TestResult run(TestContext& ctx) = 0;
};

} // namespace ft
```

```cpp
// core/include/TestResult.h
#pragma once
#include <string>
#include <nlohmann/json.hpp>

namespace ft {

struct TestResult {
    enum class Status { Pass, Fail, Skipped };
    Status status;
    std::string detail;
    nlohmann::json data;   // 结构化测量值，默认空对象

    static TestResult pass()                    { return {Status::Pass,    "", nlohmann::json::object()}; }
    static TestResult fail(std::string msg)     { return {Status::Fail,    std::move(msg), nlohmann::json::object()}; }
    static TestResult skipped(std::string msg)  { return {Status::Skipped, std::move(msg), nlohmann::json::object()}; }

    bool isPass()     const { return status == Status::Pass; }
    bool isFail()     const { return status == Status::Fail; }
    bool isSkipped()  const { return status == Status::Skipped; }
};

} // namespace ft
```

```cpp
// core/include/TestContext.h
#pragma once
#include <memory>
#include <nlohmann/json.hpp>

namespace ft {

class DriverRegistry;
class PlatformConfig;

class TestContext {
public:
    TestContext(const DriverRegistry& drivers,
                const PlatformConfig& cfg,
                nlohmann::json params)
        : drivers_(drivers), config_(cfg), params_(std::move(params)) {}

    template<class I>
    std::unique_ptr<I> create() const {
        return drivers_.create<I>();
    }

    const PlatformConfig& config() const { return config_; }
    const nlohmann::json& params() const { return params_; }

private:
    const DriverRegistry& drivers_;
    const PlatformConfig& config_;
    nlohmann::json params_;   // 按值持有，每次调用独有
};

} // namespace ft
```

### 4.2 测试模块写法（自注册）

```cpp
// tests/CameraTest.cpp
#include "tests/ITestModule.h"
#include "platforms/common/interface/ICameraDriver.h"

namespace ft {

class CameraTest : public ITestModule {
public:
    TestResult run(TestContext& ctx) override {
        auto cam = ctx.create<ICameraDriver>();
        if (!cam)              return TestResult::skipped("no camera driver");
        if (!cam->probe(0))    return TestResult::fail("camera probe failed");
        if (!cam->checkOtp(0)) return TestResult::fail("otp mismatch");
        return TestResult::pass();
    }
};

REGISTER_TEST_MODULE("camera", CameraTest);   // 按 module 名注册

} // namespace ft
```

### 4.3 TestEngine 调度与并发模型

**并发语义（冻结点）**：
> `AsyncTaskQueue` 为**单 worker 线程**。所有测试（无论 sync/async）均入队由该 worker **串行执行**。sync 测试 enqueue 后阻塞等待结果；async 测试 enqueue 后立即返回。此设计确保：① 回调线程不被长测试阻塞，MQTT 消息持续接收；② 所有硬件访问（I2C、V4L2、GPIO、Motor SPI）由架构保证串行，无需外部协议假设。

```cpp
void TestEngine::dispatch(const std::string& topic, const nlohmann::json& testCfg) {
    auto mod = ModuleRegistry::instance().create(
        testCfg.at("module").get<std::string>());
    if (!mod) {
        publishResult(topic, TestResult::fail("unknown module"));
        return;
    }

    TestContext ctx(drivers_, config_,
                    testCfg.value("params", nlohmann::json::object()));

    if (testCfg.value("async", false)) {
        asyncQueue_.enqueue(
            [mod = std::move(mod), ctx = std::move(ctx), topic, this]() mutable {
                publishResult(topic, mod->run(ctx));
            });
    } else {
        asyncQueue_.enqueueAndWait(
            [mod = std::move(mod), ctx = std::move(ctx), topic, this]() mutable {
                publishResult(topic, mod->run(ctx));
            });
    }
}
```

**线程安全**：`TestEngine` 内部持 `std::mutex mqttMutex_`。`publishResult` 内加锁后调用 `mosquitto_publish`，串行化 sync 分支（MQTT 回调线程）与 async 分支（worker 线程）的 publish 调用。`mosquitto_loop` 的并发安全由 libmosquitto 自身保证（pthread 构建）。

### 4.4 tests.json 驱动调度

```json
{
  "version": 1,
  "tests": [
    { "topic": "10R", "module": "rtc",        "enabled": true,  "order": 1, "async": false },
    { "topic": "15R", "module": "battery",    "enabled": true,  "order": 2, "async": false, "params": { "min_voltage": 3300 } },
    { "topic": "18R", "module": "camera",     "enabled": true,  "order": 3, "async": true,  "params": { "cam_index": 0 } },
    { "topic": "22R", "module": "camera",     "enabled": true,  "order": 4, "async": true,  "params": { "cam_index": 1 } },
    { "topic": "28R", "module": "camera",     "enabled": true,  "order": 5, "async": true,  "params": { "cam_index": 2 } },
    { "topic": "40R", "module": "speaker",    "enabled": false, "order": 6, "async": true }
  ]
}
```

| 操作 | 方式 | core 改动 |
|---|---|---|
| 增加测试 | 新增 `tests/XxxTest.cpp` + `REGISTER_TEST_MODULE("xxx", XxxTest)` + `tests.json` 加一行 | 零 |
| 改 topic 绑定 | 改 `tests.json` 里的 `topic` 字段，不重新编译 | 零 |
| 同一模块多 topic | `tests.json` 里多行指向同一 `module`，带不同 `params` | 零 |
| 修改测试 | 只改对应那一个 `.cpp` | 零 |
| 调参数 | 改 `tests.json` 的 `params` | 零 |
| 停用测试 | `enabled` 置 `false` | 零 |
| 调顺序 | 改 `order` | 零 |

### 4.5 tests 库类型

`tests/` 编译为 **OBJECT library**（`add_library(factory_tests OBJECT ...)`），消费方（`factory_test` 可执行文件）将其对象文件全量链接。OBJECT library 避免了静态库 dead-strip 导致自注册符号被丢弃的问题。

---

## 5. 构建系统

### 5.1 头文件包含约定

所有模块的头文件统一放在 `factory_fw/include/<module>/`，CMake 中各模块：
```cmake
target_include_directories(factory_core PUBLIC
    ${FW_ROOT}/include
)
```

文件树：
```
factory_fw/include/
├── core/
│   ├── TestEngine.h
│   ├── AsyncTaskQueue.h
│   ├── TestResult.h
│   ├── TestContext.h
│   ├── FactoryStore.h
│   ├── DriverRegistry.h
│   └── ModuleRegistry.h
├── tests/
│   └── ITestModule.h
├── platforms/common/interface/
│   ├── IPlatform.h
│   ├── IEncoder.h
│   ├── ICameraDriver.h
│   ├── IBatteryDriver.h
│   ├── IMotorDriver.h
│   ├── IHallDriver.h
│   ├── IDisplayDriver.h
│   ├── IGpioDriver.h
│   ├── IRecorder.h
│   └── IWifiManager.h
├── config/
│   └── PlatformConfig.h
└── common/
    ├── Types.h
    └── ShellUtils.h
```

### 5.2 链接传递性

`factory_common` 与 `factory_config` 对 `factory_core` 的依赖需通过 `PUBLIC`/`INTERFACE` 传递，确保 `factory_test` 最终链接时不出现 undefined reference。

---

## 6. Phase 1 实现待办清单

| 编号 | 内容 | 阶段 | 优先级 |
|---|---|---|---|
| A | `file(GLOB ...)` 改显式列源文件 | 骨架搭建 | 高 |
| B | `set(FW_ROOT ...)` 改 `if(NOT DEFINED FW_ROOT)` | 骨架搭建 | 高 |
| C | `build.sh` 加 toolchain 存在性检查 + `SCRIPT_DIR` 锚定 | 骨架搭建 | 高 |
| D | `IWifiManager.h` 从 `ble/` 移到 `platforms/common/interface/` | 骨架搭建 | 高 |
| E | 确认 `factory_common`/`factory_config` 链接传递性（PUBLIC/INTERFACE） | 骨架搭建 | 中 |
| F | `testCfg["module"]` 改 `.at("module").get<std::string>()` | 测试框架 | 中 |
| G | 确认 mosquitto 构建/循环模式的线程安全（pthread / loop_start） | 测试框架 | 中 |
| H | `TestResult` 统一走工厂方法（`pass()`/`fail()`/`skipped()`），不留裸聚合初始化 | 测试框架 | 低 |
