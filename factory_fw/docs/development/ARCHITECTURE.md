# 架构说明

## 核心不变式

> **factory_fw 是可整体搬走的独立项目根。**
>
> 判据：把 `factory_fw/` 整个目录拷到任意路径，`cd` 进去 `./build.sh` 必须能编出固件。
>
> 推论：`factory_fw` 内部任何文件都不许引用 `factory_fw` 之外的路径。

## 分层架构

```
┌─────────────────────────────────────────────┐
│  Application 层                              │
│  main.cpp — 通用入口，零平台相关代码          │
└─────────────────────────────────────────────┘
                      │
    ┌─────────────────┼─────────────────┐
    ▼                 ▼                 ▼
┌──────────┐   ┌──────────┐   ┌──────────────┐
│  core/   │   │ common/  │   │   config/    │
│ 测试引擎  │   │ 通用工具  │   │  JSON 配置   │
│ 异步队列  │   │ 协议常量  │   │              │
│ 注册中心  │   │ ShellUtils│   │              │
└──────────┘   └──────────┘   └──────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────┐
│  平台抽象层                                   │
│  platforms/common/interface/ — 纯虚接口      │
│  platforms/<platform>/drivers/ — 平台实现    │
└─────────────────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────┐
│  HAL 通用层                                   │
│  hal/ — 平台无关的硬件封装（I2C、V4L2 等）   │
└─────────────────────────────────────────────┘
```

## 双注册机制

系统中有两个正交注册表：

| 注册表 | 维度 | 键类型 | 用途 |
|--------|------|--------|------|
| `DriverRegistry` | 硬件能力 | `std::type_index` | 平台登记拥有的 driver，测试通过 `TestContext::create<I>()` 获取 |
| `ModuleRegistry` | 测试方法 | `std::string` | 测试模块通过 `REGISTER_TEST_MODULE` 宏自注册，引擎按 topic 派发 |

## 关键设计决策

1. **IPlatform 只保留 `registerDrivers()`** — 单一真相源，避免 `createXxx()` 与 `registerDrivers()` 漂移
2. **配置文件放在平台层** — `platforms/<platform>/platform.json` + `tests.json`，各平台自维护
3. **单 worker 串行** — `AsyncTaskQueue` 单线程，所有测试入队串行执行，硬件访问由架构保证串行
4. **tests 库用 OBJECT library** — 防止静态库 dead-strip 导致自注册符号被静默丢弃
5. **异常守卫 `catch(...)`** — worker 线程永不崩溃，任何异常都转为 `TestResult::fail`

## 数据流

```
MQTT topic "15R" → TestEngine::onMqttMessage()
                        ↓
                  查找 tests.json 中对应项
                        ↓
                  ModuleRegistry::create("battery")
                        ↓
                  BatteryTest::run(TestContext)
                        ↓
                  ctx.create<IBatteryDriver>()
                        ↓
                  DriverRegistry → Cw221xBatteryDriver (Falcon 平台)
                        ↓
                  TestResult::pass() / fail() / skipped()
                        ↓
                  publishResult() → MQTT 上报
```
