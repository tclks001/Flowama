# 2026-08-25

## 完成

- 将展示相机固定为 `PresentationCamera`：桌面模式不再提供独立相机拖拽。程序启动时以固定展示视图的屏幕上方向一次性确定 `displayGravityWorld`，使它在画面中向下。
- 左键拖拽只改变 `ContainerPose`。容器模型矩阵旋转薄盒、粒子和局部重力箭头；模拟以 `inverse(containerOrientation) * displayGravityWorld` 得到局部重力，盒壁与粒子状态仍保持在固定的容器局部坐标中。当前仍是准静态倾斜，不含平移、角速度或其他非惯性项。
- 按实际操作观感调整左键旋转符号，使容器在画面中的旋转方向与鼠标拖拽一致。
- 将 `data/` 设为 Git 忽略的可再生运行数据目录，并停止跟踪示例姿态轨迹。桌面录制和示例轨迹生成仍使用 `data/motion-tracks/`；需要轨迹时在本机录制或重新生成。
- 整理脚本目录：日常入口保留在 `scripts/`，环境诊断与示例轨迹生成移至 `scripts/tools/`，MSVC 环境导入实现移至 `scripts/internal/`。所有当前文档和调用路径已同步。

## 验证

- `scripts/build.ps1` 成功完成增量构建。
- `scripts/verify.ps1 -Ticks 720 -MotionTrack data/motion-tracks/tilt-right.csv` 成功完成，最终粒子位置为 `(1.720, -0.020, -2.920)`。
- `scripts/capture.ps1 -Ticks 720 -CaptureEvery 120 -MotionTrack data/motion-tracks/tilt-right.csv -OutputDirectory artifacts/captures/script-layout-smoke` 成功输出 7 张 BMP；人工检查 tick 240，容器倾斜、重力箭头屏幕向下、粒子位于容器低侧。
- `scripts/tools/new-tilt-track.ps1` 成功生成临时轨迹并已清理。`scripts/tools/diagnose-environment.ps1` 能从新位置执行；它如实报告当前自动化终端未将 CMake 放入 `PATH`，而 `build.ps1` 的内部环境导入仍可完成构建。

## 下一步

- 在当前交互与姿态回放已验收的基础上，继续选择下一项可观察的静态容器或多粒子验证切片；引入流体和非惯性项前，先明确对应的视觉目标与验证判据。
