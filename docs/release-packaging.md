# 发布打包说明

## 目标

这份文档描述当前仓库已经具备的最小发布打包流程。

当前阶段提供的是：

- `Release` 构建
- 可导出的安装目录
- 基于 `CPack` 的 ZIP 打包

当前阶段还不包含：

- 图形化安装器
- 自动签名
- 自动更新

## 前置条件

- Windows
- Visual Studio 2022 或 Build Tools
- Ninja
- CMake 3.22+

在运行 `cmake --preset ...`、`cmake --build --preset ...`、`cmake --install ...` 或 `cpack ...` 前，先在同一 PowerShell 会话中激活 MSVC 环境：

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
```

## 生成 Release 构建

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc-release
```

产物位置：

- `out\build\windows-msvc\src\app\Release\CaptureZY.exe`

## 导出安装目录

如需得到一个可直接分发的目录，可以执行：

```powershell
cmake --install out\build\windows-msvc --config Release --prefix out\install\CaptureZY
```

预期输出目录至少包含：

- `out\install\CaptureZY\CaptureZY.exe`
- `out\install\CaptureZY\README.md`
- `out\install\CaptureZY\LICENSE`

## 生成 ZIP 包

如需生成 ZIP 包，可以执行：

```powershell
cpack --config out\build\windows-msvc\CPackConfig.cmake -C Release
```

默认产物位置：

- `out\build\windows-msvc\CaptureZY-<version>-windows-x64.zip`

## 一键执行脚本

如果希望一次完成配置、Release 构建、安装目录导出和 ZIP 打包，可以执行：

```powershell
pwsh -File .\scripts\package_release.ps1
```

默认行为：

- 使用 `windows-msvc` 进行配置
- 使用 `windows-msvc-release` 进行构建
- 导出到 `out\install\CaptureZY`
- 生成 ZIP 到 `out\build\windows-msvc`

## 当前建议

- 开发期优先验证 ZIP 包和安装目录导出流程是否稳定。
- 在进入更正式发布前，再决定是否接入安装器、签名和版本化发布脚本。
