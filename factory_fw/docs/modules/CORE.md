# Core 模块说明

## 职责

`core/` 是产测固件的心脏，包含：
- 测试引擎（MQTT 消息接收、topic 路由、结果上报）
- 异步任务队列（单 worker 串行执行）
- 双注册中心（DriverRegistry + ModuleRegistry）
- 测试抽象（TestResult、TestContext、ITestModule）

**平台相关性**：零。

## 文件清单

| 文件 | 职责 |
|------|------|
| `include/core/FactoryStore.h` | 薄基座：通用「键→工厂」存储，模板实现 |
| `include/core/DriverRegistry.h` | 硬件能力注册表：按 `type_index` 索引 |
| `include/core/ModuleRegistry.h` | 测试方法注册表：按字符串名索引，含 `REGISTER_TEST_MODULE` 宏 |
| `include/core/TestResult.h` | 测试结果：Pass/Fail/Skipped + detail + data(JSON) |
| `include/core/TestContext.h` | 测试运行时上下文：读配置、取 driver、取参数 |
| `include/core/AsyncTaskQueue.h` | 单 worker 异步队列：enqueue / enqueueAndWait |
| `include/core/TestEngine.h` | 测试引擎：MQTT 集成、调度、结果上报 |
| `src/core/ModuleRegistry.cpp` | ModuleRegistry 单例实现 |
| `src/core/AsyncTaskQueue.cpp` | 单 worker 线程实现 |
| `src/core/TestEngine.cpp` | 引擎骨架（TODO：dispatch / MQTT / publishResult） |

## DriverRegistry

```cpp
DriverRegistry reg;
reg.bind<ICameraDriver>([]{ return std::make_unique<Gc4663CameraDriver>(); });

auto cam = reg.create<ICameraDriver>();  // 返回 unique_ptr<ICameraDriver>
if (!cam) /* 平台未绑定此接口 */;
```

**关键**：`create<I>()` 未命中返回 `nullptr`，测试模块应返回 `TestResult::skipped`。

## ModuleRegistry + REGISTER_TEST_MODULE

```cpp
// tests/CameraTest.cpp
class CameraTest : public ITestModule {
    TestResult run(TestContext& ctx) override { ... }
};
REGISTER_TEST_MODULE("camera", CameraTest);
```

**原理**：宏生成一个静态 bool 变量，在 `main()` 之前完成注册。

**陷阱**：若编译为 STATIC library，链接器可能 dead-strip 整个 `.o` → **必须用 OBJECT library**。

## AsyncTaskQueue

```cpp
AsyncTaskQueue queue;

// async：发了就走
queue.enqueue([]{ doSomething(); });

// sync：阻塞等待结果
auto result = queue.enqueueAndWait([]{ return compute(); });
```

**实现细节**：`enqueueAndWait` 用 `std::shared_ptr<std::packaged_task>` 拷贝进 lambda，避免栈引用竞态。

## TestContext

测试模块的唯一外部把手：

```cpp
class TestContext {
    template<class I> std::unique_ptr<I> create() const;  // 取 driver
    const PlatformConfig& config() const;                  // 读平台配置
    const nlohmann::json& params() const;                  // 读测试参数（来自 tests.json）
};
```

**约束**：测试模块只认 `TestContext`，不 `#include` 任何平台头文件。

## TestEngine（当前状态）

Phase 1 骨架已完成：
- `loadTestConfig()`：启动期校验 tests.json（module 存在性、topic 完整性）
- `run()`：空循环（TODO：集成 MQTT loop）
- `onMqttMessage()`：空桩（TODO：topic → testCfg 查找）
- `dispatch()`：空桩（TODO：sync/async 派发 + 异常守卫）
- `publishResult()`：空桩（TODO：mosquitto_publish + 锁）
