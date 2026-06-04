# 平台配置说明

## 平台目录结构

每个平台一个目录，自包含：

```
platforms/<platform_name>/
├── CMakeLists.txt          # 平台编译配置
├── build_factory.sh        # 平台打包脚本
├── platform.json           # 硬件资源配置
├── tests.json              # 测试项注册表
└── drivers/                # 平台专用 driver 实现
    ├── *.cpp
    └── *.h
```

## 新增一个平台

以 `rk3588` 为例：

### 1. 创建目录

```bash
mkdir -p platforms/rk3588/drivers
```

### 2. 编写 CMakeLists.txt

参考 `platforms/null/CMakeLists.txt` 或 `platforms/falcon/CMakeLists.txt`。

关键约定：
- `if(NOT DEFINED FW_ROOT)` 回退到 `../..`
- `if(NOT TARGET factory_core)` 守卫避免重复 add
- 显式列出 `drivers/*.cpp` 源文件（不用 `file(GLOB)`）

### 3. 实现平台类

```cpp
// platforms/rk3588/Rk3588Platform.cpp
#include "platforms/common/interface/IPlatform.h"

class Rk3588Platform : public IPlatform {
public:
    bool init(const nlohmann::json& config) override { /* ... */ }
    const char* name() const override { return "rk3588"; }
    void registerDrivers(DriverRegistry& reg) override {
        reg.bind<IEncoder>([]{ return std::make_unique<RkMppEncoderV2>(); });
        // ... 绑定平台拥有的 driver
    }
};
```

### 4. 编写 platform.json

```json
{
  "platform": "rk3588",
  "gpio": { "led": { "white": 120 } },
  "i2c": { "cam0": { "bus": 4, "addr": "0x1a" } }
}
```

### 5. 编写工具链文件

```cmake
# cmake/platforms/rk3588.cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_SYSROOT ".../sysroot")
set(CMAKE_C_COMPILER ".../aarch64-linux-gnu-gcc")
# ...
```

### 6. 编译验证

```bash
PLATFORM=rk3588 ./build.sh
```

## 平台命名约定

- 平台名 = 产品平台代号（如 `falcon`），不是芯片型号
- 同一平台可服务多个产品（功能不同但硬件相同）
- 芯片型号体现在 driver 实现文件名中（如 `RkMppEncoder.cpp`）
