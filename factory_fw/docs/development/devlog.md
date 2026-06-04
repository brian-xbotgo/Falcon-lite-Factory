# 产测固件平台化开发记录

> 记录设计评审决策、实现过程陷阱、待办状态。
> 与设计文档（specs/）和实现计划（plans/）互补——spec 讲"应该是什么样"，plan 讲"怎么一步步做"，devlog 讲"实际发生了什么"。

---

## 目录

- [阶段 1：骨架搭建](#阶段-1骨架搭建)
- [阶段 2：驱动解耦 + 调度链路贯通](#阶段-2驱动解耦--调度链路贯通)

---

## 阶段 1：骨架搭建

### 1.1 设计迭代详情

#### 1.1.1 v1.0 → v1.1：独立性重构

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

#### 1.1.2 v1.1 → v1.2：双注册语义修正

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

#### 1.1.3 v1.2 → v1.3-final：技术修复

**触发原因**：代码审查发现编译期和运行期硬伤。

以下修复按「状态」分类——设计评审阶段发现的问题 vs 编码实现阶段暴露的问题：

**设计评审阶段发现并冻结（v1.2 之前）**

| # | 修复项 | 状态 | 说明 |
|---|--------|------|------|
| 1 | TestContext::params_ 值持有 | 设计已定 / 代码已实现 / 已编译验证 | 评审发现构造函数临时对象绑定引用成员的 UB，`TestContext.h` 已改为 `json params_` |
| 2 | async lambda move-capture | 设计已定 / 代码已实现 / 已编译验证 | 评审发现 `unique_ptr` 不可拷贝，`AsyncTaskQueue.h` 已用 `shared_ptr` 自持 |
| 3 | OBJECT library 防 dead-strip | 设计已定 / 代码已实现 / 已编译验证 | 评审发现静态库自注册静默消失，`src/tests/CMakeLists.txt` 已改 OBJECT |

**编码实现阶段暴露（Phase 1 骨架搭建时）**

| # | 修复项 | 状态 | 说明 |
|---|--------|------|------|
| 4 | enqueueAndWait shared_ptr 竞态 | 设计已定 / 代码已实现 / 已编译验证 | `AsyncTaskQueue.h` 已实现 `shared_ptr` 版，`factory_test` 链接通过 |
| 5 | loadTestConfig 职责归位 + 启动期校验 | 设计已定 / 代码已实现 / 已编译验证 | `TestEngine.cpp:23-37` 已实现 module/topic 存在性检查 |
| 6 | catch(...) 异常守卫 | 设计已定 | 冻结在 spec 的 `dispatch()` 定义中，**实际代码仍为 `// TODO` 桩**，待 Phase 2 实现 |
| 7 | SysfsGpioDriver 去重 | 设计已定 / 代码已实现 / 已编译验证 | 只留 `src/platforms/common/` 一份，falcon CMake 未引用第二份 |

**关键区分**：修复 6（catch(...)）寄生在 `dispatch()` 内部，而 `dispatch()` 在 Phase 1 是空桩。它的异常守卫语义已冻结在 v1.3-final 设计文档中，但**实际代码尚未落地**。

### 1.2 Phase 1 实现详情

#### 1.2.1 提交历史

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

#### 1.2.2 null 平台编译验证实录

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

**Round 1：nlohmann/json 缺失**
```
fatal error: nlohmann/json.hpp: No such file or directory
```
**修复**：`curl -o third_party/nlohmann/json.hpp ...`

**Round 2：include 路径错误**
文件在 `third_party/nlohmann/json.hpp`，CMake 路径是 `third_party/nlohmann`，编译器查找 `third_party/nlohmann/nlohmann/json.hpp`——不匹配。
**修复**：4 个 CMakeLists.txt 中 `third_party/nlohmann` → `third_party`

**Round 3：ModuleRegistry 类型不匹配 + ITestModule 不完整**
**修复**：lambda 转换 + 补全 include

**Round 4：main.cpp 命名空间 + 类型不匹配**
**修复**：`using namespace ft;` + `cfg.platformName().c_str()`

### 1.3 Phase 1 待办清单状态

| 编号 | 内容 | 状态 |
|------|------|------|
| A | 显式列源文件 | ✅ null 编译通过 |
| B | `if(NOT DEFINED FW_ROOT)` | ✅ null 编译通过 |
| C | `SCRIPT_DIR` 锚定 + toolchain 检查 | ✅ 运行 `build.sh` |
| D | `IWifiManager.h` 位置 | ✅ 阅读文件 |
| F | `.at("module").get<string>()` | ✅ 阅读代码 |
| H | `TestResult` 工厂方法 | ✅ 阅读代码 |
| L | `loadTestConfig` 启动期校验 | ✅ 阅读代码 |
| I | `FALCON_SDK` 环境变量 | ⏳ 需 falcon 平台验证 |
| E | 链接传递性 PUBLIC/INTERFACE | ⏳ 需 falcon 实际链接 |

### 1.4 Phase 1 代码中 TODO 汇总

| 文件 | TODO | 影响 |
|------|------|------|
| `src/core/TestEngine.cpp:run()` | MQTT loop 集成 | 固件无法收消息 |
| `src/core/TestEngine.cpp:onMqttMessage()` | topic→testCfg 查找 | MQTT 消息无法路由 |
| `src/core/TestEngine.cpp:dispatch()` | 完整 sync/async 派发 + catch(...) 守卫 | 测试项无法执行 |
| `src/core/TestEngine.cpp:publishResult()` | `mosquitto_publish` + 锁 | 结果无法上报 |
| 12 个 `src/tests/*Test.cpp` | 全部 `skipped("not implemented")` | 无实际测试功能 |
| `src/ble/*.cpp` | 空桩 | BLE 未实现 |
| `platforms/falcon/drivers/*.cpp` | 空桩 | RK3576 驱动未实现 |
| `platforms/falcon/build_factory.sh` | 仅 `echo` | 不生成固件镜像 |

---

## 阶段 2：驱动解耦 + 调度链路贯通

### 2.1 目标

让 **"驱动注册 → 测试获取 → 调度执行 → 结果上报"** 整条链路跑通。

Phase 1 的骨架中，以下关键链路节点仍为 `// TODO` 或空桩：
- `FalconPlatform`：不存在，falcon 8 个 driver 散落在空文件中，无统一注册入口
- `TestEngine::dispatch()`：空桩
- `TestEngine::publishResult()`：空桩
- 12 个测试模块：全部 `skipped("not implemented")`

阶段 2 的目标是让这些节点**最小可编译地动起来**，硬件细节精确实现留给后续阶段。

### 2.2 任务清单与执行结果

| # | 任务 | 优先级 | 状态 | 关键文件 |
|---|------|--------|------|----------|
| 1 | 创建 FalconPlatform | 高 | ✅ | `platforms/falcon/FalconPlatform.cpp` |
| 2 | 填充 8 个 Falcon driver stub | 高 | ✅ | `platforms/falcon/drivers/*.h` + `*.cpp` |
| 3 | 更新 falcon CMakeLists.txt | 高 | ✅ | `platforms/falcon/CMakeLists.txt` |
| 4 | 填充 12 个测试模块骨架 | 高 | ✅ | `src/tests/*Test.cpp` |
| 5 | 实现 TestEngine::dispatch() | 高 | ✅ | `src/core/TestEngine.cpp` |
| 6 | 实现 TestEngine::publishResult() | 高 | ✅ | `src/core/TestEngine.cpp` |
| 7 | 双平台编译验证 | 高 | ✅ | `build/null/` + `build/falcon/` |
| 8 | main.cpp 环境变量支持 | 中 | ✅ | `src/main.cpp` |
| 9 | null 平台配置补齐 | 中 | ✅ | `platforms/null/platform.json` + `tests.json` |

### 2.3 陷阱与修复实录

#### 2.3.1 陷阱 A：`std::unique_ptr<Derived>` 无法放入 `std::function<std::unique_ptr<Base>()>`

**暴露位置**：`FalconPlatform::registerDrivers()` 初次实现时

**问题代码**：
```cpp
reg.bind<IEncoder>([] { return std::make_unique<RkMppEncoder>(); });
// 错误：lambda 返回 unique_ptr<RkMppEncoder>，不能隐式转为
//        function<unique_ptr<IEncoder>()>
```

**根因**：`std::unique_ptr` 不是协变的。`std::function` 的模板参数要求返回值类型精确匹配，不允许隐式转换。

**修复方案**：修改 `DriverRegistry::bind` 的模板签名，从接受 `std::function` 改为接受通用 Factory：
```cpp
// 修复前
template<class Interface>
void bind(std::function<std::unique_ptr<Interface>()> factory);

// 修复后
template<class Interface, class Factory>
void bind(Factory factory) {
    store_.put(typeid(Interface), [f = std::move(factory)]() -> void* {
        return f().release();
    });
}
```

**验证**：falcon 平台编译通过。

#### 2.3.2 陷阱 B：lambda 捕获 `unique_ptr` → `std::function` 不可复制

**暴露位置**：`TestEngine::dispatch()` 初次实现时

**问题代码**：
```cpp
auto task = [mod = std::move(mod), ctx, topic, this]() mutable -> TestResult {
    return mod->run(ctx);
};
asyncQueue_.enqueue(task);  // 错误：task 含 unique_ptr，不可复制
```

**根因**：`std::function` 要求其目标类型必须可复制构造（copy-constructible）。move-only 的 lambda 无法放入 `std::function`。

**修复方案**：`unique_ptr` 转 `shared_ptr`：
```cpp
auto modShared = std::shared_ptr<ITestModule>(std::move(mod));
auto task = [modShared, ctx, topic, this]() mutable -> TestResult {
    return modShared->run(ctx);
};
```

**验证**：null 平台自测通过。

#### 2.3.3 陷阱 C：静态库 dead-strip 再次发作（平台注册）

**暴露位置**：null 平台运行时 `createPlatform("null")` 返回 `nullptr`

**根因**：`NullPlatform.cpp` 中的 `_reg = registerPlatformFactory("null", ...)` 所在的 `.o` 文件未被任何代码直接引用，链接器做 archive member selection 时整个丢弃。

**复现**：
```bash
nm build/null/factory_test | grep _reg_null
# 无输出——符号被丢弃
```

**修复方案**：把 `NullPlatform` 类定义和 `createNullPlatform()` 合并到 `PlatformFactory.cpp` 同一 TU 中。`PlatformFactory.o` 因为有 `createPlatform()` 被 `main.cpp` 引用，所以不会被丢弃，`createNullPlatform()` 和 `_reg_null` 随之保留。

```cpp
// PlatformFactory.cpp
// ... createPlatform() 实现 ...

class NullPlatform : public IPlatform { ... };
std::unique_ptr<IPlatform> createNullPlatform() { ... }
static bool _reg_null = registerPlatformFactory("null", createNullPlatform);
```

**验证**：null 平台运行时 `createPlatform("null")` 成功返回非空指针。

**评审发现 falcon 同样中枪**：`nm build/falcon/factory_test | grep _reg_falcon` 零输出——`_reg_falcon` 同样被 archive member selection 丢弃。之前认为"vtable 引用保留整个 .o"的推理是循环的：vtable 只有在 `.o` 被链进来后才存在，而 `.o` 被链进来的前提是 `_reg_falcon` 先把条目填进注册表……

**统一修复方案**：平台 library 链接时加 `--whole-archive`，与测试模块走 OBJECT library 的套路保持一致（同一原理，不同实现）：
```cmake
target_link_libraries(factory_test
    factory_core
    factory_common
    ...
    -Wl,--whole-archive
    platform_falcon
    -Wl,--no-whole-archive
    factory_platform_common
)
```
**注意**：`factory_platform_common` 必须放在 `--whole-archive` 之后，否则 `FalconPlatform.o` 中引用 `SysfsGpioDriver` vtable 时，链接器已处理完 `factory_platform_common`。

**验证**：
```bash
nm build/falcon/factory_test | grep _reg_falcon
# 00000000000c6098 b _ZN2ftL11_reg_falconE  ← 符号存在
```

#### 2.3.4 陷阱 D：平台 driver include 路径缺失

**暴露位置**：falcon 平台首次编译时

**问题**：`platforms/falcon/drivers/*.cpp` 中的 `#include "platforms/common/interface/IBatteryDriver.h"` 找不到头文件。

**修复**：在 `platforms/falcon/CMakeLists.txt` 中给 `platform_falcon` target 添加：
```cmake
target_include_directories(platform_falcon PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}   # 让 #include "drivers/xxx.h" 能找到
    ${FW_ROOT}/include
    ${FW_ROOT}/third_party
)
```

#### 2.3.5 陷阱 E：ITestModule 不完整类型

**暴露位置**：`TestEngine.cpp` 编译时

**问题**：`TestEngine.cpp` 只 `#include "core/ModuleRegistry.h"`，其中 `ITestModule` 是前置声明。`dispatch()` lambda 中调用 `mod->run(ctx)` 需要完整类型。

**修复**：`TestEngine.cpp` 添加 `#include "tests/ITestModule.h"`。

#### 2.3.6 陷阱 F：SysfsGpioDriver 定义在 .cpp 中，无法跨 TU make_unique

**暴露位置**：`FalconPlatform.cpp` 绑定 `SysfsGpioDriver` 时

**问题**：`SysfsGpioDriver` 类定义在 `src/platforms/common/SysfsGpioDriver.cpp` 中，其他 TU 看不到完整定义，`std::make_unique<SysfsGpioDriver>()` 编译失败。

**修复**：提取类声明到 `include/platforms/common/SysfsGpioDriver.h`，`.cpp` 只保留方法实现。`FalconPlatform.cpp` 和 `NullPlatform.cpp`（已合并到 PlatformFactory.cpp）均可 include 该头文件。

### 2.4 编译验证实录

#### 2.4.1 null 平台（x86-64 本地编译）

**环境**：WSL2, Ubuntu 22.04, GCC 11.4.0, CMake 3.22

**命令**：
```bash
cd /home/gdh/FACTORY_GIT/factory_fw
PLATFORM=null ./build.sh
FACTORY_PLATFORM_JSON=platforms/null/platform.json \
  FACTORY_TESTS_JSON=platforms/null/tests.json \
  FACTORY_SELF_TEST=1 \
  ./build/null/factory_test
```

**输出**：
```
[SelfTest] triggering battery (sync)...
[Result] topic=15R status=SKIP detail=no battery driver
[SelfTest] triggering camera (async)...
[Result] topic=18R status=SKIP detail=no camera driver
[SelfTest] triggering sys (async)...
[Result] topic=26R status=FAIL detail=cannot read version
[SelfTest] done
```

**解读**：
- `battery` → `SKIP`（null 平台无 battery driver，测试正确回退到 skipped）
- `camera` → `SKIP`（null 平台无 camera driver，同上）
- `sys` → `FAIL`（`/etc/version` 在开发环境中不存在，ShellUtils 正确返回 fail）
- **sync/async 分支均工作正常**
- **dispatch → driver 获取 → 测试执行 → result 上报 链路贯通**

#### 2.4.2 falcon 平台（ARM64 交叉编译）

**环境**：WSL2, `FALCON_SDK=/home/gdh/falcon/Omni3576-sdk/buildroot/output/rockchip_rk3576_ipc/host`

**命令**：
```bash
export FALCON_SDK=/home/gdh/falcon/Omni3576-sdk/buildroot/output/rockchip_rk3576_ipc/host
PLATFORM=falcon ./build.sh
```

**输出**：
```
[100%] Linking CXX executable factory_test
[100%] Built target factory_test
[build_factory] Packaging firmware from /home/gdh/FACTORY_GIT/factory_fw/build/falcon
[build_factory] Done
```

**产物验证**：
```bash
file build/falcon/factory_test
# ELF 64-bit LSB pie executable, ARM aarch64
```

**运行限制**：ARM 二进制无法在 x86 主机直接执行（缺少 aarch64 解释器），需在目标板或 QEMU 中运行。这属于交叉编译的预期限制，不影响编译验证的有效性。

### 2.5 代码变更摘要

#### 2.5.1 新增文件

| 文件 | 说明 |
|------|------|
| `platforms/falcon/FalconPlatform.cpp` | Falcon 平台总入口，registerDrivers() 绑定 9 个 driver |
| `platforms/falcon/drivers/*.h` (8 个) | Driver 类声明（inline stub 实现） |
| `platforms/falcon/drivers/*.cpp` (8 个) | Driver 源文件（仅 `#include` 对应 .h） |
| `include/platforms/common/SysfsGpioDriver.h` | 通用 GPIO driver 声明，跨平台复用 |
| `platforms/null/platform.json` | null 平台基础配置 |
| `platforms/null/tests.json` | null 平台测试注册表（4 项，用于自测） |

#### 2.5.2 修改文件

| 文件 | 变更 |
|------|------|
| `include/core/DriverRegistry.h` | `bind()` 签发改通用 Factory 模板 |
| `src/core/TestEngine.cpp` | 实现 dispatch()、publishResult()、onMqttMessage() |
| `src/main.cpp` | 支持环境变量覆盖配置路径 + 自测模式 |
| `src/platforms/common/PlatformFactory.cpp` | 合并 NullPlatform，防 dead-strip |
| `src/platforms/common/SysfsGpioDriver.cpp` | 提取声明到 .h，只留实现 |
| `src/platforms/common/CMakeLists.txt` | 移除 NullPlatform.cpp |
| `platforms/falcon/CMakeLists.txt` | 加入 FalconPlatform.cpp + include 路径 |
| `src/tests/*Test.cpp` (12 个) | 从空桩填充为最小可执行骨架 |

### 2.6 设计决策记录

#### 2.6.1 决策 1：Driver stub 用 .h/.cpp 分离还是全内联？

- **选项 A**：全部内联在 `.h` 中，`.cpp` 不需要
- **选项 B**：声明在 `.h`，实现在 `.cpp`
- **选择 B（但 stub 阶段把实现放在 .h）**：因为未来真实 driver 实现会很大（MPP 编码、I2C 通信、V4L2 操作），`.h` 只保留声明，`.cpp` 填实现。当前 stub 阶段 `.cpp` 只有 `#include "drivers/xxx.h"`，过渡期后逐步替换。

#### 2.6.2 决策 2：dispatch 中 `unique_ptr` → `shared_ptr` 是否合理？

- **问题**：`std::function` 要求可复制，但 `unique_ptr` 只能移动
- **选项 A**：修改 `AsyncTaskQueue::enqueue` 接受 `std::move_only_function`（C++23）
- **选项 B**：用 `shared_ptr` 包装
- **选项 C**：在入队前 release raw pointer，手动管理生命周期
- **选择 B**：`shared_ptr` 语义正确（测试模块的生命周期由 task 和调用方共同持有），代码简洁，无需 C++23。唯一的轻微开销是引用计数，可忽略。

#### 2.6.3 决策 3：publishResult 先写 stdout 还是直接接 mosquitto？

- **选择**：先写 stdout。原因：
  1. MQTT 库（mosquitto）尚未引入构建系统
  2. stdout 输出足够验证链路贯通
  3. 后续替换为 `mosquitto_publish` 是局部改动，不影响架构

### 2.7 待办清单更新（阶段 2 结束后）

#### 2.7.1 已解决（从 Phase 1 TODO 中移除）

| Phase 1 TODO | 解决方式 |
|--------------|----------|
| `TestEngine::dispatch()` 空桩 | 已实现完整 sync/async 分支 + 异常守卫 |
| `TestEngine::publishResult()` 空桩 | 已实现 stdout 输出 + mutex 保护 |
| 12 个测试模块空桩 | 已填充最小骨架 |
| `platforms/falcon/drivers/*.cpp` 空桩 | 已填充最小可编译 stub |
| falcon configure 验证 | `PLATFORM=falcon ./build.sh` 编译通过 |

#### 2.7.2 仍待后续阶段

| # | 内容 | 优先级 | 说明 |
|---|------|--------|------|
| G | mosquitto 线程安全 | 中 | 需引入 mosquitto 库后才能验证 |
| J | 第三方依赖获取方式 | ✅ | `third_party/nlohmann/json.hpp` 已 vendored 提交（3.11.3 单头文件），隔离构建验证通过 |
| K | 隔离构建 | ✅ | `cp -r factory_fw /tmp/fw_iso && cd /tmp/fw_iso && PLATFORM=null ./build.sh` 编译通过 |
| M | MQTT 集成 | 高 | `onMqttMessage` 目前从 tests.json 查找 topic，需接入 mosquitto loop |
| N | BLE 模块迁移 | 中 | `src/ble/*.cpp` 仍为 stub |
| O | 平台 driver 真实实现 | 中 | 8 个 driver stub 需从旧代码迁移真实硬件逻辑 |
| P | build_factory.sh 打包 | 低 | 目前仅 echo，需生成固件镜像 |

#### 2.7.3 上轮 IWYU 遗留（本轮确认已解决）

以下 3 条来自上轮评审，本轮验证时确认已落在代码中，但之前 devlog 未记录：

| # | 问题 | 落点 | 状态 |
|---|------|------|------|
| 1 | `running_` 应改 `std::atomic<bool>` | `include/core/TestEngine.h:34` | ✅ `std::atomic<bool> running_{true};` |
| 2 | `TestEngine.cpp` 应显式 `#include <thread>/<chrono>` | `src/core/TestEngine.cpp:9-10` | ✅ 已包含 |
| 3 | `TestEngine.h` include 路径一致性 | `include/core/TestEngine.h` | ✅ 统一从 `include/` 根目录引用 `core/DriverRegistry.h` |

### 2.8 评审检查项

以下检查项供阶段 2 评审使用：

- [ ] `FalconPlatform::registerDrivers()` 是否绑定了全部 9 个接口？
- [ ] `DriverRegistry::bind()` 模板签名是否正确处理 `unique_ptr<Derived>`？
- [ ] `TestEngine::dispatch()` 是否同时支持 sync 和 async 分支？
- [ ] `TestEngine::dispatch()` 是否有 `catch(...)` 异常守卫？
- [ ] `publishResult()` 是否有 `mqttMutex_` 锁保护？
- [ ] null 平台运行时 `createPlatform("null")` 是否返回非空？
- [ ] falcon 平台交叉编译是否通过？
- [ ] 自测输出中 sync/async 测试是否都产生 `[Result]` 行？
- [ ] 12 个测试模块是否都使用了 `ctx.create<>()` 或 `ShellUtils`（而非直接 new 平台类）？
- [ ] `SysfsGpioDriver` 是否只有一份定义（无重复）？

---

## 文件索引

### 设计冻结文档
- `factory_fw/docs/development/ARCHITECTURE.md` — 架构说明
- `factory_fw/docs/development/CODING_GUIDE.md` — 编码规范
- `docs/superpowers/specs/2026-06-03-factory-firmware-platform-design.md` — v1.3-final

### 核心接口
- `factory_fw/include/core/DriverRegistry.h` — 通用 Factory 模板
- `factory_fw/include/core/TestEngine.h` — dispatch / publishResult 声明
- `factory_fw/include/tests/ITestModule.h` — 测试模块接口

### 平台实现
- `factory_fw/platforms/falcon/FalconPlatform.cpp` — 平台总入口
- `factory_fw/platforms/falcon/drivers/*.h` — 8 个 driver 声明
- `factory_fw/platforms/falcon/platform.json` — 硬件资源配置
- `factory_fw/platforms/falcon/tests.json` — 测试项注册表

### 构建入口
- `factory_fw/build.sh` — SCRIPT_DIR 锚定
- `factory_fw/platforms/falcon/CMakeLists.txt` — 平台自包含 CMake
- `factory_fw/platforms/null/CMakeLists.txt` — 独立/聚合两用
