# CaptureZY

CaptureZY 是一个面向 Windows 的原生截图与贴图工具。

项目当前明确以 Windows 桌面集成为核心目标：

- 快速区域截图与全屏截图
- 将截图固定到桌面并保持流畅交互
- 全局热键、托盘与剪贴板工作流
- 高 DPI 与多显示器支持

## 当前状态

仓库当前已经完成截图与贴图主链路，不再只是工程骨架：

- 基于 CMake 的构建配置
- 纯托盘驻留的 Win32 应用壳层
- 全局热键、托盘菜单与默认截图动作分发
- 冻结背景式截图覆盖层
- 区域截图、全屏截图与窗口级预框选截图
- 剪贴板复制、PNG 保存与截图后贴图主路径
- 贴图窗口的移动、缩放、置顶、隐藏、关闭、复制与保存
- 图形化设置对话框与 JSON 配置持久化
- 基础崩溃诊断、手工回归清单与 ZIP 打包入口
- 基础格式化与静态检查配置
- 持续更新中的产品、架构、路线图与状态文档

当前阶段更接近“Beta 打磨起步”，重点已经从单点能力补齐转向智能选区边界、交互收敛和代码组织控制。

## 构建方式

建议在 Windows 环境中构建。仓库可以在 WSL 中编辑和做工程级校验，但可执行程序应在 Windows 中配置、编译和调试。

推荐在 Visual Studio Developer PowerShell，或已将 MSVC / `clang-cl` 加入 `PATH` 的终端中执行：

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc-debug
```

如果需要使用 `clang-cl`：

```powershell
cmake --preset windows-clang-cl
cmake --build --preset windows-clang-cl-debug
```

## 近期目标

1. 继续收敛智能选区的窗口可见边界与任务栏 / 系统壳窗口识别。
2. 继续打磨贴图交互反馈与截图设置页。
3. 继续控制 `platform_win` 调度层复杂度，避免主流程继续膨胀。
4. 持续观察大尺寸贴图缩放与渲染稳定性，但暂不优先投入发布包装工作。

## 仓库结构

```text
cmake/              CMake 辅助模块
docs/               产品、架构、路线图与开发文档
src/core/           平台无关的应用元信息与基础类型
src/feature_capture/截图、覆盖层与保存流程
src/feature_pin/    贴图窗口与贴图管理
src/platform_win/   Win32 窗口与平台接入
src/render_d2d/     Direct2D 渲染层占位
src/app/            进程入口与应用组装
```

开始实现功能前，建议先阅读 [docs/product.md](docs/product.md)、[docs/architecture.md](docs/architecture.md) 和 [docs/decisions.md](docs/decisions.md)。

如果需要了解项目当前已完成能力与下一阶段建议，可继续阅读 [docs/current-status.md](docs/current-status.md)。

如果需要执行当前的手工回归基线，可继续阅读 [docs/manual-regression.md](docs/manual-regression.md)。

如果需要处理只在 `Release` 出现的 crash / 内存破坏类问题，可继续阅读 [docs/release-crash-diagnostics.md](docs/release-crash-diagnostics.md)。

如果需要生成当前阶段的 Release 目录或 ZIP 包，可继续阅读 [docs/release-packaging.md](docs/release-packaging.md)。

仓库也提供了一键打包脚本：

```powershell
pwsh -File .\scripts\package_release.ps1
```
