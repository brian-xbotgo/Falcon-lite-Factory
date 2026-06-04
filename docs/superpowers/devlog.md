# 产测固件平台化开发记录

> 记录设计评审决策、实现过程陷阱、待办状态。
> 与设计文档（specs/）和实现计划（plans/）互补——spec 讲"应该是什么样"，plan 讲"怎么一步步做"，devlog 讲"实际发生了什么"。

---

## 1. Phase 1 实现纪要（已完成）

### 1.1 交付物
- 72 个新文件，`factory_fw/` 完整目录结构
- `TARGET_PLATFORM=null` 编译通过，`factory_test` 可执行文件生成
- Git commit: `1e633b0`

### 1.2 新增文件清单

```
factory_fw/
├── build.sh
├── CMakeLists.txt
├── cmake/platforms/{falcon,null}.cmake
├── include/          # 28 个头文件（core/ tests/ hal/ config/ common/ platforms/common/interface/）
├── src/              # 38 个源文件 + main.cpp
│   ├── core/         # ModuleRegistry, AsyncTaskQueue, TestEngine, TestResult, TestContext
│   ├── common/       # ShellUtils
│   ├── config/       # PlatformConfig
│   ├── hal/          # RecorderController, I2cController, V4l2Recorder
│   ├── tests/        # 12 个测试模块桩
│   ├── ble/          # 5 个桩（待 Phase 2 填充）
│   ├── platforms/common/  # Null* + SysfsGpioDriver + PlatformFactory
│   └── main.cpp
├── platforms/
│   ├── falcon/       # CMakeLists.txt, platform.json, tests.json, build_factory.sh
│   └── null/         # CMakeLists.txt, build_factory.sh
└── third_party/
    └── nlohmann/json.hpp
```

### 1.3 编译错误与修复记录

| # | 错误现象 | 根因 | 修复 |
|---|---------|------|------|
| 1 | `nlohmann/json.hpp: No such file` | 文件未下载；include 路径写错 | `curl` 下载单头文件；CMake 路径从 `third_party/nlohmann` 改 `third_party` |
| 2 | `ModuleRegistry::add` 类型不匹配 | `FactoryStore<string>` 的 `FactoryFunc` 是 `function<void*()>`，但传入 `function<unique_ptr<ITestModule>()>` | 包一层 lambda：`[f]()->void*{ return f().release(); }` |
| 3 | `invalid application of sizeof to incomplete type ITestModule` | `ModuleRegistry.cpp` 未 include `ITestModule.h`，`unique_ptr` 析构需要完整类型 | `#include "tests/ITestModule.h"` |
| 4 | `TestContext.h: invalid use of incomplete type DriverRegistry` | 模板方法 `create<I>()` 调用前向声明类型的成员 | 前向声明改 `#include "core/DriverRegistry.h"` |
| 5 | `REGISTER_TEST_MODULE("camera", CameraTest)` 宏未定义 | `ITestModule.h` 未 include `ModuleRegistry.h` | `#include "core/ModuleRegistry.h"` |
| 6 | `I2cController.cpp: expected '}' at end of input` | probe 函数写崩，namespace ft 未关闭 | 重写 probe 函数，补 `} // namespace ft` |
| 7 | `main.cpp: PlatformConfig has not been declared` | 未 `using namespace ft;` | 加 `using namespace ft;` |
| 8 | `cannot convert string to const char*` | `cfg.platformName()` 返回 `std::string`，`createPlatform` 参数是 `const char*` | `.c_str()` |
| 9 | `Error: platform 'null' not found` | `platforms/null/` 目录和 CMakeLists.txt 缺失 | 新建 `platforms/null/` + `CMakeLists.txt` + `build_factory.sh` |

---

## 2. 设计评审关键决策备忘

> 以下决策来自 v1.0 → v1.3-final 的多轮评审，**冻结后不可回退**。

### 2.1 独立项目根不变式
**判据**：把 `factory_fw/` 整个目录拷到任意路径，`cd` 进去 `./build.sh` 必须能编出固件。  
**推论**：`factory_fw` 内部任何文件都不许引用 `factory_fw` 之外的路径；旧 `FACTORY_GIT/src`、`FACTORY_GIT/include` 仅迁移期只读参考，零构建依赖。

### 2.2 IPlatform 只保留 `registerDrivers()`
**决策**：删掉 `createXxx()` 工厂方法，只留 `registerDrivers(DriverRegistry&)`。  
**原因**：避免两套并行工厂（`createXxx()` vs `registerDrivers()`）导致"换 sensor 要改两处"的漂移风险；main 直接通过 `reg.create<IDisplayDriver>()` 取一次自持。

### 2.3 双注册 = DriverRegistry + ModuleRegistry
**澄清**："双注册"指系统中有两个正交注册表——
- `DriverRegistry`（按接口类型 `type_index`，硬件能力）
- `ModuleRegistry`（按字符串名，测试方法）  
**不是** `createXxx() + registerDrivers()`。

### 2.4 配置文件放在平台层
**决策**：`platforms/<platform>/platform.json` + `platforms/<platform>/tests.json`，取消独立 `configs/` 目录。  
**原因**：各平台自维护，不共享。

### 2.5 单 worker 串行 + 全部入队
**决策**：`AsyncTaskQueue` 单 worker 线程；sync/async 测试均入队由 worker 串行执行。  
**语义**：async = "不阻塞 MQTT 回调线程"；sync = "阻塞回调线程直至完成"。  
**目的**：硬件访问（I2C/V4L2/GPIO/Motor SPI）由架构保证串行，无需外部 MES 协议假设。

### 2.6 enqueueAndWait 用 shared_ptr 自持
**决策**：`std::make_shared<std::packaged_task>` + `enqueue([pt]() { (*pt)(); })`。  
**原因**：避免按引用捕获栈上局部 `packaged_task` 导致的竞态窗口（worker 线程 `operator()` 收尾与调用线程 `fut.get()` 醒来之间 `pt` 可能被销毁）。

### 2.7 tests 库用 OBJECT library
**决策**：`add_library(factory_tests OBJECT ...)`，而非 STATIC。  
**原因**：静态库链接器会做 dead-strip，`REGISTER_TEST_MODULE` 生成的 `_reg_Xxx` 无外部引用 → 整个 `.o` 被静默丢弃 → 测试模块"在单测里注册得好好的，进真固件就消失"。OBJECT library 的对象文件全量进 exe，无此问题。

### 2.8 TestContext::params_ 按值持有
**决策**：`nlohmann::json params_;`（值），不是 `const nlohmann::json&`。  
**原因**：构造函数接收 `testCfg.value("params", json::object())` 返回临时对象，绑定到引用成员后构造函数返回即悬空，UB。

---

## 3. 待办清单状态

### 3.1 已落实（Phase 1 完成）

| 编号 | 内容 | 落点 |
|------|------|------|
| A | 显式列源文件（不用 `file(GLOB)`） | `platforms/falcon/CMakeLists.txt` |
| B | `set(FW_ROOT)` 改 `if(NOT DEFINED FW_ROOT)` | `platforms/{falcon,null}/CMakeLists.txt` |
| C | `build.sh` `SCRIPT_DIR` 锚定 + toolchain 存在性检查 | `build.sh` |
| D | `IWifiManager.h` 放 `platforms/common/interface/` | `include/platforms/common/interface/` |
| F | `testCfg.at("module").get<string>()` | `TestEngine::loadTestConfig` |
| H | `TestResult` 统一走工厂方法 | `include/core/TestResult.h` |
| I | 工具链从 `FALCON_SDK` 环境变量定位 | `cmake/platforms/falcon.cmake` |
| L | `loadTestConfig` 启动期校验 module 存在性 | `TestEngine::loadTestConfig` |

### 3.2 待 Phase 2 实现

| 编号 | 内容 | 优先级 |
|------|------|--------|
| E | 确认 `factory_common`/`factory_config` 链接传递性（PUBLIC/INTERFACE） | 中 |
| G | 确认 mosquitto 构建/循环模式线程安全 | 中 |
| J | 第三方依赖获取方式明确（find_package / FetchContent / git submodule） | 高 |
| K | CI 隔离构建测试（`cp -r factory_fw /tmp/fw_iso && cd /tmp/fw_iso && PLATFORM=falcon ./build.sh`） | 中 |

### 3.3 设计文档中标记 TODO、尚未编码

| 位置 | TODO | 影响 |
|------|------|------|
| `TestEngine::run()` | MQTT loop 集成 | 固件无法收 MQTT 消息 |
| `TestEngine::dispatch()` | 完整 sync/async 派发逻辑 | 测试项无法执行 |
| `TestEngine::publishResult()` | `mosquitto_publish` + `mqttMutex_` | 结果无法上报 |
| `TestEngine::onMqttMessage()` | topic → testCfg 查找 | MQTT 消息无法路由 |
| 12 个测试模块 | 全部返回 `skipped("not implemented")` | 无实际测试功能 |
| `ble/` 5 个文件 | 空桩 | BLE 未实现 |
| `platforms/falcon/drivers/` 8 个文件 | 空桩 | RV1126B 平台驱动未实现 |
| `build_factory.sh` | 仅 echo，无实际打包逻辑 | 不生成固件镜像 |

---

## 4. 关键陷阱备忘（不要再踩）

### 4.1 静态库 + 自注册 = 静默消失
**现象**：`REGISTER_TEST_MODULE` 在 `.cpp` 里看起来好好的，编译也过，运行时 `ModuleRegistry::has("camera")` 返回 false。  
**根因**：链接器 `--gc-sections` 或默认 dead-strip 把无外部引用的 `_reg_CameraTest` 整个 `.o` 丢了。  
**解法**：OBJECT library，或链接时加 `--whole-archive`。

### 4.2 构造函数参数临时对象绑定引用成员
**现象**：`TestContext ctx(drivers_, config_, testCfg.value("params", json::object()));` 后 `ctx.params()` 偶发崩溃。  
**根因**：`value()` 按值返回临时，`const json& params_` 只绑定到构造函数参数（不延长生命周期），构造函数返回即悬空。  
**解法**：值持有 `json params_;`。

### 4.3 `packaged_task` 按引用捕获进 lambda
**现象**：`enqueueAndWait` 在高压下偶发崩溃或返回垃圾值。  
**根因**：`[&pt]` 捕获栈上局部，`fut.get()` 醒来的瞬间 `pt` 可能已被销毁，worker 线程在 `operator()` 收尾访问已释放内存。  
**解法**：`std::shared_ptr<std::packaged_task>` 拷贝进 lambda，生命周期脱离栈帧。

### 4.4 `Null*` 实现放 `platforms/common/` 但平台 CMake 又编译一份同名
**现象**：ODR 违规，链接时报重复符号。  
**根因**：`src/platforms/common/NullGpioDriver.cpp` 和 `platforms/falcon/drivers/SysfsGpioDriver.cpp` 同时存在且都 bind 到 `IGpioDriver`。  
**解法**：通用实现只留一份在 `src/platforms/common/`，平台目录不放同名；平台要特化就改名（如 `Rv1126bGpioDriver`）。

---

## 5. 后续工作入口

**Phase 2 启动条件**：`TestEngine::dispatch` + MQTT 集成完成后，能跑通一个端到端测试（如 `15R battery`）。

**建议顺序**：
1. `TestEngine::dispatch` + `onMqttMessage` + `publishResult`（核心调度）
2. 迁移现有 `src/tests/BatteryTest.cpp` 逻辑到新 `factory_fw/src/tests/BatteryTest.cpp`
3. 迁移 `src/hal/MppEncoder.cpp` → `platforms/falcon/drivers/RkMppEncoder.cpp`
4. 逐个迁移其余平台 driver
5. BLE 模块迁移
