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

以下修复按「状态」分类——设计评审阶段发现的问题 vs 编码实现阶段暴露的问题：

#### 设计评审阶段发现并冻结（v1.2 之前）

| # | 修复项 | 状态 | 说明 |
|---|--------|------|------|
| 1 | TestContext::params_ 值持有 | 设计已定 / 代码已实现 / 已编译验证 | 评审发现构造函数临时对象绑定引用成员的 UB，`TestContext.h` 已改为 `json params_` |
| 2 | async lambda move-capture | 设计已定 / 代码已实现 / 已编译验证 | 评审发现 `unique_ptr` 不可拷贝，`AsyncTaskQueue.h` 已用 `shared_ptr` 自持 |
| 3 | OBJECT library 防 dead-strip | 设计已定 / 代码已实现 / 已编译验证 | 评审发现静态库自注册静默消失，`src/tests/CMakeLists.txt` 已改 OBJECT |

#### 编码实现阶段暴露（Phase 1 骨架搭建时）

| # | 修复项 | 状态 | 说明 |
|---|--------|------|------|
| 4 | enqueueAndWait shared_ptr 竞态 | 设计已定 / 代码已实现 / 已编译验证 | `AsyncTaskQueue.h` 已实现 `shared_ptr` 版，`factory_test` 链接通过 |
| 5 | loadTestConfig 职责归位 + 启动期校验 | 设计已定 / 代码已实现 / 已编译验证 | `TestEngine.cpp:23-37` 已实现 module/topic 存在性检查 |
| 6 | catch(...) 异常守卫 | 设计已定 | 冻结在 spec 的 `dispatch()` 定义中，**实际代码仍为 `// TODO` 桩**，待 Phase 2 实现 |
| 7 | SysfsGpioDriver 去重 | 设计已定 / 代码已实现 / 已编译验证 | 只留 `src/platforms/common/` 一份，falcon CMake 未引用第二份 |

**关键区分**：修复 6（catch(...)）寄生在 `dispatch()` 内部，而 `dispatch()` 在 Phase 1 是空桩。它的异常守卫语义已冻结在 v1.3-final 设计文档中，但**实际代码尚未落地**。

---

## 2. Phase 1 实现详情

### 2.1 提交历史

**生成方式**：`git log --oneline --reverse -- factory_fw/ docs/superpowers/specs/ docs/superpowers/plans/ docs/superpowers/devlog.md`

```
d8578a8 doc(v1.3-final): 产测固件平台化设计文档 — 冻结基线
d423331 doc(v1.3): 产测固件平台化设计文档
fcca28d doc(v1.2): 产测固件平台化设计文档
ec1e833 doc(v1.1): 产测固件平台化设计文档
de1b918 doc(v1.0): 产测固件平台化设计文档 (Section 1-3)
f389216 feat: Phase 1 skeleton — all interfaces, registries, stubs, build system
1e633b0 fix: compilation fixes for null platform
8e3c07a doc: add development log (devlog.md)
d8260e1 doc: 完善开发记录，补充设计迭代详情、编译修复实录、代码 diff
```

> **注意**：此前手抄的提交历史有 SHA 重复和顺序错误，以上为准。

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

### 3.1 已编译验证（null 平台通过）

| 编号 | 内容 | 落点 | 验证方式 |
|------|------|------|----------|
| A | 显式列源文件 | `platforms/falcon/CMakeLists.txt:10-19` | null 编译通过 |
| B | `if(NOT DEFINED FW_ROOT)` | `platforms/{falcon,null}/CMakeLists.txt:5-7` | null 编译通过 |
| C | `SCRIPT_DIR` 锚定 + toolchain 检查 | `build.sh:5-6, 14-15` | 运行 `build.sh` |
| D | `IWifiManager.h` 位置 | `include/platforms/common/interface/IWifiManager.h` | 阅读文件 |
| F | `.at("module").get<string>()` | `src/core/TestEngine.cpp:27` | 阅读代码 |
| H | `TestResult` 工厂方法 | `include/core/TestResult.h:13-21` | 阅读代码 |
| L | `loadTestConfig` 启动期校验 | `src/core/TestEngine.cpp:23-37` | 阅读代码 |

### 3.2 代码已写、未验证（null 构建未覆盖）

| 编号 | 内容 | 未验证原因 | 验证条件 |
|------|------|----------|----------|
| I | `FALCON_SDK` 环境变量 | null 是 x86 本地编译，不走交叉工具链 | 需 `PLATFORM=falcon ./build.sh` 跑通 configure |
| E | 链接传递性 PUBLIC/INTERFACE | null 不链接 mpp/LVGL/mosquitto | 需 falcon 平台实际链接验证 |

### 3.3 待 Phase 2 实现

| 编号 | 内容 | 优先级 | 说明 |
|------|------|--------|------|
| G | mosquitto 线程安全 | 中 | 需引入 mosquitto 库后才能验证 |
| J | 第三方依赖获取方式 | **高** | nlohmann/json 目前是构建期 curl 下载，违反独立性不变式（离线构建断）。**必须在 Phase 2 之前解决**：方案 (i) vendored 单头文件提交进 git；方案 (ii) `find_package` 打 sysroot |
| K | 隔离构建 | **高** | `cp -r factory_fw /tmp/iso && cd /tmp/iso && PLATFORM=null ./build.sh` 三行即可验证，**不需要 CI 基础设施**。建议在 Phase 2 之前跑一遍，把独立性不变式真正证伪 |

### 3.4 代码中 TODO 汇总

| 文件 | 行号 | TODO | 影响 | 冻结状态 |
|------|------|------|------|----------|
| `src/core/TestEngine.cpp:42` | `run()` | MQTT loop 集成 | 固件无法收消息 | 待实现 |
| `src/core/TestEngine.cpp:46` | `onMqttMessage()` | topic→testCfg 查找 | MQTT 消息无法路由 | 待实现 |
| `src/core/TestEngine.cpp:50` | `dispatch()` | 完整 sync/async 派发 + catch(...) 守卫 | 测试项无法执行 | **设计已冻结**，代码待实现 |
| `src/core/TestEngine.cpp:55` | `publishResult()` | `mosquitto_publish` + 锁 | 结果无法上报 | 待实现 |
| 12 个 `src/tests/*Test.cpp` | `run()` | 全部 `skipped("not implemented")` | 无实际测试功能 | 待实现 |
| `src/ble/*.cpp` | 全部 | 空桩 | BLE 未实现 | 待实现 |
| `platforms/falcon/drivers/*.cpp` | 全部 | 空桩 | RV1126B 驱动未实现 | 待实现 |
| `platforms/falcon/build_factory.sh` | 全部 | 仅 `echo` | 不生成固件镜像 | 待实现 |

---

## 4. 关键陷阱详解

### 4.1 静态库 + 自注册 = 静默消失

**机制（精确版）**：
静态库链接时，链接器做 archive member selection——`.o` 文件因为没有任何已解析符号引用它，根本不会从 `.a` 里被抽出来。这**不需要** `--gc-sections`，是默认行为。`--gc-sections` 丢的是已链接对象里的无用 section，是另一回事。两者都能害死自注册，但主因是 archive member selection。

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

## 5. Phase 2 之前必须先做的 3 件事

评审建议，优先级高于 Section 6 的默认顺序：

1. **vendored nlohmann/json** — 把 `third_party/nlohmann/json.hpp` 提交进 git（一次性 populate，以后不再 curl）。现在构建期 curl 违反独立性不变式（离线/隔离构建断）。
2. **跑一遍隔离构建** — `cp -r factory_fw /tmp/fw_iso && cd /tmp/fw_iso && PLATFORM=null ./build.sh`。验证"可整体搬走"这个头号不变式。
3. **falcon configure 至少跑一次** — `PLATFORM=falcon ./build.sh` 到 cmake configure 阶段即可（driver 可以是桩），验证交叉工具链 + `FALCON_SDK` + 依赖装配是否通。

以上三件事做完，Phase 2 的 dispatch + MQTT + driver 迁移才有可靠地基。

---

## 6. 文件索引

### 6.1 设计冻结文档
- `docs/superpowers/specs/2026-06-03-factory-firmware-platform-design.md` — v1.3-final

### 6.2 实现计划
- `docs/superpowers/plans/2026-06-03-factory-firmware-platform.md` — Phase 1 全部 14 个 task

### 6.3 核心接口（冻结点）
- `factory_fw/include/core/FactoryStore.h` — 薄基座
- `factory_fw/include/core/DriverRegistry.h` — 硬件能力注册
- `factory_fw/include/core/ModuleRegistry.h` — 测试方法注册
- `factory_fw/include/core/TestResult.h` — 三态结果 + data 字段
- `factory_fw/include/core/TestContext.h` — 值持有 params
- `factory_fw/include/core/AsyncTaskQueue.h` — 单 worker + shared_ptr 竞态修复
- `factory_fw/include/tests/ITestModule.h` — 模块接口 + REGISTER_TEST_MODULE 宏

### 6.4 平台 Driver 接口（冻结点）
- `factory_fw/include/platforms/common/interface/IPlatform.h` — registerDrivers() 单一面
- `factory_fw/include/platforms/common/interface/I*.h` — 9 个 Driver 接口

### 6.5 通用 HAL（冻结点）
- `factory_fw/include/hal/I2cController.h` — i2cget/i2cset 封装
- `factory_fw/include/hal/RecorderController.h` — IRecorder 注入点
- `factory_fw/include/hal/V4l2Recorder.h` — V4L2 标准录像

### 6.6 构建入口
- `factory_fw/build.sh` — SCRIPT_DIR 锚定
- `factory_fw/CMakeLists.txt` — 聚合
- `factory_fw/cmake/platforms/null.cmake` — x86 本地编译
- `factory_fw/cmake/platforms/falcon.cmake` — FALCON_SDK 环境变量
- `factory_fw/platforms/falcon/CMakeLists.txt` — if(NOT TARGET) 守卫
- `factory_fw/platforms/null/CMakeLists.txt` — 独立/聚合两用

---

## 7. 后续工作入口

**Phase 2 启动条件**：`TestEngine::dispatch` + MQTT 集成完成后，能跑通一个端到端测试（如 `15R battery`）。

**默认顺序**：
1. `TestEngine::dispatch` + `onMqttMessage` + `publishResult`（核心调度）
2. 迁移现有 `src/tests/BatteryTest.cpp` 逻辑到新 `factory_fw/src/tests/BatteryTest.cpp`
3. 迁移 `src/hal/MppEncoder.cpp` → `platforms/falcon/drivers/RkMppEncoder.cpp`
4. 逐个迁移其余平台 driver
5. BLE 模块迁移

**但评审建议 Phase 2 之前先做**：见 Section 5（vendored json、隔离构建、falcon configure）。
