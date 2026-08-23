# 2026-08-23

## 完成

- 在新电脑恢复并验证当前 Windows 开发环境：Git、Visual Studio 2022 x64 C++ 工具链、CMake 4.4.2、Ninja 1.13.2 与仓库锁定的 vcpkg 子模块均可用。
- 处理首次 vcpkg 配置中的弱网下载问题；依赖归档与工具下载完成后，安装 fmt、GLAD、GLM、SDL3 及其注册表依赖。
- 新增 `scripts/diagnose-environment.ps1`。该脚本只读检查 Git、MSVC、CMake、Ninja、vcpkg 子模块、下载资产缓存和二进制缓存；必需环境缺失返回非零，缓存缺失只警告。
- 新增 `docs/new-machine-setup.md`，说明新电脑准备、缓存迁移及后续缓存更新工作流。
- 明确缓存职责：vcpkg 下载资产缓存与二进制缓存用于加速；Git 仓库、未来 FetchContent 依赖和大型运行时资产分别管理，不混入 vcpkg 缓存。
- 完成参考视频、薄腔悬浮液、珠光材料与实时图形学资料的可行性研究，并写入 `docs/physical-model-and-feasibility.md`。当前推荐路线是网格流体/浓度场处理细颗粒，独立粒子处理可见亮粉；不将 PBF 作为主模型。
- 新增可调整的 `docs/planning/` 工作安排。它与实际开发记录分离，包含总体阶段和实体样本到货前两周的建议，不代表完成状态或不可变更的排期。
- 确定下一个最小验证切片：三维薄盒局部坐标、固定步长、单个可见粒子与解析盒壁接触。粒子采用位置约束和低回弹/阻尼，而非 PBF 虚粒子墙；该切片暂不包含流体与颗粒相互作用。

## 关键发现

- 本次耗时主要来自 GitHub 下载链路不稳定，而非本地编译。vcpkg 依赖安装和项目本身构建在归档准备完毕后均较快完成。
- `windows-debug` preset 使用 Ninja；仅安装 Visual Studio 与 CMake 不足以配置项目。
- 新安装 CMake 或 Ninja 后，旧 PowerShell 不会自动获得新的 `PATH`，需要重新打开终端。
- 真实容器几何、宏观亮粉位置和容器运动学从一开始保持三维。未来为流体网格降维时，采用深度平均或少量深度层的 2.5D 近似；这不等同于忽略薄盒厚度。
- 宏观亮粉的盒壁接触、细颗粒浓度的无通量/最大堆积，以及流体速度的不可穿透边界须分别验证，避免将多种边界问题混入同一套 PBF 虚粒子机制。

## 验证

- `scripts/setup.ps1` 成功完成 vcpkg 安装、CMake 配置和 Ninja 构建，生成 `build/windows-debug/flowama.exe`。
- `scripts/build.ps1` 成功执行，Ninja 报告无需重新构建。
- `scripts/verify.ps1` 在 NVIDIA GeForce RTX 4050 Laptop GPU 上创建 OpenGL 4.6 上下文，并成功完成 120 个自动化渲染循环。
- 诊断脚本在完整环境下返回 `0`；指定不存在的缓存路径时仅输出警告并返回 `0`；模拟缺失 Git、CMake、Ninja 时输出对应 `[FAIL]` 并返回 `1`。

## 下一步

- 实现三维薄盒、单个测试粒子、固定步长和基于视角的右键轨迹交互；以独立场景验证重力方向与六面边界接触。
