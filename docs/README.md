# Flowama 文档入口

本目录是重新进入项目时阅读的入口，只保留对当前实现仍然真实且有用的说明；它不是开发过程或方案档案。

## 当前状态

项目目前可创建 SDL3 窗口与 OpenGL 4.6 Core 上下文，并通过 GLAD 加载 OpenGL 函数。程序会输出实际 OpenGL 版本与渲染器信息并持续清屏；`scripts/verify.ps1` 使用隐藏窗口和关闭垂直同步的自动化模式，运行 120 次渲染循环后自动退出。它暂时不是物理固定步长；着色器、模拟与正式渲染通路尚未实现。

## 本地入口

- `scripts/setup.ps1`：首次克隆、修改依赖或 CMake 配置后运行；初始化依赖、配置并构建。
- `scripts/build.ps1`：日常增量编译；要求对应 preset 已由 setup 配置。
- `scripts/verify.ps1`：只运行已构建的自动化场景，不会重新配置或编译。
- [`new-machine-setup.md`](new-machine-setup.md)：新电脑工具链准备、只读环境诊断与 vcpkg 缓存迁移。

## 推荐阅读顺序

1. [`../README.md`](../README.md)：项目目标、学习范围和最小运行方式。
2. [`../progress/README.md`](../progress/README.md)：开发记录的使用方式；最新日记录说明最近完成的工作。
3. 源码与构建配置：`CMakeLists.txt`、`CMakePresets.json`、`scripts/setup.ps1`、`scripts/build.ps1`、`scripts/verify.ps1` 与 `src/`。

## 后续文档

只有在对应系统已实现且需要反复理解时，才在此建立语义化页面，例如模拟模型、运行时数据流、渲染通路或验证方法。页面应说明当前实现、如何验证和已知限制；被替代的方案及过程细节应写入开发记录或由 Git 历史保存。
