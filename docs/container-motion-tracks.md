# 容器姿态轨迹

## 目的

姿态轨迹是容器的固定步运动学输入。它记录已经解析后的容器世界姿态，不记录鼠标移动、窗口像素、按键事件或相机操作。桌面端和自动化回放都使用同一组 `ContainerPose` 样本，因此回放结果不依赖录制机器的帧率、窗口尺寸或鼠标灵敏度。

当前每个样本包含位置与姿态。位置已参与渲染变换并为未来平移惯性力保留；本切片只由姿态改变局部重力，尚未从位置或姿态差分推导线加速度、角速度、角加速度或非惯性力。

## 文件位置与格式

轨迹使用 UTF-8 兼容的纯文本 CSV，位于本机的 `data/motion-tracks/`。该目录存放可再生运行数据，已由 Git 忽略；以 `#` 开头的行是注释。每个数据行有八个逗号分隔值：

~~~text
# Flowama container motion track
# tick,px,py,pz,qx,qy,qz,qw
0,0,0,0,0,0,0,1
1,0,0,0,0,0,0,1
~~~

字段约定如下：

| 字段 | 含义 |
| --- | --- |
| `tick` | 从零开始、连续递增的 120 Hz 固定模拟步边界编号。 |
| `px, py, pz` | 容器原点在世界坐标中的位置，单位为米。 |
| `qx, qy, qz, qw` | 单位四元数，表示“容器局部坐标 → 世界坐标”的旋转；文本顺序为 `x, y, z, w`。 |

默认容器姿态为位置 `(0, 0, 0)`、四元数 `(0, 0, 0, 1)`。初始相机本身采用斜向观察位置；这只影响画面构图，并不表示容器带有初始旋转。

读取器要求 tick 从 `0` 开始且没有缺口，所有数值必须有限，四元数长度必须非零；读取后会归一化四元数。自动化运行 `--ticks T` 时，轨迹必须含有 `0` 到 `T` 的全部样本。

样本 `N` 表示第 `N` 个固定步边界的容器状态：该状态用于渲染 tick `N`，并在区间 `[N, N+1)` 内作为粒子积分的容器姿态。因此轨迹首行在任何渲染和模拟之前直接接管初始容器状态，不会从默认姿态跳变到轨迹姿态。

## 当前受力与渲染

模拟仍在容器局部坐标中运行，盒壁保持轴对齐。展示相机在创建时固定；程序用它的 `ScreenUp` 一次性确定显示重力：

~~~text
displayGravityWorld = -presentationCamera.ScreenUp() * 9.81 m/s²
localGravity = inverse(containerOrientation) * displayGravityWorld
~~~

`displayGravityWorld` 在一次运行内不会变化，且在画面中始终指向屏幕下方。亮粉群使用 `localGravity` 积分；渲染时，薄盒、亮粉和局部重力箭头共同乘以容器的“平移 × 旋转”模型矩阵。于是容器在世界中倾斜，重力箭头保持屏幕向下，而亮粉在固定局部盒壁内向新的低侧滑动和堆积。

展示相机不接受桌面输入。轨迹、重力和粒子受力均不依赖鼠标观察操作。

## 桌面录制

手动运行时会自动在 `data/motion-tracks/` 创建一个以本地创建时间命名的 `recorded-YYYYMMDD-HHMMSS.csv` 文件；同一秒再次创建时附加数字后缀。记录器在每个固定步边界写入当前 `ContainerPose`，文件关闭前会补写最终边界样本。

- 左键拖拽：按固定展示视图的屏幕上、右方向旋转容器；容器在画面中的旋转方向与拖拽方向一致，它改变容器姿态和粒子受力。
- R：重置容器和粒子，并开始一条新的姿态轨迹文件。

录制文件是本地运行数据，不提交到 Git。需要某条轨迹时，可保留该文件供本机回放；需要一个标准化示例时，使用下文的生成脚本重新创建。

## 自动化回放

`verify.ps1` 与 `capture.ps1` 的 `-MotionTrack` 参数接受相对项目根目录或绝对路径。脚本只传递轨迹，不模拟桌面输入。

~~~powershell
# 由轨迹驱动的固定步状态验证
.\scripts\verify.ps1 -Ticks 720 -MotionTrack data\motion-tracks\tilt-right.csv

# 由同一轨迹驱动的关键帧截图
.\scripts\capture.ps1 -Ticks 720 -CaptureEvery 120 `
    -MotionTrack data\motion-tracks\tilt-right.csv `
    -OutputDirectory artifacts\captures\tilt-right
~~~

可执行程序的对应参数是 `--motion-track <文件>`。自动化窗口保持隐藏，不接受 SDL 鼠标、键盘或桌面姿态输入。

## 示例轨迹

`scripts/tools/new-tilt-track.ps1` 生成一个 720 tick 的示例轨迹：静止、绕世界 Y 轴平滑倾斜到 25 度、保持、平滑回正、再次静止。

~~~powershell
.\scripts\tools\new-tilt-track.ps1
~~~

默认输出 `data/motion-tracks/tilt-right.csv`。该轨迹只验证姿态文件、局部重力转换、渲染模型矩阵和固定步回放链路；它不模拟猛晃、流体涡流或惯性效应。
