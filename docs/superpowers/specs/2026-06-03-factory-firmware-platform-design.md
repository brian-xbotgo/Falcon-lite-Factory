# 产测固件平台化设计文档

> 版本: v1.3
> 日期: 2026-06-03
> 范围: 从 RV1126B 专用固件演进为跨平台产测框架
> 权威来源: `产测固件架构.docx`（根目录）
> 核心不变式: `factory_fw/` 是可整体搬走的独立项目根

---

## 目录

1. [执行摘要](#1-执行摘要)
2. [独立项目根不变式](#2-独立项目根不变式)
3. [目录结构与编译入口](#3-目录结构与编译入口)
4. [核心注册机制](#4-核心注册机制)
5. [测试框架](#5-测试框架)
6. [构建系统](#6-构建系统)
7. [依赖管理](#7-依赖管理)
8. [独立性守卫](#8-独立性守卫)
9. [Phase 1 实现待办清单](#9-phase-1-实现待办清单)

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
| **独立根** | `factory_fw/` 可整体拷走独立编译，零外部依赖 |

---

## 2. 独立项目根不变式

**判据（可执行）**：
> 把 `factory_fw/` 整个目录拷到任意路径，`cd` 进去 `./build.sh` 必须能编出固件。

**等价约束**：
- `factory_fw` 内部任何文件都不许引用 `factory_fw` 之外的路径
- 无逃出根的 `../`
- 不 `include` `FACTORY_GIT/src/` 或 `FACTORY_GIT/include/`
- 不 `add_subdirectory(../src)`
- 不读 `FACTORY_GIT/cmake/`
- 旧 `FACTORY_GIT/src`、`FACTORY_GIT/include` 只有一个身份：**迁移期只读参考，零构建依赖**
- 迁移 = 照着旧代码在 `factory_fw` 内重写进新接口，永远不存在从 `factory_fw` 指向旧代码的构建边

---

## 3. 目录结构与编译入口

### 3.1 顶层目录

```
factory_fw/                        ← 未来独立仓库根（可整体搬走）
├── build.sh                       # 以 SCRIPT_DIR 为根
├── CMakeLists.txt                 # 聚合 CMake（IDE 用）
├── cmake/
│   └── platforms/
│       ├── falcon.cmake           # RV1126B 交叉编译工具链
│       └── null.cmake             # x86 本地编译工具链
│
├── include/                       # 公共头，按模块
│   ├── core/
│   │   ├── TestEngine.h
│   │   ├── AsyncTaskQueue.h
│   │   ├── TestResult.h
│   │   ├── TestContext.h
│   │   ├── FactoryStore.h
│   │   ├── DriverRegistry.h
│   │   └── ModuleRegistry.h
│   ├── tests/
│   │   └── ITestModule.h
│   ├── hal/
│   │   ├── RecorderController.h
│   │   ├── I2cController.h
│   │   └── V4l2Recorder.h
│   ├── config/
│   │   └── PlatformConfig.h
│   ├── common/
│   │   ├── Types.h
│   │   └── ShellUtils.h
│   └── platforms/common/interface/
│       ├── IPlatform.h
│       ├── IEncoder.h
│       ├── ICameraDriver.h
│       ├── IBatteryDriver.h
│       ├── IMotorDriver.h
│       ├── IHallDriver.h
│       ├── IDisplayDriver.h
│       ├── IGpioDriver.h
│       ├── IRecorder.h
│       └── IWifiManager.h
│
├── src/                           # 实现，按模块
│   ├── main.cpp
│   ├── core/
│   │   ├── TestEngine.cpp
│   │   ├── AsyncTaskQueue.cpp
│   │   ├── TestResult.cpp
│   │   ├── TestContext.cpp
│   │   ├── FactoryStore.cpp
│   │   ├── DriverRegistry.cpp
│   │   └── ModuleRegistry.cpp
│   ├── config/
│   │   └── PlatformConfig.cpp
│   ├── hal/
│   │   ├── RecorderController.cpp
│   │   ├── I2cController.cpp
│   │   └── V4l2Recorder.cpp
│   ├── tests/
│   │   ├── TestModuleRegistry.cpp
│   │   ├── BatteryTest.cpp
│   │   ├── CameraTest.cpp
│   │   ├── WifiTest.cpp
│   │   ├── RtcTest.cpp
│   │   ├── KeyTest.cpp
│   │   ├── MicTest.cpp
│   │   ├── SocTest.cpp
│   │   ├── TfCardTest.cpp
│   │   ├── MotorTest.cpp
│   │   ├── HallTest.cpp
│   │   ├── AgingTest.cpp
│   │   └── SysTest.cpp
│   ├── ble/
│   │   ├── BleAdvertiser.cpp
│   │   ├── BleGattServer.cpp
│   │   ├── BleMqttBridge.cpp
│   │   ├── BleDeviceInfo.cpp
│   │   └── BleUtility.cpp
│   └── platforms/common/
│       ├── NullPlatform.cpp
│       ├── NullEncoder.cpp
│       ├── NullCameraDriver.cpp
│       ├── NullBatteryDriver.cpp
│       ├── NullMotorDriver.cpp
│       ├── NullHallDriver.cpp
│       ├── NullDisplayDriver.cpp
│       ├── SysfsGpioDriver.cpp
│       ├── NullRecorder.cpp
│       └── NullWifiManager.cpp
│
├── platforms/
│   └── falcon/                    # Falcon (RV1126B)
│       ├── CMakeLists.txt         # 自包含 CMake（独立/聚合两用）
│       ├── build_factory.sh       # 平台打包脚本
│       ├── platform.json          # ★ 硬件资源配置（平台层自维护）
│       ├── tests.json             # ★ 测试项注册表（平台层自维护）
│       └── drivers/               # 平台 driver 实现
│           ├── RkMppEncoder.cpp
│           ├── Gc4663CameraDriver.cpp
│           ├── Cw221xBatteryDriver.cpp
│           ├── Tmi8152MotorDriver.cpp
│           ├── HallSwitchDriver.cpp
│           ├── LvglDisplayDriver.cpp
│           ├── Rv1126bRecorder.cpp
│           └── Rv1126bWifiManager.cpp
│
├── third_party/                   # 第三方依赖（见第 7 节）
│   ├── cJSON/
│   ├── lvgl/
│   ├── mosquitto/
│   └── nlohmann/
│
└── build/                         # 构建产物（.gitignore）
    └── falcon/
```

### 3.2 关键约定

- `factory_fw` 自包含；`FACTORY_GIT/src`、`FACTORY_GIT/include` 仅迁移期只读参考，零构建依赖
- `build.sh` 以 `SCRIPT_DIR` 为根，不依赖调用者 CWD
- `main.cpp` 位于 `src/`，唯一一份通用入口
- 平台 driver 目录命名 `drivers/`（与通用 `hal/` 区分）
- **配置文件放在平台层**：`platforms/<platform>/platform.json` + `platforms/<platform>/tests.json`，各平台自维护
- 构建产物落在 `build/${PLATFORM}/`，不污染源码树

### 3.3 build.sh

```bash
#!/bin/bash
set -euo pipefail

# factory_fw 即项目根：以脚本自身位置为锚
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

PLATFORM="${PLATFORM:-falcon}"
PLATFORM_LC=$(echo "$PLATFORM" | tr '[:upper:]' '[:lower:]')
PLATFORM_DIR="$SCRIPT_DIR/platforms/${PLATFORM_LC}"
BUILD_DIR="$SCRIPT_DIR/build/${PLATFORM_LC}"
TOOLCHAIN_FILE="$SCRIPT_DIR/cmake/platforms/${PLATFORM_LC}.cmake"

[[ -d "$PLATFORM_DIR" ]]    || { echo "Error: platform '$PLATFORM' not found" >&2; exit 1; }
[[ -f "$TOOLCHAIN_FILE" ]]  || { echo "Error: toolchain not found: $TOOLCHAIN_FILE" >&2; exit 1; }

cmake -S "$PLATFORM_DIR" -B "$BUILD_DIR" \
      -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
      -DFW_ROOT="$SCRIPT_DIR"

cmake --build "$BUILD_DIR" --parallel
"$PLATFORM_DIR/build_factory.sh" "$BUILD_DIR"
```

### 3.4 聚合 CMake（CMakeLists.txt）

```cmake
cmake_minimum_required(VERSION 3.16)
project(FactoryFirmware LANGUAGES C CXX)

# 通用框架（只 add 一次）
add_subdirectory(src/core)
add_subdirectory(src/common)
add_subdirectory(src/config)
add_subdirectory(src/hal)
add_subdirectory(src/tests)
add_subdirectory(src/platforms/common)

# 平台选择
set(TARGET_PLATFORM "falcon" CACHE STRING "Target platform")
add_subdirectory("platforms/${TARGET_PLATFORM}")
```

### 3.5 平台自包含 CMake（platforms/falcon/CMakeLists.txt）

```cmake
cmake_minimum_required(VERSION 3.16)
project(FalconFactory LANGUAGES C CXX)

if(NOT DEFINED FW_ROOT)
    set(FW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")   # platforms/falcon → factory_fw 根
endif()

# 独立编译模式：如果上层没 add，就自己 add
if(NOT TARGET factory_core)
    add_subdirectory(${FW_ROOT}/src/core          ${CMAKE_BINARY_DIR}/fw_core)
    add_subdirectory(${FW_ROOT}/src/common        ${CMAKE_BINARY_DIR}/fw_common)
    add_subdirectory(${FW_ROOT}/src/config        ${CMAKE_BINARY_DIR}/fw_config)
    add_subdirectory(${FW_ROOT}/src/hal           ${CMAKE_BINARY_DIR}/fw_hal)
    add_subdirectory(${FW_ROOT}/src/tests         ${CMAKE_BINARY_DIR}/fw_tests)
    add_subdirectory(${FW_ROOT}/src/platforms/common ${CMAKE_BINARY_DIR}/fw_platform_common)
endif()

# 平台 driver（显式列源文件，不用 GLOB）
set(FALCON_DRIVER_SRCS
    drivers/RkMppEncoder.cpp
    drivers/Gc4663CameraDriver.cpp
    drivers/Cw221xBatteryDriver.cpp
    drivers/Tmi8152MotorDriver.cpp
    drivers/HallSwitchDriver.cpp
    drivers/LvglDisplayDriver.cpp
    drivers/Rv1126bRecorder.cpp
    drivers/Rv1126bWifiManager.cpp
)
add_library(platform_falcon STATIC ${FALCON_DRIVER_SRCS})

# 主可执行文件
add_executable(factory_test ${FW_ROOT}/src/main.cpp)
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

### 3.6 main.cpp（唯一一份通用入口）

```cpp
#include "config/PlatformConfig.h"
#include "core/DriverRegistry.h"
#include "core/TestEngine.h"
#include "platforms/common/interface/IPlatform.h"
#include "platforms/common/interface/IDisplayDriver.h"

int main() {
    auto& cfg = PlatformConfig::instance();
    if (!cfg.loadFromFile("/oem/usr/conf/platform.json")) return -1;

    auto platform = ft::createPlatform(cfg.platformName());
    if (!platform || !platform->init(cfg.raw())) return -1;

    // 驱动注册表：平台登记自己拥有的能力
    ft::DriverRegistry reg;
    platform->registerDrivers(reg);

    // main 直接取 display（长生命周期组件，main 持有）
    auto display = reg.create<IDisplayDriver>();
    if (display) display->init();

    ft::TestEngine engine(reg, cfg);
    engine.loadTestConfig("/oem/usr/conf/tests.json");

    engine.run();
    return 0;
}
```

---

## 4. 核心注册机制

### 4.1 设计核心：双注册

系统中存在 **两个正交注册表**，索引两个不同维度：

| 注册表 | 维度 | 键类型 | 用途 |
|--------|------|--------|------|
| `DriverRegistry` | 硬件能力 | `std::type_index` | 平台登记自己拥有的 driver，测试模块通过 `TestContext::create<I>()` 获取 |
| `ModuleRegistry` | 测试方法 | `std::string`（module 名） | 测试模块通过 `REGISTER_TEST_MODULE` 宏自注册，`TestEngine` 按 topic 派发 |

两者共享薄基座 `FactoryStore`，但语义外壳独立，不强行合并。

### 4.2 FactoryStore

```cpp
// include/core/FactoryStore.h
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

### 4.3 DriverRegistry

```cpp
// include/core/DriverRegistry.h
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

### 4.4 IPlatform

```cpp
// include/platforms/common/interface/IPlatform.h
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

### 4.5 平台侧登记示例

```cpp
void Rv1126bPlatform::registerDrivers(DriverRegistry& reg) {
    reg.bind<IEncoder>      ([] { return std::make_unique<RkMppEncoder>(); });
    reg.bind<ICameraDriver> ([] { return std::make_unique<Gc4663CameraDriver>(); });
    reg.bind<IBatteryDriver>([] { return std::make_unique<Cw221xBatteryDriver>(); });
    reg.bind<IGpioDriver>   ([] { return std::make_unique<SysfsGpioDriver>(); });
    reg.bind<IDisplayDriver>([] { return std::make_unique<LvglDisplayDriver>(); });
    // 平台没有电机 → 不 bind<IMotorDriver>()，上层 create<IMotorDriver>() 返回 nullptr
}
```

### 4.6 ModuleRegistry

```cpp
// include/core/ModuleRegistry.h
#pragma once
#include "FactoryStore.h"
#include <string>
#include <memory>

namespace ft {

class ITestModule;

class ModuleRegistry {
public:
    static ModuleRegistry& instance();

    void add(const std::string& name,
             std::function<std::unique_ptr<ITestModule>()> factory);
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

---

## 5. 测试框架

### 5.1 三个核心抽象

```cpp
// include/tests/ITestModule.h
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
// include/core/TestResult.h
#pragma once
#include <string>
#include <nlohmann/json.hpp>

namespace ft {

struct TestResult {
    enum class Status { Pass, Fail, Skipped };
    Status status;
    std::string detail;
    nlohmann::json data;

    static TestResult pass() {
        return {Status::Pass, "", nlohmann::json::object()};
    }
    static TestResult fail(std::string msg) {
        return {Status::Fail, std::move(msg), nlohmann::json::object()};
    }
    static TestResult skipped(std::string msg) {
        return {Status::Skipped, std::move(msg), nlohmann::json::object()};
    }

    bool isPass()     const { return status == Status::Pass; }
    bool isFail()     const { return status == Status::Fail; }
    bool isSkipped()  const { return status == Status::Skipped; }
};

} // namespace ft
```

```cpp
// include/core/TestContext.h
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
    nlohmann::json params_;
};

} // namespace ft
```

### 5.2 测试模块写法

```cpp
// src/tests/CameraTest.cpp
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

REGISTER_TEST_MODULE("camera", CameraTest);

} // namespace ft
```

### 5.3 AsyncTaskQueue 接口

```cpp
// include/core/AsyncTaskQueue.h
#pragma once
#include <functional>
#include <future>

namespace ft {

class AsyncTaskQueue {
public:
    void enqueue(std::function<void()> task);   // async：发了就走

    template<class F>
    auto enqueueAndWait(F&& task) -> decltype(task()) {
        using ResultType = decltype(task());
        std::packaged_task<ResultType()> pt(std::forward<F>(task));
        std::future<ResultType> fut = pt.get_future();
        enqueue([&pt]() mutable { pt(); });
        return fut.get();   // 阻塞等待结果
    }

private:
    // 内部：单 worker 线程 + std::queue + condition_variable
};

} // namespace ft
```

### 5.4 TestEngine 调度、并发模型与异常契约

**并发语义（冻结点）**：
> `AsyncTaskQueue` 为**单 worker 线程**。所有测试（无论 sync/async）均入队由该 worker **串行执行**。sync 测试 `enqueueAndWait` 阻塞等待结果；async 测试 `enqueue` 后立即返回。此设计确保：① 回调线程不被长测试阻塞，MQTT 消息持续接收；② 所有硬件访问（I2C、V4L2、GPIO、Motor SPI）由架构保证串行，无需外部协议假设。

**异常契约（冻结点）**：
> `dispatch` 对 `testCfg` 的访问和 `mod->run()` 的执行均受异常守卫保护。任何异常（包括非 `std::exception` 派生类型）都被捕获为 `TestResult::fail`，worker 线程永不崩溃。

```cpp
void TestEngine::dispatch(const std::string& topic,
                          const nlohmann::json& testCfg) {
    std::unique_ptr<ITestModule> mod;
    try {
        mod = ModuleRegistry::instance().create(
            testCfg.at("module").get<std::string>());
    } catch (const std::exception& e) {
        publishResult(topic,
            TestResult::fail(std::string("bad config: ") + e.what()));
        return;
    }

    if (!mod) {
        publishResult(topic, TestResult::fail("unknown module"));
        return;
    }

    TestContext ctx(drivers_, config_,
                    testCfg.value("params", nlohmann::json::object()));

    auto task = [mod = std::move(mod), ctx = std::move(ctx),
                 topic, this]() mutable -> TestResult {
        try {
            return mod->run(ctx);
        } catch (const std::exception& e) {
            return TestResult::fail(std::string("exception: ") + e.what());
        } catch (...) {
            return TestResult::fail("unknown exception");
        }
    };

    if (testCfg.value("async", false)) {
        asyncQueue_.enqueue([task = std::move(task), topic, this]() mutable {
            publishResult(topic, task());
        });
    } else {
        publishResult(topic, asyncQueue_.enqueueAndWait(std::move(task)));
    }
}
```

**线程安全**：`TestEngine` 内部持 `std::mutex mqttMutex_`。`publishResult` 内加锁后调用 `mosquitto_publish`，串行化 sync 分支（MQTT 回调线程）与 async 分支（worker 线程）的 publish 调用。

### 5.5 tests.json 驱动调度（平台层自维护）

```json
// platforms/falcon/tests.json
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
| 增加测试 | 新增 `src/tests/XxxTest.cpp` + `REGISTER_TEST_MODULE("xxx", XxxTest)` + `platforms/<platform>/tests.json` 加一行 | 零 |
| 改 topic 绑定 | 改 `tests.json` 里的 `topic` 字段 | 零 |
| 同一模块多 topic | `tests.json` 里多行指向同一 `module`，带不同 `params` | 零 |
| 修改测试 | 只改对应那一个 `.cpp` | 零 |
| 调参数 | 改 `tests.json` 的 `params` | 零 |
| 停用测试 | `enabled` 置 `false` | 零 |
| 调顺序 | 改 `order` | 零 |

### 5.6 启动期校验（fail-fast）

`TestEngine::loadTestConfig` 在启动期校验 `tests.json`：
- 每条必须有 `topic` + `module`
- `module` 必须在 `ModuleRegistry` 中存在（即已被某个 `.cpp` 自注册）
- 校验失败立即打印明确错误并退出，不在产线上等 topic 触发才暴露

```cpp
bool TestEngine::loadTestConfig(const std::string& path) {
    // 加载 JSON...
    for (auto& t : j["tests"]) {
        auto moduleName = t.at("module").get<std::string>();
        if (!ModuleRegistry::instance().has(moduleName)) {
            fprintf(stderr, "[TestEngine] tests.json 引用未注册模块: %s\n",
                    moduleName.c_str());
            return false;
        }
    }
    testConfigs_ = std::move(j);
    return true;
}
```

### 5.7 tests 库类型

`src/tests/` 编译为 **OBJECT library**（`add_library(factory_tests OBJECT ...)`），消费方将其对象文件全量链接，避免静态库 dead-strip 导致自注册符号被丢弃。

---

## 6. 构建系统

### 6.1 头文件包含约定

所有模块的头文件统一放在 `include/<module>/`，CMake 中各模块：
```cmake
target_include_directories(factory_core PUBLIC
    ${FW_ROOT}/include
)
```

### 6.2 链接传递性

`factory_common` 与 `factory_config` 对 `factory_core` 的依赖需通过 `PUBLIC`/`INTERFACE` 传递，确保 `factory_test` 最终链接时不出现 undefined reference。

---

## 7. 依赖管理

`factory_fw` 独立后，第三方依赖必须显式声明获取方式，不能依赖"旧工程或 SDK sysroot 里碰巧能 include 到"。

| 依赖 | 获取方式 | 说明 |
|------|----------|------|
| nlohmann/json | `find_package` 或 header-only 放 `third_party/` | JSON 解析 |
| mosquitto | `find_package` + sysroot 查找 | MQTT 客户端 |
| LVGL | `add_subdirectory(third_party/lvgl)` | 显示渲染 |
| rockchip_mpp | 平台 CMake 中 `find_package` | 仅 falcon 平台需要 |

工具链文件不得写死 SDK 绝对路径，改从环境变量定位：
```cmake
# cmake/platforms/falcon.cmake
if(NOT DEFINED ENV{FALCON_SDK})
    message(FATAL_ERROR "请设置 FALCON_SDK 指向 RV1126B SDK 根目录")
endif()
set(CMAKE_SYSROOT      "$ENV{FALCON_SDK}/sysroot")
set(CMAKE_C_COMPILER   "$ENV{FALCON_SDK}/bin/aarch64-buildroot-linux-gnu-gcc")
# ...
```

---

## 8. 独立性守卫

**CI 隔离构建测试**：
```bash
cp -r factory_fw /tmp/fw_iso
cd /tmp/fw_iso
PLATFORM=falcon ./build.sh
```
在与 `FACTORY_GIT` 完全脱钩的目录里能编过才算数。

---

## 9. Phase 1 实现待办清单

### 9.1 已落入文档（编码验证即可）

| 编号 | 内容 | 状态 |
|---|---|---|
| D | `IWifiManager.h` 在 `include/platforms/common/interface/` | ✅ 文档已落实 |
| F | `testCfg["module"]` 改 `.at("module").get<std::string>()` | ✅ 5.4 已落实 |
| G | mosquitto 线程安全确认 | ⏳ 编码期验证 |
| H | `TestResult` 统一走工厂方法 | ✅ 5.1 已落实 |

### 9.2 待编码实现

| 编号 | 内容 | 优先级 |
|---|---|---|
| A | 显式列源文件（不用 `file(GLOB)`） | 高 |
| B | `set(FW_ROOT ...)` 改 `if(NOT DEFINED FW_ROOT)` | 高 |
| C | `build.sh` 加 toolchain 存在性检查 + `SCRIPT_DIR` 锚定 | 高 |
| E | 确认 `factory_common`/`factory_config` 链接传递性（PUBLIC/INTERFACE） | 中 |
| I | 工具链文件从 `FALCON_SDK` 环境变量定位 | 高 |
| J | 第三方依赖获取方式明确 | 高 |
| K | CI 隔离构建测试 | 中 |
| L | 启动期 `tests.json` 校验（`loadTestConfig` 中校验 module 存在性） | 高 |
