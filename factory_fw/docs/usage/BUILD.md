# 构建说明

## 环境准备

### 依赖

- CMake >= 3.16
- GCC（x86 本地编译）或交叉工具链（ARM 平台编译）
- nlohmann/json（已 vendored 在 `third_party/nlohmann/`）

### 交叉编译工具链（Falcon 平台）

```bash
# 设置环境变量（指向 Buildroot 生成的 host 目录）
export FALCON_SDK=/home/gdh/falcon/Omni3576-sdk/buildroot/output/rockchip_rk3576_ipc/host
```

> **注意**：`falcon.cmake` 从 `FALCON_SDK` 环境变量读取工具链路径。sysroot 位于 `${FALCON_SDK}/aarch64-buildroot-linux-gnu/sysroot/`。

## 编译

### Null 平台（x86 本地测试）

```bash
cd factory_fw
PLATFORM=null ./build.sh
```

产物：`build/null/factory_test`（x86-64 可执行文件）

### Falcon 平台（RK3576 交叉编译）

```bash
cd factory_fw
export FALCON_SDK=/home/gdh/falcon/Omni3576-sdk/buildroot/output/rockchip_rk3576_ipc/host
PLATFORM=falcon ./build.sh
```

产物：`build/falcon/factory_test`（ARM64 可执行文件）

验证产物架构：
```bash
file build/falcon/factory_test
# 输出：ELF 64-bit LSB pie executable, ARM aarch64
```

## 构建脚本行为

`build.sh` 的执行流程：

1. 以脚本自身位置为根（`SCRIPT_DIR`），不依赖调用者 CWD
2. 检查平台目录存在性
3. 检查工具链文件存在性
4. `cmake -S <platform_dir> -B build/<platform> -DCMAKE_TOOLCHAIN_FILE=...`
5. `cmake --build build/<platform> --parallel`
6. 调用平台打包脚本 `platforms/<platform>/build_factory.sh`

## 清理构建

```bash
rm -rf build/
```

## 常见问题

### Permission denied on build/

如果 `build/` 目录被 root 创建过，当前用户可能无写权限：
```bash
sudo chown -R $(id -u):$(id -g) build/
```

### "FALCON_SDK not set"

Falcon 平台编译前必须设置 `FALCON_SDK` 环境变量。可写入 `~/.bashrc`：
```bash
echo 'export FALCON_SDK=/home/gdh/falcon/Omni3576-sdk/buildroot/output/rockchip_rk3576_ipc/host' >> ~/.bashrc
source ~/.bashrc
```
