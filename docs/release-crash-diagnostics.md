# Release 崩溃专项诊断

## 目的

这份文档用于处理以下类型的问题：

- `Release` 崩溃，但 `Debug` 正常
- `Release` 下出现明显的数据损坏、对象状态异常或随机访问冲突
- 崩溃点落在容器、析构、回调、虚表调用等“表面位置”，怀疑真正的写坏点更早

这类问题默认按“未定义行为 / 内存破坏”优先处理，不应先假设为普通业务逻辑分支错误。

## 快速结论

遇到 `Release-only` 崩溃时，优先做 5 件事：

1. 固定复现条件，并确认 `Debug`/`Release` 差异。
2. 立即保留日志、crash report、dump、构建产物和提交号。
3. 先看 crash report，再看日志，再看 dump。
4. 如果轻微布局变化就显著改变现象，优先按内存破坏排查。
5. 一旦结论收敛，把案例补进 [docs/severe-issues.md](D:/Repo/C++/CaptureZY/docs/severe-issues.md)。

## 适用前提

- 可执行文件路径：
  - `out\build\windows-msvc\src\app\Debug\CaptureZY.exe`
  - `out\build\windows-msvc\src\app\Release\CaptureZY.exe`
- 诊断目录：
  - `%LOCALAPPDATA%\CaptureZY\Diagnostics`
- 日志目录：
  - `%LOCALAPPDATA%\CaptureZY\Logs`

## 标准流程

### 1. 先确认问题边界

至少明确以下问题：

- 是否只在 `Release` 出现
- 是否只在某一动作路径出现，例如：
  - `CopyOnly`
  - `CopyAndPin`
  - `SaveToFile`
- 是否与窗口数量、贴图数量、关闭/销毁时机有关
- 是否与对象布局、成员顺序、回调是否触发有关

如果 `Debug` 正常、`Release` 崩溃，而且轻微代码重排就能改变现象，优先按“布局敏感的未定义行为”处理。

### 2. 先收集资料，不要急着改代码

至少保留：

- 崩溃前后的日志
- 最新 `crash-*.txt`
- 最新 `crash-*.dmp`
- 对应的二进制产物
- 对应提交号

建议在复现前先清理或备份旧诊断目录，避免把新旧 crash 混在一起。

### 3. 先读 crash report

重点看：

- `exception_code`
- `exception_address`
- `session_id`
- `minidump_written`
- `log_file`
- `diagnostics_directory`

如果同一问题多次复现时 `exception_address` 非常接近或完全相同，说明故障模式可能稳定，不是纯随机噪音。

### 4. 再看日志

先执行：

```powershell
pwsh -File .\scripts\show_latest_diagnostics.ps1
```

这个脚本会：

- 输出最新 crash report 路径
- 输出最新 dump 路径
- 打印 crash report 内容
- 尝试按 `session_id` 过滤主日志

重点看崩溃前最后几条业务日志，确认：

- 崩溃发生前最后完成的动作
- 是否进入了预期分支
- 崩溃是否总是出现在同一条日志之后

### 5. 再看 dump

先执行：

```powershell
pwsh -File .\scripts\analyze_latest_dump.ps1
```

如果要额外看当前指令附近的反汇编：

```powershell
pwsh -File .\scripts\analyze_latest_dump.ps1 -ShowDisassembly
```

如果要执行更深入的 WinDbg 命令，例如 `!analyze -v`：

```powershell
pwsh -File .\scripts\analyze_latest_dump.ps1 -InitialCommand '!analyze -v'
```

第一轮只需要回答这些问题：

- 崩溃点是在做“正常写入/析构/调用”，还是已经在访问明显非法地址
- 寄存器或内存内容是否像：
  - 已释放地址
  - 空指针附近
  - 文本碎片
  - 对象内部字段残片
- 崩溃现场更像：
  - 真正的根因位置
  - 还是更早内存破坏后暴露的受害点

### 6. 什么时候启用 Page Heap

如果 dump 显示：

- 容器状态被污染
- 析构时崩
- 回调/消息流表面正常，但对象字段明显异常
- `Release` 和 `Debug` 表现差异极大

优先考虑启用 Page Heap，再复现一次。

仓库内脚本入口：

```powershell
pwsh -File .\scripts\pageheap_capturezy.ps1 -Mode query
pwsh -File .\scripts\pageheap_capturezy.ps1 -Mode enable
pwsh -File .\scripts\pageheap_capturezy.ps1 -Mode disable
```

前提：

- 这组命令通常需要在管理员 PowerShell 中执行。
- 如果 `query` / `enable` / `disable` 返回访问被拒绝，优先确认当前终端是否具备管理员权限。

对应底层命令：

```powershell
gflags /p /enable CaptureZY.exe /full
```

复现结束后记得关闭：

```powershell
gflags /p /disable CaptureZY.exe
```

注意：

- `gflags` 会修改本机对应进程的调试设置，属于系统级诊断手段。
- 启用后性能会下降，内存布局会改变。
- 复现完成后应尽快关闭，避免干扰后续普通验证。
- 建议先执行 `query`，确认当前状态，再决定是否启用。

### 7. `clang-cl` 的定位

仓库保留 `clang-cl` 作为补充诊断编译器，不替代 `MSVC` 主构建。

它的用途是：

- 在 Windows / MSVC 兼容语境下补充额外诊断能力
- 为后续 ASan 等专项构建预留入口

但当前应明确：

- 发布与日常主验证仍然以 `MSVC` 为准
- `clang-cl + ASan` 目前还不是仓库默认可直接使用的标准流程
- 如果专项构建没打通，不应阻塞先用日志、dump、page heap 收敛问题

### 8. 什么情况下可以先做隔离修复

如果已经满足以下条件，可以先做缓解/隔离修复：

- 用户可见崩溃严重
- 根因类型已较高置信度收敛到“未定义行为 / 内存破坏”
- 轻量级结构调整可以稳定消除崩溃
- 精确写坏点短期内难以直接抓到

但提交说明和问题归档必须明确区分：

- 已证实事实
- 高概率推断
- 尚未证明的部分

## 常见判断信号

### 更像普通逻辑 bug

- 崩溃地址和调用链稳定落在同一业务分支
- 关键对象状态在崩溃前后都自洽
- 改对象布局、成员顺序、堆/栈持有方式后现象基本不变

### 更像内存破坏 / 未定义行为

- `Debug` 正常，`Release` 崩溃
- 崩溃点落在 STL、析构、虚调用或消息分发中
- 寄存器值像乱码、文本碎片、异常指针
- 轻微布局变化会改变崩溃位置或是否复现
- 同一批逻辑在不同动作路径下只有少数路径崩溃

## 建议沉淀物

每次完成这类排查后，至少补齐：

- [docs/severe-issues.md](D:/Repo/C++/CaptureZY/docs/severe-issues.md)
- 必要时更新 [docs/manual-regression.md](D:/Repo/C++/CaptureZY/docs/manual-regression.md)
- 必要时更新 [docs/current-status.md](D:/Repo/C++/CaptureZY/docs/current-status.md)

## 当前仓库内可直接使用的入口

```powershell
pwsh -File .\scripts\show_latest_diagnostics.ps1
pwsh -File .\scripts\analyze_latest_dump.ps1
pwsh -File .\scripts\pageheap_capturezy.ps1 -Mode query
```

如果当前机器安装了 WinDbg / Debugging Tools for Windows，第二个脚本会优先使用 `cdb.exe`。
