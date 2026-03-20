# 开发环境

## 支持的工作方式

- 可以在 WSL 中编辑代码与做工程级校验
- 应在 Windows 中完成配置、编译与调试

## 工具链基线

- Windows 10 或 Windows 11
- CMake 3.22+
- Ninja
- Visual Studio 2022 Build Tools 或 Visual Studio 2022
- `cl.exe` 或 `clang-cl.exe`

## 推荐流程

1. 在 Windows 中打开仓库。
2. 启动 Visual Studio Developer PowerShell。
3. 选择一个预设进行配置：

```powershell
cmake --preset windows-msvc
```

4. 执行构建：

```powershell
cmake --build --preset windows-msvc-debug
```

## 编译器建议

- 默认开发与发布编译器：`MSVC`
- 辅助校验编译器：`clang-cl`
- `clang-tidy` 先作为分析工具使用，不作为早期主构建门禁

## clang-format 与 clang-tidy

### `clang-format`

检查单个文件是否符合格式：

```bash
clang-format --dry-run --Werror src/app/main.cpp
```

直接格式化文件：

```bash
clang-format -i src/app/main.cpp
```

### `clang-tidy`

项目已经提供 Windows PowerShell 脚本，通常不需要手工逐文件运行。

全量脚本会优先使用系统环境中的 `run-clang-tidy.py` 或 `run-clang-tidy`；如果都找不到，则回退到仓库根目录下的 `run-clang-tidy.py`。建议优先使用 LLVM 安装自带版本，本地复制到仓库根目录的文件仅作为兜底，不纳入版本控制。

检查整个 `src/`：

```pwsh
pwsh -ExecutionPolicy Bypass -File scripts/run_clang_tidy.ps1
```

只检查某个路径正则：

```pwsh
pwsh -ExecutionPolicy Bypass -File scripts/run_clang_tidy.ps1 -PathRegex '.*[\\/]src[\\/]app[\\/].*'
```

只检查相对 `HEAD` 改动过的文件：

```pwsh
pwsh -ExecutionPolicy Bypass -File scripts/run_clang_tidy_changed.ps1
```

如果要对比某个分支或远端引用：

```pwsh
pwsh -ExecutionPolicy Bypass -File scripts/run_clang_tidy_changed.ps1 -BaseRef origin/master
```

`run-clang-tidy.py` 的位置参数匹配的是 `compile_commands.json` 中的完整文件路径。当前构建目录中的路径是 `D:\Repos\CaptureZY\...` 这种 Windows 绝对路径，因此像 `"src/.*"` 这样的正则不会命中，应该使用 `.*[\\/]src[\\/].*` 这种写法。

建议日常开发时只跑改动文件，阶段性再跑一次全量检查。

## Release 崩溃专项诊断

如果问题满足以下任一条件：

- `Release` 崩溃但 `Debug` 正常
- 崩溃点落在 STL、析构、回调、消息分发等表面位置
- 怀疑是对象布局敏感或优化敏感的问题

优先阅读：

- [docs/release-crash-diagnostics.md](D:/Repo/C++/CaptureZY/docs/release-crash-diagnostics.md)

仓库内可直接使用的诊断入口：

```powershell
pwsh -File .\scripts\show_latest_diagnostics.ps1
pwsh -File .\scripts\analyze_latest_dump.ps1
pwsh -File .\scripts\pageheap_capturezy.ps1 -Mode query
```

其中 `pageheap_capturezy.ps1` 通常需要管理员 PowerShell。

## 当前限制

- 不以 Linux 构建为目标
- 当前骨架只验证仓库布局与 Win32 入口
- 等截图 API 接入后，再补充 Windows SDK 版本要求

## 日志

### 日志文件位置

- 默认日志目录：
  `%LOCALAPPDATA%\CaptureZY\Logs`
- 当前主日志文件：
  `%LOCALAPPDATA%\CaptureZY\Logs\capturezy.log`
- 当前轮转备份文件：
  `%LOCALAPPDATA%\CaptureZY\Logs\capturezy.log.1`
  `%LOCALAPPDATA%\CaptureZY\Logs\capturezy.log.2`
  `%LOCALAPPDATA%\CaptureZY\Logs\capturezy.log.3`

### 默认行为

- `Debug` 构建默认最小级别为 `debug`
- `Release` 构建默认最小级别为 `info`
- `Debug` 构建默认同时输出到调试器和文件
- `Release` 构建默认关闭调试器输出，仅保留文件输出
- 当前日志文件达到阈值后，会按顺序轮转到 `.1/.2/.3`
- 当前已覆盖应用启动、设置、托盘、截图、剪贴板、文件保存和贴图关键路径
- 当前每条日志都会带 `session_id`、`process_id` 和 `thread_id`
- 当前 crash report 会写入对应 `session_id` 和运行中的日志文件路径，便于关联排障

### 运行时环境变量

- `CAPTUREZY_LOG_LEVEL`
  - 可用值：`error` `warning` `warn` `info` `debug` `trace`
- `CAPTUREZY_LOG_DEBUGGER`
  - 可用值：`true/false` `on/off` `yes/no` `1/0`
- `CAPTUREZY_LOG_FILE`
  - 可用值：`true/false` `on/off` `yes/no` `1/0`
- `CAPTUREZY_LOG_MAX_KB`
  - 例如：`256` `512` `1024`
- `CAPTUREZY_LOG_MAX_FILES`
  - 表示保留的轮转备份份数，例如：`0` `1` `3`

### 常用示例

在当前 PowerShell 会话中临时把日志级别调到 `trace`：

```powershell
$env:CAPTUREZY_LOG_LEVEL = 'trace'
```

只保留文件日志，关闭调试器输出：

```powershell
$env:CAPTUREZY_LOG_DEBUGGER = 'false'
```

把日志文件大小阈值调到 256 KB：

```powershell
$env:CAPTUREZY_LOG_MAX_KB = '256'
```
