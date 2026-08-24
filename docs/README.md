# Flowama 文档入口

本目录是重新进入项目时阅读的入口，只保留对当前实现仍然真实且有用的说明；它不是开发过程或方案档案。

## 当前状态

项目目前可创建 SDL3 窗口与 OpenGL 4.6 Core 上下文，并通过 GLAD 加载 OpenGL 函数。当前最小切片绘制一个 3.6 x 0.2 x 6.0 的三维线框薄盒、重力箭头和一个圆形点精灵粒子；粒子以 120 Hz 固定步长接受阻尼重力，并通过解析六面边界约束留在盒内。右键拖拽绕盒体旋转视角，`R` 重置视角与粒子；本切片特意令重力始终指向当前屏幕下方，尚不是容器运动与非惯性力的真实模型。`scripts/verify.ps1` 使用隐藏窗口和关闭垂直同步的自动化模式，运行指定数量的固定模拟步后退出。

## 本地入口

- `scripts/setup.ps1`：首次克隆、修改依赖或 CMake 配置后运行；初始化依赖、配置并构建。
- `scripts/build.ps1`：日常增量编译；要求对应 preset 已由 setup 配置。
- `scripts/verify.ps1`：只运行已构建的自动化场景，不会重新配置或编译。
- [`new-machine-setup.md`](new-machine-setup.md)：新电脑工具链准备、只读环境诊断与 vcpkg 缓存迁移。

## 推荐阅读顺序

1. [`../README.md`](../README.md)：项目目标、学习范围和最小运行方式。
2. [`three-dimensional-debug-slice.md`](three-dimensional-debug-slice.md)：当前三维薄盒、相机、粒子、OpenGL 绘制和验证数据流。
3. [`physical-model-and-feasibility.md`](physical-model-and-feasibility.md)：参考视频呈现的物理与光学现象、推荐近似、可行性和验证路线；其中模型尚未实现。
4. [`../progress/README.md`](../progress/README.md)：开发记录的使用方式；最新日记录说明最近完成的工作。
5. 源码与构建配置：`CMakeLists.txt`、`CMakePresets.json`、`scripts/setup.ps1`、`scripts/build.ps1`、`scripts/verify.ps1` 与 `src/`。

## 后续文档

只有在对应系统已实现且需要反复理解时，才在此建立语义化页面，例如模拟模型、运行时数据流、渲染通路或验证方法。页面应说明当前实现、如何验证和已知限制；被替代的方案及过程细节应写入开发记录或由 Git 历史保存。

## 可调整的工作安排

[`planning/`](planning/) 存放尚未实施的阶段安排和近期工作计划。它与开发记录、当前实现文档分开：其中内容是可依据学习速度、样本测量和实验结果调整的预测，不表示已经完成或确定采用。
