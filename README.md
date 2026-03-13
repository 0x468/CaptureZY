# CaptureZY

CaptureZY 是一个面向 Windows 的原生截图与贴图工具。

项目当前明确以 Windows 桌面集成为核心目标：

- 快速区域截图与全屏截图
- 将截图固定到桌面并保持流畅交互
- 全局热键、托盘与剪贴板工作流
- 高 DPI 与多显示器支持

## 当前状态

仓库目前包含第一版工程骨架：

- 基于 CMake 的构建配置
- 最小可运行的 Win32 应用壳层与托盘宿主
- 全局热键与截图入口占位流程
- 全屏截图覆盖层原型
- 区域截图、全屏截图、截图后保存、贴图窗口与 PNG 保存的基础流程
- 基础格式化与静态检查配置
- 首轮产品、架构与路线规划文档

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

1. 在真实 DPI / 多显示器环境执行首版手工回归清单。
2. 继续观察大尺寸贴图缩放与渲染稳定性。
3. 继续补齐安装器、签名和发布自动化。
4. 为后续正式发布准备交付与回归收敛方案。

## 仓库结构

```text
cmake/              CMake 辅助模块
docs/               产品、架构、路线图与开发文档
src/core/           平台无关的应用元信息与基础类型
src/platform_win/   Win32 窗口与平台接入
src/render_d2d/     Direct2D 渲染层占位
src/app/            进程入口与应用组装
```

开始实现功能前，建议先阅读 [docs/product.md](/home/gwf/Projects/captureZY/docs/product.md)、[docs/architecture.md](/home/gwf/Projects/captureZY/docs/architecture.md) 和 [docs/decisions.md](/home/gwf/Projects/captureZY/docs/decisions.md)。

如果需要了解项目当前已完成能力与下一阶段建议，可继续阅读 [docs/current-status.md](docs/current-status.md)。

如果需要执行当前的手工回归基线，可继续阅读 [docs/manual-regression.md](docs/manual-regression.md)。

如果需要生成当前阶段的 Release 目录或 ZIP 包，可继续阅读 [docs/release-packaging.md](docs/release-packaging.md)。
