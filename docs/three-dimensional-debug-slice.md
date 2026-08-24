# 三维容器调试切片

## 目的与范围

当前程序是后续流麻模拟的最小三维验证：确认容器坐标、相机观察、固定步长、单粒子重力运动、六面边界约束和最小 OpenGL 绘制能够共同工作。

它不是流体模拟，也不是正式摆件渲染。当前容器姿态会将固定世界重力转换为容器局部重力；它能表达静止或缓慢倾斜时的下落方向，尚不包含容器加速度和旋转造成的非惯性力。

## 运行与操作

~~~powershell
.\scripts\build.ps1
.\build\windows-debug\flowama.exe
~~~

- 左键拖拽：按当前屏幕上、右方向旋转容器，并在固定步边界记录容器姿态。
- 右键拖拽：绕盒心旋转视角；向上或向下拖动控制俯仰，左右拖动控制水平旋转。
- R：重置视角、容器和粒子，并开始一条新的姿态记录。
- scripts/verify.ps1 -Ticks 120：隐藏窗口运行 120 个固定模拟步。
- scripts/capture.ps1：以同一初始状态输出固定 tick 的 BMP 截图；运行方式见 automation-and-capture.md。

## 运行时数据流

~~~text
SDL 事件
  -> ContainerPose（左键容器旋转） / OrbitCamera（右键观察）
  -> 姿态记录或自动化姿态轨迹
  -> 固定世界重力转换为容器局部重力
  -> ParticleSimulation（120 Hz 固定步长）
  -> DebugRenderer（容器模型矩阵、线框、重力箭头、点精灵）
~~~

## 坐标与相机

容器局部坐标从一开始就是三维：

~~~text
x：横向
y：厚度／前后方向
z：竖向
~~~

薄盒全尺寸为 3.6 x 0.2 x 6.0。相机始终以盒心为观察目标，保存相对盒心的位置 position_ 和上方参考 upHint_。实际相机基向量由下式推导：

~~~text
forward  = normalize(-position)
right    = normalize(cross(forward, upHint))
screenUp = normalize(cross(right, forward))
~~~

upHint 不是屏幕上方本身；它可能含有沿 forward 的分量。推导 screenUp 相当于从 upHint 中去掉该分量，得到与相机前向正交的真实屏幕向上方向。

拖拽以当前局部轴作增量轴角旋转：水平拖拽绕 screenUp，垂直拖拽绕 right。相机位置与上方参考同时旋转，因此不使用固定世界轴的 Euler yaw/pitch，也没有传统俯仰极点。当前保护条件检查前向与上方参考是否近似平行；由于两者在每次旋转中一起旋转，其夹角在本切片中基本保持不变，保护通常不会触发。

当前世界重力与局部重力为：

~~~text
worldGravity = (0, 0, -9.81)
localGravity = inverse(containerOrientation) * worldGravity
~~~

相机只控制观察方向，不参与受力。渲染把局部线框、粒子和重力箭头共同乘以容器模型矩阵，因此倾斜后的容器仍显示世界向下的重力箭头。当前没有由平移、角速度或角加速度导出的非惯性项。

## 单粒子与盒壁

粒子状态为三维位置、三维速度和半径。每个固定步按半隐式 Euler 更新：

~~~text
velocity += gravity * dt
velocity *= exp(-fluidDragRate * dt)
position += velocity * dt
~~~

第二项是尚未有流体速度场时的均匀阻尼代理。

随后依次检查 x、y、z 的上下界。粒子中心只能到达 halfExtents - particleRadius；穿过边界时，将中心投回边界，消除朝墙法线的入射速度，并对切向速度施加少量阻尼。它是接触约束/冲量近似，而非高刚度弹簧或 PBF 虚粒子墙。

## OpenGL 绘制

渲染器只使用一个顶点 Shader 和一个片元 Shader。

- 线框：CPU 构造薄盒八个角点和十二条边，使用 GL_LINES 绘制。
- 重力箭头：作为额外的三条线段，与线框使用同一路径。
- 粒子：只提交一个 GL_POINTS 顶点。Vertex Shader 写入 gl_PointSize，Fragment Shader 通过 gl_PointCoord 丢弃圆外片元，得到圆形点精灵。
- 坐标变换：几何和粒子保存在容器局部坐标中；渲染使用容器的平移 × 旋转 Model 矩阵，顶点 Shader 接收 Projection * View * Model * localPosition。
- 深度：开启深度测试，使线框与点精灵按相机深度遮挡。

当前每次绘制都把少量调试顶点上传到同一个动态 VBO。该实现便于阅读；大量亮粉渲染会采用不同的数据布局与更新方式。

## 固定步长与确定性运行

交互模式将真实帧间隔累积到时间池中，并在每个渲染帧执行零到多个 1 / 120 s 模拟步。单帧间隔被限制为最多 0.25 s，且每帧最多运行 32 个子步，以避免卡顿后的无限追赶。

验证和截图模式不读取真实时间：每次循环恰好执行一个固定模拟步。因此 `--verify --ticks N` 或 `--capture --ticks N` 表示确定的 N 个模拟步，而不是 N 帧显示帧。

## 已知限制

- 只有一个粒子；没有粒子—粒子接触、流体速度场、浓度场、沉积或再悬浮。
- 鼠标是逻辑拖拽状态，尚未启用系统级鼠标捕获。
- 没有容器表面、透明度、折射、珠光、阴影或正式 UI。
- 当前姿态轨迹只改变局部重力和渲染变换，不能用于验证平移惯性、角加速度、科里奥利力或移动端输入。

## 验证

- scripts/build.ps1 应成功构建。
- scripts/verify.ps1 -Ticks 120 与 -Ticks 720 应成功完成，并输出处于薄盒允许范围内的粒子位置。
- 人工运行时，右键任意方向拖拽不改变粒子受力；左键拖拽容器后，盒体应倾斜、重力箭头保持世界向下、粒子应在六个面约束内向新的低侧下落或滑动。姿态轨迹的详细契约见 container-motion-tracks.md。
