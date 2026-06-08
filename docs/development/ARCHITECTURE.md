# 架构说明

## 当前定位

`factory_fw/` 是产测平台化过程中的临时独立工程根目录，后续平台化稳定后可以整体移出当前仓库。目录内的构建、配置、平台实现不依赖 `factory_fw` 外部源码路径。

当前测试平台为 FALCON（RK3576），目标产品固件背景为 FALCON_AIR（RV1126B）。FALCON 现阶段复用部分 FALCON_AIR 的硬件控制思路，后续再按硬件差异替换平台 driver。

## 分层

```text
factory_fw/
├── src/
│   ├── core/                 # 测试引擎、模块注册、异步队列、结果模型
│   ├── common/               # 通用工具
│   ├── config/               # platform.json / tests.json 读取
│   ├── control/              # 通用控制层：I2C、GPIO、V4L2、录制控制等
│   ├── tests/                # 产测项目实现
│   ├── ble/                  # BLE / MQTT 桥接（FALCON 启用）
│   └── platforms/common/     # 平台接口、Base 兜底实现、DriverRegistry 接入
├── platforms/
│   ├── base/                 # 本地基础平台，用于 x86 编译和框架回归
│   └── falcon/               # FALCON / RK3576 平台实现
└── cmake/platforms/          # 平台 toolchain 文件
```

## 关键约定

- `src/control` 是通用控制层，不再称为 HAL 通用层；它放平台无关的控制封装，例如 `I2cController`、`GpioController`、`V4l2Recorder`、`RecorderController`。
- `platforms/common/interface` 定义跨平台 driver 接口；平台真实硬件能力由 `platforms/<platform>/drivers` 实现并注册。
- `Base*` 是基础兜底实现，替代原来的 `Null*` 命名；基础平台名统一为 `base`。
- `factory_fw/src/CMakeLists.txt` 是 src 内唯一 CMake 入口，负责 core/common/config/control/tests/platform_common/ble 的目标定义。
- 平台 CMake 只负责平台 driver、平台链接和最终 `factory_test` 可执行文件。

## 数据流

```text
MQTT topic
  -> TestEngine::onMqttMessage()
  -> tests.json 查找测试项
  -> ModuleRegistry 创建测试模块
  -> TestContext 从 DriverRegistry 获取平台 driver
  -> 测试模块执行并生成 TestResult
  -> TestEngine 发布结果
```

## 当前阶段

第二阶段已完成平台接口、调度链路、base/falcon 双平台编译验证。第三阶段开始迁移真实 driver、接入 MQTT/BLE，并清理构建和命名边界。