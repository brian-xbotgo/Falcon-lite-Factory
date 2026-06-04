# Factory Firmware 文档

本文档目录包含产测固件平台化项目的全部文档。

## 文档地图

| 目录 | 内容 | 读者 |
|------|------|------|
| `usage/` | 使用说明：编译、配置、运行 | 固件工程师、产线运维 |
| `development/` | 开发文档：架构、接口、编码规范、**开发记录** | 平台开发者、新平台适配者 |
| `modules/` | 模块说明：每个子系统的职责和接口 | 所有开发者 |

## 快速入口

- **第一次编译？** → [`usage/BUILD.md`](usage/BUILD.md)
- **新增一个平台？** → [`development/ARCHITECTURE.md`](development/ARCHITECTURE.md) + [`usage/PLATFORM.md`](usage/PLATFORM.md)
- **新增一个测试项？** → [`modules/TESTS.md`](modules/TESTS.md)
- **接口定义在哪？** → [`modules/PLATFORMS.md`](modules/PLATFORMS.md)
- **评审或追溯历史决策？** → [`development/devlog.md`](development/devlog.md)
