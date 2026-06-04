# 产测固件平台化开发记录

> 记录设计评审决策、实现过程陷阱、待办状态。
> 与设计文档（specs/）和实现计划（plans/）互补——spec 讲"应该是什么样"，plan 讲"怎么一步步做"，devlog 讲"实际发生了什么"。

---

## 1. 设计迭代详情

### 1.1 v1.0 → v1.1：独立性重构

**触发原因**：用户提出核心不变式——"factory_fw 是可整体搬走的项目根"。

**具体变更**：
| 变更项 | v1.0 | v1.1 | 文件位置 |
|--------|------|------|----------|
| build.sh 位置 | `FACTORY_GIT/build.sh` | `factory_fw/build.sh` | 根目录 → `factory_fw/` |
| cmake/ 位置 | `FACTORY_GIT/cmake/` | `factory_fw/cmake/` | 根目录 → `factory_fw/` |
| build/ 产物 | `FACTORY_GIT/build/` | `factory_fw/build/` | 根目录 → `factory_fw/` |
| main.cpp 位置 | `factory_fw/main.cpp` | `factory_fw/src/main.cpp` | 根目录 → `src/` |
| 旧代码关系 | "与现有 src/include 并行" | "仅迁移期只读参考，零构建依赖" | 设计文档措辞 |
| FW_ROOT 传入 | `build.sh` 传 `-DFW_ROOT` | 平台 CMake 用 `if(NOT DEFINED FW_ROOT)` 回退 | `build.sh` + `platforms/*/CMakeLists.txt` |
| build.sh 锚定 | 相对路径，依赖 CWD | `SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"` | `factory_fw/build.sh:5-6` |

**新增章节**：
- 第 7 节「依赖管理」：工具链从 `FALCON_SDK` 环境变量定位
- 第 8 节「独立性守卫」：CI 隔离构建测试

### 1.2 v1.1 → v1.2：双注册语义修正

**触发原因**：用户指出 v1.1 的 `IPlatform` 同时保留 `createXxx()` + `registerDrivers()` 与 `.docx` 权威来源不一致，且引入冗余。

**评审指出的具体问题**：
1. `IPlatform` 9 个 `createXxx()` 全是纯虚 (`= 0`)，但 `main.cpp` 实际只调了 `createDisplayDriver()` 一个，剩下 7 个是纯死接口
2. `createCameraDriver()` 返 `Gc4663`，`registerDrivers()` 又 `bind<ICameraDriver>` 到 `Gc4663`——换 sensor 要改两处，无机制保证一致

**决策过程**：
- 选项 (a)：纯 registry，`main` 用 `reg.create<IDisplayDriver>()` 取一次自持
- 选项 (b)：只给 display/recorder 两个留 `createXxx()`，其余 7 个删掉
- **最终选择 (a)**：全部走 registry，单一真相源

**代码变更**：
```cpp
// v1.1 IPlatform.h（已废弃）
class IPlatform {
    virtual std::unique_ptr<IEncoder>       createEncoder()       = 0;
    virtual std::unique_ptr<ICameraDriver>  createCameraDriver()  = 0;
    // ... 共 9 个 createXxx()
    virtual void registerDrivers(DriverRegistry& reg) = 0;
};

// v1.2 IPlatform.h（当前）
class IPlatform {
    virtual void registerDrivers(DriverRegistry& reg) = 0;
    // 仅此一个抽象方法
};
```

**main.cpp 对应变更**：
```cpp
// v1.1
auto display = platform->createDisplayDriver();

// v1.2
auto display = reg.create<IDisplayDriver>();
```

### 1.3 v1.2 → v1.3-final：技术修复

**触发原因**：代码审查发现编译期和运行期硬伤。

**修复 1：TestContext::params_ 悬空引用**
- **问题**：`TestContext` 构造函数参数 `params` 是 `const json&`，但调用方传 `testCfg.value("params", json::object())`（临时对象）
- **C++ 规则**：临时对象绑定到构造函数引用参数不延长生命周期，构造函数返回即悬空
- **修复前**：`const nlohmann::json& params_;`
- **修复后**：`nlohmann::json params_;`（值持有）
- **落点**：`include/core/TestContext.h:20`

**修复 2：async lambda unique_ptr 不可拷贝**
- **问题**：`asyncQueue_.enqueue([=]() mutable { auto result = mod->run(ctx); ... });`
- **编译错误**：`mod` 是 `std::unique_ptr<ITestModule>`，`[=]` 要拷贝它——`unique_ptr` 不可拷贝
- **修复前**：`[=]() mutable { ... }`
- **修复后**：`[mod = std::move(mod), ctx = std::move(ctx), topic, this]() mutable { ... }`
- **落点**：`include/core/TestEngine.h` dispatch 声明

**修复 3：静态库 dead-strip 导致测试静默失踪**
- **问题**：`factory_tests` 是 STATIC library，`REGISTER_TEST_MODULE` 生成的 `_reg_Xxx` bool 变量无外部引用
- **链接器行为**：`--gc-sections` 或默认 dead-strip 把整个 `.o` 丢弃
- **现象**：模块在单测里注册得好好的，进真固件就消失，且无任何报错
- **修复**：`add_library(factory_tests OBJECT ...)`（OBJECT library，对象文件全量进 exe）
- **落点**：`src/tests/CMakeLists.txt:16`

**修复 4：enqueueAndWait 竞态窗口**
- **问题**：`std::packaged_task` 按引用捕获进 lambda，`fut.get()` 醒来的瞬间 worker 可能还在 `operator()` 收尾
- **UB 场景**：调用线程销毁 `pt` 的同时 worker 线程访问 `*this`
- **修复前**：`[&pt]() mutable { pt(); }`
- **修复后**：`auto pt = std::make_shared<std::packaged_task<R()>>; ... enqueue([pt]() { (*pt)(); });`
- **落点**：`include/core/AsyncTaskQueue.h:14-18`

**修复 5：tests.json 职责归 TestEngine**
- **问题**：v1.2 中 `ModuleRegistry::loadFromJson(engine, "tests.json")` 把调度配置和模块工厂搅在一起
- **修复**：`ModuleRegistry` 只负责模块工厂；`TestEngine::loadTestConfig("tests.json")` 负责调度配置
- **新增**：启动期校验（`loadTestConfig` 中检查每条 `module` 是否在 `ModuleRegistry` 中存在，fail-fast）
- **落点**：`src/core/TestEngine.cpp:16-37`

**修复 6：异常守卫补 `catch(...)`**
- **问题**：只 `catch (const std::exception&)`，第三方库抛非 std::exception（如 `throw int`）会逃出 worker lambda → `std::terminate`
- **修复**：`catch (...) { return TestResult::fail("unknown exception"); }`
- **落点**：`src/core/TestEngine.cpp:51-53`

**修复 7：SysfsGpioDriver 重复定义**
- **问题**：v1.2 目录树中 `src/platforms/common/SysfsGpioDriver.cpp` 和 `platforms/falcon/drivers/SysfsGpioDriver.cpp` 两份同名类
- **后果**：ODR 违规，链接时报重复符号
- **修复**：删 `platforms/falcon/drivers/` 中的那份，只留 `src/platforms/common/` 一份
- **落点**：目录结构 + `platforms/falcon/CMakeLists.txt` 源文件列表

---

## 2. Phase 1 实现详情

### 2.1 提交历史

```
d8578a8 doc(v1.3-final): 产测固件平台化设计文档 — 冻结基线
d423331 doc(v1.3): 产测固件平台化设计文档
fcca28d doc(v1.2): 产测固件平台化设计文档
ec1e833 doc(v1.1): 产测固件平台化设计文档
d8578a8 doc(v1.0): 产测固件平台化设计文档 (Section 1-3)
f389216 feat: Phase 1 skeleton — all interfaces, registries, stubs, build system
1e633b0 fix: compilation fixes for null platform
8e3c07a doc: add development log (devlog.md)
```

### 2.2 null 平台编译验证实录

**环境**：WSL2, Ubuntu 22.04, GCC 11.4.0, CMake 3.22

**验证命令**：
```bash
cd /home/gdh/FACTORY_GIT/factory_fw
PLATFORM=null ./build.sh
```

**最终输出**：
```
[100%] Linking CXX executable factory_test
[100%] Built target factory_test
[build_factory] Null platform — nothing to package
```

**修复轮次**：共 4 轮编译才通过

#### Round 1：nlohmann/json 缺失
```
fatal error: nlohmann/json.hpp: No such file or directory
    2 | #include <nlohmann/json.hpp>
```
**修复**：`curl -o third_party/nlohmann/json.hpp https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp`

#### Round 2：include 路径错误
文件在 `third_party/nlohmann/json.hpp`，CMake 路径是 `third_party/nlohmann`，编译器查找 `third_party/nlohmann/nlohmann/json.hpp`——不匹配。
**修复**：4 个 CMakeLists.txt 中 `third_party/nlohmann` → `third_party`

#### Round 3：ModuleRegistry 类型不匹配 + ITestModule 不完整
```
error: cannot convert 'function<unique_ptr<ITestModule>()>' to 'function<void*()>'
error: invalid application of 'sizeof' to incomplete type 'ft::ITestModule'
error: expected constructor, destructor, or type conversion before '(' token
   REGISTER_TEST_MODULE("camera", CameraTest);
```
**修复**：
1. `ModuleRegistry::add` 包 lambda 转换
2. `ModuleRegistry.cpp` `#include "tests/ITestModule.h"`
3. `ITestModule.h` `#include "core/ModuleRegistry.h"`

#### Round 4：main.cpp 命名空间 + 类型不匹配
```
error: 'PlatformConfig' has not been declared
error: cannot convert 'std::string' to 'const char*'
```
**修复**：`using namespace ft;` + `cfg.platformName().c_str()`

### 2.3 编译产物

```
factory_fw/build/null/
├── CMakeFiles/
├── fw_core/libfactory_core.a
├── fw_common/libfactory_common.a
├── fw_config/libfactory_config.a
├── fw_hal/libfactory_hal.a
├── fw_platform_common/libfactory_platform_common.a
├── factory_test          # ← 可执行文件
└── ...
```

---

## 3. 待办清单状态

### 3.1 已落实（Phase 1 完成）

| 编号 | 内容 | 落点 | 验证方式 |
|------|------|------|----------|
| A | 显式列源文件 | `platforms/falcon/CMakeLists.txt:10-19` | 阅读 CMakeLists |
| B | `if(NOT DEFINED FW_ROOT)` | `platforms/{falcon,null}/CMakeLists.txt:5-7` | 阅读 CMakeLists |
| C | `SCRIPT_DIR` 锚定 + toolchain 检查 | `build.sh:5-6, 14-15` | 运行 `build.sh` |
| D | `IWifiManager.h` 位置 | `include/platforms/common/interface/IWifiManager.h` | 阅读文件 |
| F | `.at("module").get<string>()` | `src/core/TestEngine.cpp:23` | 阅读代码 |
| H | `TestResult` 工厂方法 | `include/core/TestResult.h:13-21` | 阅读代码 |
| I | `FALCON_SDK` 环境变量 | `cmake/platforms/falcon.cmake:1-4` | 阅读代码 |
| L | `loadTestConfig` 启动期校验 | `src/core/TestEngine.cpp:23-29` | 阅读代码 |

### 3.2 待 Phase 2 实现

| 编号 | 内容 | 优先级 | 阻塞条件 |
|------|------|--------|----------|
| E | 确认 `factory_common`/`factory_config` 链接传递性 | 中 | 需 falcon 平台实际链接验证 |
| G | mosquitto 线程安全确认 | 中 | 需引入 mosquitto 库后才能验证 |
| J | 第三方依赖获取方式 | 高 | 需决定 nlohmann/json 是继续 curl 单头还是改用 find_package |
| K | CI 隔离构建 | 中 | 需 Jenkins/GitHub Actions 环境 |

### 3.3 代码中 TODO 汇总

| 文件 | 行号 | TODO | 影响 |
|------|------|------|------|
| `src/core/TestEngine.cpp:42` | `run()` | MQTT loop 集成 | 固件无法收消息 |
| `src/core/TestEngine.cpp:46` | `onMqttMessage()` | topic→testCfg 查找 | MQTT 消息无法路由 |
| `src/core/TestEngine.cpp:50` | `dispatch()` | 完整 sync/async 派发 | 测试项无法执行 |
| `src/core/TestEngine.cpp:55` | `publishResult()` | `mosquitto_publish` + 锁 | 结果无法上报 |
| 12 个 `src/tests/*Test.cpp` | `run()` | 全部 `skipped("not implemented")` | 无实际测试功能 |
| `src/ble/*.cpp` | 全部 | 空桩 | BLE 未实现 |
| `platforms/falcon/drivers/*.cpp` | 全部 | 空桩 | RV1126B 驱动未实现 |
| `platforms/falcon/build_factory.sh` | 全部 | 仅 `echo` | 不生成固件镜像 |

---

## 4. 关键陷阱详解

### 4.1 静态库 + 自注册 = 静默消失

**完整复现步骤**：
```cmake
# 错误的写法
add_library(factory_tests STATIC BatteryTest.cpp CameraTest.cpp ...)
target_link_libraries(factory_test factory_tests)
```

**编译成功，运行失败**：
```cpp
// main.cpp
ModuleRegistry::instance().has("camera");  // 返回 false！
```

**反汇编验证**：
```bash
nm factory_test | grep _reg_CameraTest
# 无输出——符号被链接器丢弃了
```

**正确写法**：
```cmake
add_library(factory_tests OBJECT BatteryTest.cpp CameraTest.cpp ...)
# OBJECT library 的对象文件无条件进入最终链接
```

### 4.2 构造函数参数临时对象绑定引用成员

**问题代码**：
```cpp
class TestContext {
public:
    TestContext(const DriverRegistry& d, const PlatformConfig& c, const json& p)
        : drivers_(d), config_(c), params_(p) {}  // p 是引用
private:
    const json& params_;  // ← 悬空炸弹
};

// 调用方
TestContext ctx(drivers_, config_, testCfg.value("params", json::object()));
// value() 返回临时对象，ctx 构造完即悬空
```

**修复代码**：
```cpp
class TestContext {
public:
    TestContext(const DriverRegistry& d, const PlatformConfig& c, json p)
        : drivers_(d), config_(c), params_(std::move(p)) {}  // 按值传入，move 持有
private:
    json params_;  // ← 值持有，安全
};
```

### 4.3 packaged_task 按引用捕获

**问题代码**：
```cpp
template<class F>
auto enqueueAndWait(F&& task) -> decltype(task()) {
    std::packaged_task<ResultType()> pt(std::forward<F>(task));
    std::future<ResultType> fut = pt.get_future();
    enqueue([&pt]() mutable { pt(); });  // ← 引用捕获栈上局部
    return fut.get();  // worker 可能在 pt 析构后才写完 operator() 收尾
}
```

**修复代码**：
```cpp
template<class F>
auto enqueueAndWait(F&& task) -> decltype(task()) {
    using R = decltype(task());
    auto pt = std::make_shared<std::packaged_task<R()>>(std::forward<F>(task));
    std::future<R> fut = pt->get_future();
    enqueue([pt]() { (*pt)(); });  // ← shared_ptr 拷贝，引用计数+1
    return fut.get();  // worker 持有 pt，安全
}
```

---

## 5. 文件索引

### 5.1 设计冻结文档
- `docs/superpowers/specs/2026-06-03-factory-firmware-platform-design.md` — v1.3-final

### 5.2 实现计划
- `docs/superpowers/plans/2026-06-03-factory-firmware-platform.md` — Phase 1 全部 14 个 task

### 5.3 核心接口（冻结点）
- `factory_fw/include/core/FactoryStore.h` — 薄基座
- `factory_fw/include/core/DriverRegistry.h` — 硬件能力注册
- `factory_fw/include/core/ModuleRegistry.h` — 测试方法注册
- `factory_fw/include/core/TestResult.h` — 三态结果 + data 字段
- `factory_fw/include/core/TestContext.h` — 值持有 params
- `factory_fw/include/core/AsyncTaskQueue.h` — 单 worker + shared_ptr 竞态修复
- `factory_fw/include/tests/ITestModule.h` — 模块接口 + REGISTER_TEST_MODULE 宏

### 5.4 平台接口（冻结点）
- `factory_fw/include/platforms/common/interface/IPlatform.h` — registerDrivers() 单一面
- `factory_fw/include/platforms/common/interface/I*.h` — 9 个 HAL 接口

### 5.5 构建入口
- `factory_fw/build.sh` — SCRIPT_DIR 锚定
- `factory_fw/CMakeLists.txt` — 聚合
- `factory_fw/cmake/platforms/null.cmake` — x86 本地编译
- `factory_fw/cmake/platforms/falcon.cmake` — FALCON_SDK 环境变量
- `factory_fw/platforms/falcon/CMakeLists.txt` — if(NOT TARGET) 守卫
- `factory_fw/platforms/null/CMakeLists.txt` — 独立/聚合两用

---

## 6. 后续工作入口

**Phase 2 启动条件**：`TestEngine::dispatch` + MQTT 集成完成后，能跑通一个端到端测试（如 `15R battery`）。

**建议顺序**：
1. `TestEngine::dispatch` + `onMqttMessage` + `publishResult`（核心调度）
2. 迁移现有 `src/tests/BatteryTest.cpp` 逻辑到新 `factory_fw/src/tests/BatteryTest.cpp`
3. 迁移 `src/hal/MppEncoder.cpp` → `platforms/falcon/drivers/RkMppEncoder.cpp`
4. 逐个迁移其余平台 driver
5. BLE 模块迁移
