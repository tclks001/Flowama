# 自动验证与截图输出

## 目的与边界

当前项目有三种运行方式。它们共用容器姿态、固定展示相机、颗粒模拟和调试渲染器；差异只在时间来源、容器姿态来源及每个固定步后的行为。这样手动观感、验证状态和截图序列由同一核心产生，而不在主循环中维护多套物理逻辑。

~~~text
手动运行      真实时间 + SDL 容器操控       每帧渲染并呈现窗口，记录姿态轨迹
固定步验证    精确 tick + 可选姿态轨迹      检查状态并退出
固定步截图    精确 tick + 可选姿态轨迹      在约定 tick 渲染并写 BMP
~~~

自动化只从姿态轨迹文件读取容器状态，不模拟鼠标、键盘、窗口事件、姿态传感器或移动端输入。轨迹格式和桌面录制方式见 [`container-motion-tracks.md`](container-motion-tracks.md)。

## 命令行契约

~~~powershell
# 手动交互
.\build\windows-debug\flowama.exe

# 固定步验证
.\build\windows-debug\flowama.exe --verify --ticks 720

# 固定步截图
.\build\windows-debug\flowama.exe --capture --ticks 720 --capture-every 120 --output artifacts\captures\granular-settling

# 使用姿态轨迹的自动化回放
.\build\windows-debug\flowama.exe --capture --ticks 960 --capture-every 60 --output artifacts\captures\dry-rotation --motion-track data\motion-tracks\dry-rotation.csv
~~~

运行时仅接受本节列出的命令行组合。

## PowerShell 入口

三个脚本都假定对应 preset 已配置并已构建；它们不会调用 setup 或 build。

~~~powershell
.\scripts\build.ps1
.\scripts\verify.ps1 -Ticks 720
.\scripts\capture.ps1 -Ticks 720 -CaptureEvery 120
.\scripts\capture.ps1 -Ticks 960 -CaptureEvery 60 -MotionTrack data\motion-tracks\dry-rotation.csv
~~~

capture.ps1 默认输出到 artifacts/captures/granular-settling。可通过 -OutputDirectory 指定相对项目根目录或绝对输出目录。artifacts/ 已被 Git 忽略：截图是可重建的本地产物，不应提交。

## 固定步性能剖析

`scripts/tools/profile.ps1` 运行隐藏窗口的 `--verify --profile`，并输出进程墙钟时间与每个固定 tick 的 CPU 阶段统计：完整固定步、位置预测、盒壁约束、非空网格构建、粒子接触，以及候选对数量和单格占用。它还输出最慢的固定 tick 及其阶段构成。

~~~powershell
# 静止容器下的 720 tick 基线
.\scripts\tools\profile.ps1 -Ticks 720

# 回放一条本地录制姿态轨迹
.\scripts\tools\profile.ps1 -Ticks 4874 `
    -MotionTrack data\motion-tracks\recorded-20260825-174951.csv

# 保持完整回放，但只统计 tick 4000 至结束的堆积阶段
.\scripts\tools\profile.ps1 -Ticks 4874 -ProfileStartTick 4000 `
    -MotionTrack data\motion-tracks\recorded-20260825-174951.csv
~~~

`-ProfileStartTick` 只跳过此前 tick 的计时，不会跳过模拟：程序仍从 tick 0 回放以得到完全相同的颗粒状态。该工具只在 `--verify` 模式启用，不写截图、不读取桌面鼠标事件，也不测量呈现、交换缓冲、磁盘截图写入或 GPU 实际执行时间。因此它用于回答“固定步 CPU 模拟是否在堆积阶段变贵”，不能单独证明桌面卡顿必然来自模拟；桌面帧时间、GPU 计时和录制文件 I/O 需要在对应问题明确后另行加入。

### 截图不是录屏格式

当前 BMP 是便于学习和验收的无压缩截图格式，不是正式录屏方案。1280 x 720 的每帧为 2,764,854 bytes（含 BMP 文件头）。在当前 120 Hz 固定步长下，若 `CaptureEvery = 2`，就会以每模拟秒 60 张的频率输出约 166 MB/s（约 158 MiB/s）；720 tick 的运行会生成 361 张图，接近 1 GB。

因此，日常验证应优先捕获少量关键帧，例如 `CaptureEvery = 120`（每模拟秒一张）或 `60`（每半模拟秒一张）。短时高频 BMP 序列可以用于观察问题，但它会同时承担 GPU 读回、CPU 像素转换和大量小文件写入的成本，不要求实时完成。

未来若需要可交付的连续视频，应作为独立能力设计：使用异步像素读回和视频编码器输出 MP4 或 WebM。不要为了当前验证切片提前引入编码器、视频依赖或录屏框架。

## 固定步执行边界

共享核心操作为：`AdvanceFixedStep` 推进一个 `1 / 120 s` 模拟步，`RenderFrame` 绘制当前运行时状态，`RunFixedTicks` 为 verify 和 capture 复用精确 tick 循环。桌面模式从 SDL 接收带时间戳的旋转增量、以真实帧间隔填充时间池，并让物理时间稳定落后真实时间 4 个 tick；每个固定子步按该步时间区间切分输入后调用 `AdvanceFixedStep`。桌面容器操作会在同一固定步边界写入姿态轨迹。

验证模式在初始状态渲染一次以检查绘制路径，之后只推进固定步并检查粒子没有离开容器。截图模式在 tick 0 和每个可被 `CaptureEvery` 整除的已完成 tick 调用 `RenderFrame` 与 BMP 写入。自动化提供轨迹时，tick 0 先加载第一个姿态样本。文件职责与相应的实现说明见 [`README.md`](README.md)、[`three-dimensional-debug-slice.md`](three-dimensional-debug-slice.md) 与 [`container-motion-tracks.md`](container-motion-tracks.md)。

因此使用 Ticks = 720、CaptureEvery = 120 时会产生：

~~~text
tick_000000.bmp
tick_000120.bmp
tick_000240.bmp
tick_000360.bmp
tick_000480.bmp
tick_000600.bmp
tick_000720.bmp
~~~

tick_000000 是任何模拟步之前的初始状态；其他文件表示已经完成对应数量固定步后的状态。

## BMP 输出

截图使用不压缩的 24-bit BMP，避免为当前学习切片引入图像编码库。

读取时，OpenGL 在后台缓冲调用 glReadPixels，格式为 RGB、每像素三字节。GL_PACK_ALIGNMENT 被设为 1，避免三字节像素行受默认四字节对齐影响。

OpenGL 的像素原点在左下。BMP 使用正高度时也按下到上的行顺序存储，因此当前实现不翻转行；它只将每个像素由 RGB 转为 BMP 所需的 BGR，并补齐每行至四字节边界。这样输出可由 Windows 与常见图像工具直接打开。

## 验收范围

当前截图切片的成功条件是：

- build.ps1 成功构建。
- verify.ps1 在短程和长程固定 tick 下成功退出。
- capture.ps1 输出预期数量、命名和尺寸的 BMP 文件。
- tick 0 与后续 tick 的像素内容不同，且人工查看能确认粒子下落、图像方向正确。
- capture 不依赖真实帧率、桌面输入或可见窗口；有轨迹时只依赖指定文件的固定 tick 样本。

它尚不比较图像与金图，不输出 PNG，不录制视频，也不模拟移动端或传感器输入。
