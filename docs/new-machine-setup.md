# 新电脑环境准备与缓存迁移

本文说明 Flowama 当前 Windows 开发环境的准备方式，以及如何在网络不佳时复用已有依赖缓存。它只描述当前项目：Windows、Visual Studio、CMake、Ninja 与仓库锁定的 vcpkg 子模块。

## 先运行诊断

克隆仓库后，先在新的 PowerShell 窗口中运行：

```powershell
.\scripts\tools\diagnose-environment.ps1
```

诊断脚本只读取系统、仓库和缓存状态：不会下载、安装、初始化子模块、配置 CMake 或修改任何文件。它检查：

- Git；
- Visual Studio 的 x64 C++ 工具链；
- CMake 3.25 或更高版本；
- Ninja（`windows-debug` preset 的生成器）；
- `third_party/vcpkg` 子模块；
- vcpkg 下载资产缓存与二进制缓存。

`[FAIL]` 表示无法完成构建；`[WARN]` 只表示首次配置可能需要下载或编译更久。安装或更新工具后必须关闭旧终端并重新打开，使新的 `PATH` 生效。

可以检查外接盘或共享目录中的缓存，而不改变它们：

```powershell
.\scripts\tools\diagnose-environment.ps1 `
    -AssetCachePath E:\FlowamaCache\vcpkg-downloads `
    -BinaryCachePath E:\FlowamaCache\vcpkg-binary
```

## 必需工具

安装以下工具后重新打开 PowerShell：

- Git；
- Visual Studio 2022（或更新版本）并选择 **Desktop development with C++**；
- CMake 3.25 或更新版本；
- Ninja。

项目的 `windows-debug` preset 使用 Ninja；仅安装 Visual Studio 与 CMake 不足以配置该 preset。

## 首次配置

在网络可用且诊断中没有 `[FAIL]` 后运行：

```powershell
git submodule update --init --recursive
.\scripts\setup.ps1
.\scripts\verify.ps1
```

`setup.ps1` 会初始化 vcpkg 依赖、配置 CMake 并构建；日常源码改动通常只需：

```powershell
.\scripts\build.ps1
.\scripts\verify.ps1
```

## 缓存包含什么

缓存不属于项目源码，不能提交到 Git。

| 缓存 | 典型位置 | 节省的时间 |
| --- | --- | --- |
| 下载资产缓存 | `third_party\vcpkg\downloads` | 下载 SDL3、GLM、GLAD、注册表、Python、构建工具等归档 |
| 二进制缓存 | `%LOCALAPPDATA%\vcpkg\archives` | 重新编译 fmt、GLAD、GLM、SDL3 等 vcpkg 包 |

下载资产缓存不替代 Git 子模块：第一次仍需要得到仓库锁定的 `third_party/vcpkg` 提交。二进制缓存也只会在依赖版本、triplet、编译器和构建配置兼容时恢复；不兼容时 vcpkg 会重新构建正确的包。

## 迁移缓存

在已成功构建且网络正常的电脑上，将下列目录复制到移动硬盘、NAS 或受信任的局域网共享目录：

```text
<Flowama>\third_party\vcpkg\downloads
%LOCALAPPDATA%\vcpkg\archives
```

例如，外接盘可采用：

```text
E:\FlowamaCache\vcpkg-downloads
E:\FlowamaCache\vcpkg-binary
```

在新电脑上：

1. 安装必需工具并重新打开 PowerShell。
2. 克隆仓库，然后执行 `git submodule update --init --recursive`。
3. 将 `E:\FlowamaCache\vcpkg-downloads` 的内容复制到 `<Flowama>\third_party\vcpkg\downloads`。
4. 将 `E:\FlowamaCache\vcpkg-binary` 的内容复制到 `%LOCALAPPDATA%\vcpkg\archives`。
5. 运行诊断脚本，确认没有 `[FAIL]` 且两个缓存均被识别。
6. 运行 `scripts/setup.ps1` 与 `scripts/verify.ps1`。

复制缓存只是加速措施：其中内容可以随时由 vcpkg 根据锁定版本重新下载或重建。不要把缓存目录加入仓库、不要把它当作项目发布物，也不要用它替代 `vcpkg.json`、`builtin-baseline` 或 Git 子模块锁定提交。

## 维护缓存

仅修改 `src/`、着色器或不影响依赖的普通 CMake 逻辑时，不需要更新缓存。在一台网络正常的机器上，完成并验证下列任一变更后，再同步缓存：

- 修改 `vcpkg.json`；
- 更新 `builtin-baseline` 或 vcpkg 子模块提交；
- 新增或升级 vcpkg 依赖；
- 以干净依赖状态成功运行一次 `scripts/setup.ps1`。

使用 `robocopy` 将新增或变更内容同步到外接盘；不要移动原始缓存。下载资产目录需要排除中断下载留下的 `.part` 和 `.tmp` 文件：

```powershell
robocopy `
    "<Flowama>\third_party\vcpkg\downloads" `
    "E:\FlowamaCache\vcpkg-downloads" `
    /E /Z /R:2 /W:2 /XF *.part *.tmp

robocopy `
    "$env:LOCALAPPDATA\vcpkg\archives" `
    "E:\FlowamaCache\vcpkg-binary" `
    /E /Z /R:2 /W:2
```

`robocopy` 的退出码 `0` 到 `7` 均表示复制成功或成功但存在可预期差异；`8` 或更高表示有复制失败，应在迁移前解决。

未来新增的第三方内容应按来源分别管理：

| 内容 | 缓存位置与更新时机 |
| --- | --- |
| vcpkg 依赖 | 更新本页的下载资产缓存和二进制缓存。 |
| Git submodule 或其他 Git 仓库 | 由 `.gitmodules` 和锁定提交保证可复现；弱网时可单独维护 `E:\FlowamaCache\git-mirrors`，不要混入 vcpkg 缓存。 |
| FetchContent / ExternalProject 依赖 | 记录其锁定来源和校验方式；按其下载机制设置独立缓存，不迁移整个 `build\` 目录。 |
| 大型运行时资产 | 使用单独的 `E:\FlowamaCache\assets` 或正式资产发布渠道，不放进 vcpkg 缓存。 |

缓存是可丢弃、可再生的加速层；仓库中的 manifest、baseline、子模块提交与源码才是可复现构建的权威记录。
