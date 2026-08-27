# Flowama 文档入口

本目录是重新进入项目时阅读的入口，只保留对当前实现仍然真实且有用的说明；它不是开发过程或方案档案。

## 当前状态

项目目前可创建 SDL3 窗口与 OpenGL 4.6 Core 上下文，并通过 GLAD 加载 OpenGL 函数。当前最小切片绘制一个 3.6 x 0.2 x 6.0 的三维线框薄盒、重力箭头和 256 个圆形点精灵亮粉；亮粉在 120 Hz 固定步长下接受重力与容器旋转产生的非惯性作用，通过解析六面盒壁、均匀网格邻域、最小距离约束与位置级摩擦沉降、接触和堆积。固定展示视图在初始化时确定屏幕向下的显示重力；左键拖拽事件被保存为带时间戳的世界旋转增量，并按固定步时间区间以 SLERP 切分为容器姿态，再从该次真实姿态变化计算角速度和角加速度。手动、固定步验证与固定步截图使用同一模拟和渲染核心，但由不同运行驱动执行。

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
3. [`container-motion-tracks.md`](container-motion-tracks.md)：容器姿态格式、桌面录制、时间戳输入切分和自动化回放。
4. [`automation-and-capture.md`](automation-and-capture.md)：当前手动、验证和截图运行方式的边界、固定 tick 与 BMP 输出约定。
5. [`physical-model-and-feasibility.md`](physical-model-and-feasibility.md)：参考视频呈现的物理与光学现象、推荐近似、可行性和验证路线；其中模型尚未实现。
6. [`../progress/README.md`](../progress/README.md)：开发记录的使用方式；最新日记录说明最近完成的工作。
7. 源码与构建配置：`CMakeLists.txt`、`CMakePresets.json`、`scripts/setup.ps1`、`scripts/build.ps1`、`scripts/verify.ps1`、`scripts/capture.ps1` 与 `src/`。

姿态轨迹页只回答“容器状态如何记录和回放”；三维调试切片页回答“当前容器、受力、颗粒接触和调试绘制如何工作”；自动化页只回答“如何运行、验证、剖析和输出截图”。同一机制的实现说明只保留在其职责页面，避免读者必须在多个页面拼接当前行为。

## 源码入口

`src/main.cpp` 只调用应用入口。需要从源码理解当前系统时，可按职责阅读：

| 文件 | 当前职责 | 对应文档 |
| --- | --- | --- |
| `application.cpp` | SDL/OpenGL 生命周期、交互循环、固定步驱动与运行模式。 | 三维调试切片、自动化与截图输出 |
| `input_timeline.cpp` | 将带时间戳的桌面旋转增量切分到固定步。 | 容器姿态轨迹 |
| `motion_track.cpp` | 姿态轨迹 CSV 的读取和桌面录制。 | 容器姿态轨迹 |
| `container.cpp` | 容器姿态、展示相机、重力坐标转换和旋转运动学估计。 | 三维调试切片 |
| `simulation.cpp` | 旋转非惯性加速度、亮粉接触、堆积和诊断。 | 三维调试切片 |
| `debug_renderer.cpp` | OpenGL 调试绘制与 BMP 后台缓冲读回。 | 三维调试切片、自动化与截图输出 |
| `*_verification.cpp` | 输入时间线和无接触旋转参考系的当前回归检查。 | 三维调试切片 |

## 后续文档

只有在对应系统已实现且需要反复理解时，才在此建立语义化页面，例如模拟模型、运行时数据流、渲染通路或验证方法。页面应说明当前实现、如何验证和已知限制；被替代的方案及过程细节应写入开发记录或由 Git 历史保存。

## 可调整的工作安排

[`planning/`](planning/) 存放尚未实施的阶段安排和近期工作计划。它与开发记录、当前实现文档分开：其中内容是可依据学习速度、样本测量和实验结果调整的预测，不表示已经完成或确定采用。
