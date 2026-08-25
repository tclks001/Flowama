# Flowama 文档入口

本目录是重新进入项目时阅读的入口，只保留对当前实现仍然真实且有用的说明；它不是开发过程或方案档案。

## 当前状态

项目目前可创建 SDL3 窗口与 OpenGL 4.6 Core 上下文，并通过 GLAD 加载 OpenGL 函数。当前最小切片绘制一个 3.6 x 0.2 x 6.0 的三维线框薄盒、重力箭头和 256 个圆形点精灵亮粉；亮粉在 120 Hz 固定步长下接受重力，通过解析六面盒壁、均匀网格邻域、最小距离约束与位置级摩擦沉降、接触和堆积。固定展示视图在初始化时确定屏幕向下的显示重力，容器姿态轨迹将它转换到容器局部坐标；左键拖拽操控并记录容器姿态。手动、固定步验证与固定步截图使用同一模拟和渲染核心，但由不同运行驱动执行。

## 本地入口

- `scripts/setup.ps1`：首次克隆、修改依赖或 CMake 配置后运行；初始化依赖、配置并构建。
- `scripts/build.ps1`：日常增量编译；要求对应 preset 已由 setup 配置。
- `scripts/verify.ps1`：只运行已构建的固定步验证，不会重新配置、编译或写截图。
- `scripts/capture.ps1`：只运行已构建的固定步截图，并将可重建的 BMP 文件写入 `artifacts/`。
- `scripts/tools/`：低频的环境诊断与可重建测试数据生成工具；其具体用法由对应文档说明。
- [`new-machine-setup.md`](new-machine-setup.md)：新电脑工具链准备、只读环境诊断与 vcpkg 缓存迁移。

## 推荐阅读顺序

1. [`../README.md`](../README.md)：项目目标、学习范围和最小运行方式。
2. [`three-dimensional-debug-slice.md`](three-dimensional-debug-slice.md)：当前三维薄盒、相机、粒子、OpenGL 绘制和验证数据流。
3. [`container-motion-tracks.md`](container-motion-tracks.md)：容器姿态格式、桌面录制、自动化回放和当前受力范围。
4. [`automation-and-capture.md`](automation-and-capture.md)：当前手动、验证和截图运行方式的边界、固定 tick 与 BMP 输出约定。
5. [`physical-model-and-feasibility.md`](physical-model-and-feasibility.md)：参考视频呈现的物理与光学现象、推荐近似、可行性和验证路线；其中模型尚未实现。
6. [`../progress/README.md`](../progress/README.md)：开发记录的使用方式；最新日记录说明最近完成的工作。
7. 源码与构建配置：`CMakeLists.txt`、`CMakePresets.json`、`scripts/setup.ps1`、`scripts/build.ps1`、`scripts/verify.ps1`、`scripts/capture.ps1` 与 `src/`。

## 后续文档

只有在对应系统已实现且需要反复理解时，才在此建立语义化页面，例如模拟模型、运行时数据流、渲染通路或验证方法。页面应说明当前实现、如何验证和已知限制；被替代的方案及过程细节应写入开发记录或由 Git 历史保存。

## 可调整的工作安排

[`planning/`](planning/) 存放尚未实施的阶段安排和近期工作计划。它与开发记录、当前实现文档分开：其中内容是可依据学习速度、样本测量和实验结果调整的预测，不表示已经完成或确定采用。
