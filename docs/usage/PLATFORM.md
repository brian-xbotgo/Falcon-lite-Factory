# 平台配置说明

## 平台目录

每个平台一个目录：

```text
platforms/<platform>/
├── CMakeLists.txt
├── build_factory.sh
├── platform.json
├── tests.json
└── drivers/
```

`base` 平台没有专用 driver，使用 `src/platforms/common/Base*.cpp` 中的兜底实现。`falcon` 平台在 `platforms/falcon/drivers/` 中提供真实硬件 driver。

## platform.json

`platform.json` 描述硬件资源和平台名：

```json
{
  "platform": "falcon",
  "gpio": {
    "led": { "white": 170, "red": 169 }
  },
  "i2c": {
    "cam0": { "bus": 3, "addr": "0x29" }
  }
}
```

平台名必须和平台注册名一致，例如 `base`、`falcon`。

## 新增平台

1. 复制 `platforms/base` 或参考 `platforms/falcon` 新建平台目录。
2. 编写 `<Platform>Platform.cpp`，实现 `IPlatform::registerDrivers()`。
3. 明确列出平台 driver 源文件，不使用 `file(GLOB)`。
4. 新增 `cmake/platforms/<platform>.cmake`。
5. 执行 `PLATFORM=<platform> ./build.sh`。

## CMake 约定

平台 CMake 只做平台相关内容：

- 设置 `FW_ROOT` fallback。
- 通过 `add_subdirectory(${FW_ROOT}/src ...)` 引入统一 src CMake。
- 定义平台 driver library。
- 链接 `factory_core`、`factory_common`、`factory_config`、`factory_control`、`factory_tests` 等目标。