# Tests 模块说明

## 职责

`tests/` 是产测业务逻辑层，包含所有测试项的具体实现。

**设计原则**：每个测试项一个类，只认 `TestContext`，不接触平台细节。

## 文件清单

| 文件 | 职责 | 对应 topic |
|------|------|------------|
| `include/tests/ITestModule.h` | 测试模块纯虚接口 | — |
| `src/tests/RtcTest.cpp` | RTC 功能测试 | `10R` |
| `src/tests/MicTest.cpp` | 麦克风测试 | `11R`、`23R` |
| `src/tests/SocTest.cpp` | SoC 子系统检测 | `12R` |
| `src/tests/TfCardTest.cpp` | TF 卡读写测试 | `14R` |
| `src/tests/BatteryTest.cpp` | 电池电压/百分比检测 | `15R`、`24R` |
| `src/tests/WifiTest.cpp` | WiFi 扫描/连接/信号强度 | `16R`、`37R` |
| `src/tests/KeyTest.cpp` | 按键响应测试 | `17R`、`20R` |
| `src/tests/CameraTest.cpp` | 摄像头探测 + OTP 验证 | `18R`、`22R`、`28R` |
| `src/tests/HallTest.cpp` | 霍尔传感器测试 | `21R`、`27R` |
| `src/tests/SysTest.cpp` | 系统版本 + 日志检查 | `26R`、`31R`、`50R` |
| `src/tests/AgingTest.cpp` | 老化压测 | `30R`、`32R`、`34R`、`36R` |
| `src/tests/MotorTest.cpp` | 电机功能测试 | `191R`、`192R`、`291R`、`292R`、`293R`、`294R` |

## 接口契约

```cpp
class ITestModule {
public:
    virtual ~ITestModule() = default;
    virtual TestResult run(TestContext& ctx) = 0;
};
```

**约束**：
- `run()` 必须幂等（可重复调用）
- 异常由调用方捕获（`TestEngine::dispatch`），模块内部可抛但不必 try-catch
- 返回值只能是 `pass()`、`fail(...)`、`skipped(...)` 三种

## 注册方式

```cpp
class CameraTest : public ITestModule {
public:
    TestResult run(TestContext& ctx) override { ... }
};

REGISTER_TEST_MODULE("camera", CameraTest);
```

`"camera"` 是 tests.json 里 `module` 字段的值，必须全局唯一。

## 典型实现模式

### 模式 1：有 driver → 正常测试

```cpp
TestResult CameraTest::run(TestContext& ctx) {
    auto cam = ctx.create<ICameraDriver>();
    if (!cam)
        return TestResult::skipped("no camera driver");

    auto result = cam->probe(0);
    if (!result.ok)
        return TestResult::fail(result.detail);

    return TestResult::pass();
}
```

### 模式 2：无 driver → 纯 Shell 测试

```cpp
TestResult SysTest::run(TestContext& ctx) {
    auto out = ShellUtils::exec("cat /etc/version");
    if (out.exitCode != 0)
        return TestResult::fail("cannot read version");
    return TestResult::pass();
}
```

### 模式 3：参数化测试

```cpp
TestResult MotorTest::run(TestContext& ctx) {
    const auto& p = ctx.params();  // 来自 tests.json
    int duration = p.value("duration_ms", 3000);
    // ...
}
```

## 新增测试项 Checklist

- [ ] 新建 `src/tests/XxxTest.cpp`
- [ ] 实现 `ITestModule` 子类
- [ ] 文件末尾加 `REGISTER_TEST_MODULE("xxx", XxxTest)`
- [ ] 在平台 `tests.json` 中注册 topic → module 映射
- [ ] `CMakeLists.txt` 中 `factory_tests` 的源文件列表追加新 `.cpp`
- [ ] 编译验证（base 平台可快速验证链接性）
