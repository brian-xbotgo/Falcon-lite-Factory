# 编码规范

## 命名约定

| 类型 | 命名 | 示例 |
|------|------|------|
| 接口类 | `I` + PascalCase | `ICameraDriver`, `IBatteryDriver` |
| 实现类 | PascalCase | `Gc4663CameraDriver`, `Om70x0xBatteryDriver` |
| 函数/方法 | camelCase | `probe()`, `readChipId()` |
| 成员变量 | snake_case + 尾下划线 | `drivers_`, `config_` |
| 局部变量 | snake_case | `cam_index`, `test_cfg` |
| 宏 | 全大写 + 下划线 | `REGISTER_TEST_MODULE` |
| 命名空间 | `ft`（factory test）| `namespace ft { ... }` |

## 目录与 include 约定

**统一从 `include/` 根目录引用**：

```cpp
// ✅ 正确
#include "core/DriverRegistry.h"
#include "tests/ITestModule.h"
#include "platforms/common/interface/ICameraDriver.h"

// ❌ 错误（相对当前文件目录）
#include "DriverRegistry.h"
#include "../interface/ICameraDriver.h"
```

**原因**：确保 `factory_fw` 内部任何文件在任何目录层级下，include 路径一致。

## 类设计

### 接口契约

```cpp
class ICameraDriver {
public:
    virtual ~ICameraDriver() = default;
    virtual CameraProbeResult probe(int cam_index) = 0;
    // ...
};
```

- 析构函数必须是 `virtual`，防止通过基类指针 delete 子类对象时内存泄漏
- 接口中不涉及平台细节（如 GPIO 号、I2C 地址），这些从 `platform.json` 读取

### 值持有 vs 引用持有

```cpp
// ❌ 错误：构造函数参数临时对象绑定引用成员
class TestContext {
    const json& params_;  // 悬空炸弹
};

// ✅ 正确：值持有
class TestContext {
    json params_;  // 安全
};
```

## 错误处理

### 测试模块返回值

```cpp
TestResult run(TestContext& ctx) {
    auto cam = ctx.create<ICameraDriver>();
    if (!cam)
        return TestResult::skipped("no camera driver");  // 平台无此能力
    if (!cam->probe(0))
        return TestResult::fail("probe failed");         // 测试失败
    return TestResult::pass();                            // 通过
}
```

### 异常守卫

```cpp
auto task = [mod = std::move(mod), ctx = std::move(ctx)]() -> TestResult {
    try {
        return mod->run(ctx);
    } catch (const std::exception& e) {
        return TestResult::fail(std::string("exception: ") + e.what());
    } catch (...) {
        return TestResult::fail("unknown exception");
    }
};
```

## CMake 约定

1. **不用 `file(GLOB)`** — 新增/删除文件不会自动触发 cmake 重配
2. **显式列源文件**：
   ```cmake
   set(FALCON_DRIVER_SRCS
       drivers/RkMppEncoder.cpp
       drivers/Gc4663CameraDriver.cpp
       ...
   )
   ```
3. **OBJECT library 用于自注册模块**：
   ```cmake
   add_library(factory_tests OBJECT ${TEST_SRCS})
   ```
4. **链接传递性**：
   ```cmake
   target_link_libraries(factory_core PUBLIC factory_common)
   # 下游自动获得 factory_common 的头文件路径和链接
   ```

## 新增平台 Checklist

- [ ] 新建 `platforms/<name>/` 目录
- [ ] 编写 `CMakeLists.txt`（含 `if(NOT DEFINED FW_ROOT)` 回退）
- [ ] 编写 `platform.json`（硬件资源配置）
- [ ] 编写 `tests.json`（测试项注册表）
- [ ] 实现 `IPlatform` 子类 + `registerDrivers()`
- [ ] 编写工具链文件 `cmake/platforms/<name>.cmake`
- [ ] 显式列 `drivers/*.cpp` 源文件
- [ ] `PLATFORM=<name> ./build.sh` 编译通过
