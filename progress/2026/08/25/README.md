# 2026-08-25

## 完成

- 将展示相机固定为 `PresentationCamera`：桌面模式不再提供独立相机拖拽。程序启动时以固定展示视图的屏幕上方向一次性确定 `displayGravityWorld`，使它在画面中向下。
- 左键拖拽只改变 `ContainerPose`。容器模型矩阵旋转薄盒、粒子和局部重力箭头；模拟以 `inverse(containerOrientation) * displayGravityWorld` 得到局部重力，盒壁与粒子状态仍保持在固定的容器局部坐标中。当前仍是准静态倾斜，不含平移、角速度或其他非惯性项。
- 按实际操作观感调整左键旋转符号，使容器在画面中的旋转方向与鼠标拖拽一致。
- 将 `data/` 设为 Git 忽略的可再生运行数据目录，并停止跟踪示例姿态轨迹。桌面录制和示例轨迹生成仍使用 `data/motion-tracks/`；需要轨迹时在本机录制或重新生成。
- 整理脚本目录：日常入口保留在 `scripts/`，环境诊断与示例轨迹生成移至 `scripts/tools/`，MSVC 环境导入实现移至 `scripts/internal/`。所有当前文档和调用路径已同步。
- 将单文件源码按当前职责拆分到 `src/` 顶层：`main.cpp` 仅保留入口；应用生命周期与自动化、容器姿态、姿态轨迹、颗粒模拟和 OpenGL/BMP 调试渲染各自独立，CMake 显式列出这些源文件。
- 实现 256 个等半径亮粉的确定性沉降与基础堆积：无重叠晶格加确定性微扰初始化；解析六面盒壁；固定均匀网格宽相；最小中心距离约束；位置级粒子/墙面摩擦；每个 120 Hz tick 使用 12 轮固定顺序位置投影并由最终位置重建速度。
- 将亮粉调试点精灵从 9 px 调整到 3.5 px，使当前相机下的离散颗粒和堆积轮廓可见；物理碰撞半径保持不变。
- 宽相遍历只检查排序后的非空网格及其邻域，不再扫描整个容器的空网格；仍以固定顺序产生候选对，保持当前 CPU 实验的重复性。

## 验证

- `scripts/build.ps1` 成功完成增量构建。
- `scripts/verify.ps1 -Ticks 720 -MotionTrack data/motion-tracks/tilt-right.csv` 成功完成，最终粒子位置为 `(1.720, -0.020, -2.920)`。
- `scripts/capture.ps1 -Ticks 720 -CaptureEvery 120 -MotionTrack data/motion-tracks/tilt-right.csv -OutputDirectory artifacts/captures/script-layout-smoke` 成功输出 7 张 BMP；人工检查 tick 240，容器倾斜、重力箭头屏幕向下、粒子位于容器低侧。
- `scripts/tools/new-tilt-track.ps1` 成功生成临时轨迹并已清理。`scripts/tools/diagnose-environment.ps1` 能从新位置执行；它如实报告当前自动化终端未将 CMake 放入 `PATH`，而 `build.ps1` 的内部环境导入仍可完成构建。
- `scripts/verify.ps1 -Ticks 120` 与 `-Ticks 720` 在 256 颗粒版本成功完成；720 tick 末轮诊断为最大格子占用 2、候选对 1373、接触对 309、最大残余穿透 0.000840。
- 静止与倾斜轨迹的 `capture.ps1 -Ticks 720 -CaptureEvery 120` 均成功输出 7 张 BMP。人工查看静止终帧：亮粉沉降至当前低侧并呈离散堆积；人工查看倾斜轨迹 tick 480：倾斜薄盒、屏幕向下箭头和亮粉低侧一致。
- 对相同的静止 720 tick 输入、最终 3.5 px 点精灵重复截图，终帧 SHA-256 一致：`37BB8A93A4FDD9518EA520C46CA858EBADC8C47456CAC7FB445DDCAE354F2050`。

## 已知问题

- 项目所有者在桌面模式运行 `data/motion-tracks/recorded-20260825-174951.csv` 时观察到轻微、可感知的卡顿；记录覆盖 tick 0 至 4874，卡顿主要出现在亮粉向角落密集堆积后。当前尚无按该输入采集的分阶段耗时、帧时间或接触工作量证据，不能据此断定是接触求解、网格宽相、渲染、桌面轨迹写入或 Debug 构建造成。下一切片先增加自动化性能剖析，不在缺乏数据时调整算法或参数。

## 下一步

- 使用该桌面录制轨迹建立自动化性能检查，分离固定步、预测、盒壁、网格构建、粒子接触与渲染/进程总耗时；根据结果决定是否需要独立性能修复切片，再进入黏性载液、非惯性项、多粒径或闪烁。
