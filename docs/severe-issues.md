# Severe Issues Ledger

本文件用于沉淀高严重度问题，重点记录以下内容：

- 现象与影响范围
- 复现条件
- 已掌握证据
- 根因结论与置信度
- 修复/缓解手段
- 验证结果
- 后续排查打法

后续凡是满足以下任一条件的问题，都应补充到这里：

- `Release` 崩溃、数据损坏、严重卡死
- 仅在特定编译器/优化级别出现的行为差异
- 需要 dump、日志、诊断开关或专项构建才能定位的问题
- 虽已缓解，但精确写坏点尚未完全证实的内存/生命周期问题

## 2026-03-20: Release 截图确认后在 CopyAndPin 路径崩溃

### 现象

- 仅 `Release` 版本出现。
- 截图确认后，如果走 `CopyAndPin` 路径，程序会在确认后立即崩溃。
- `Debug` 版本未复现同类崩溃。
- `SaveToFile` 和 `CopyOnly` 路径在同一阶段表现正常。

### 影响范围

- 影响截图主流程，属于阻断发布级问题。
- 触发条件集中在“截图确认 -> 写入剪贴板 -> 创建贴图窗口”这一链路。

### 已验证事实

1. 旧 crash report 在 2026-03-20 多次记录到相同异常地址：`0x...B095`，异常码均为 `0xC0000005`。
2. 旧日志显示崩溃最初发生在 `Copied capture bitmap to clipboard.` 之后。
3. 在给 `PinManager` 增加重入保护后，崩溃位置后移到 `Pin window created and shown.` 之后，但仍然崩溃。
4. 说明简单的贴图集合重入并不是根因，最多只改变了崩溃显现位置。
5. 将 `Application::main_window_`、`MainWindow::capture_overlay_`、`MainWindow::pin_manager_` 改为独立堆对象后，`Release` 版本恢复稳定。
6. 2026-03-20 用户对修复后的版本连续执行 26 次截图并贴图，并穿插 `save` / `copyOnly`，未再出现崩溃。
7. 2026-03-21 对旧版本 dump 的补充反汇编显示，`0x...B095` 直接落在 `PinManager::pin_windows_` 的 vector 尾指针写入快路径上。
8. 同批旧 dump 中另一组崩溃地址 `0x...B714` 也落在同一个 vector 的遍历/整理逻辑上，说明不同崩溃表象指向同一受害对象。
9. 2026-03-21 对旧 worktree 的启动期、全屏 `CopyAndPin`、overlay 自动完成、overlay 内部交互模拟探针均表明：`pin_windows_` 在进入 `CreatePin` 前保持干净，只在成功 `push_back` 后变为正常的非空 vector 状态。
10. `0x...B095` 对应会话日志显示，崩溃发生在新进程第一次贴图链路中：日志停在 `Pin window created and shown.`，未进入 `Created pin window. open_count=1.`，说明不是前几次截图累积的旧 vector 状态残留。

### 关键证据

#### Dump 证据

对 `C:\Users\GWF\AppData\Local\CaptureZY\Diagnostics\crash-20260320-231952-p20268-t22280.dmp` 与
`C:\Users\GWF\AppData\Local\CaptureZY\Diagnostics\crash-20260320-232100-p40788-t10356.dmp` 的现场分析显示：

- 异常指令：`CaptureZY+0x2b095`
- 指令内容：`mov qword ptr [rdx], r14`
- 当时 `rdx=0074007000610043`

这个 `rdx` 值明显不是合法堆地址，更像 UTF-16 文本碎片：

- `0x0043 = 'C'`
- `0x0061 = 'a'`
- `0x0070 = 'p'`
- `0x0074 = 't'`

也就是接近 `"Capt"`。

同一段反汇编还显示：

- `rdi+0x50 / rdi+0x58 / rdi+0x60` 构成一组三元指针字段
- 先比较 `[rdi+0x58]` 与 `[rdi+0x60]`
- 若未满，则直接执行 `mov qword ptr [rdx], r14` 再把 `[rdi+0x58] += 8`

这正是 `std::vector<std::unique_ptr<PinWindow>>` 的尾插入快路径，对应 `PinManager::pin_windows_.push_back(...)` 的机器码形态。等价于：

- vector 本身正在执行正常的 `push_back/emplace` 写入
- 但 vector 的尾指针已经在此之前被破坏
- 因而写入落到了一个明显非法、且疑似被文本内容污染过的地址

这说明故障类型是“对象状态先被破坏，再在正常容器写入时崩溃”，而不是 `PinManager` 容器算法本身出错。

对 `C:\Users\GWF\AppData\Local\CaptureZY\Diagnostics\crash-20260320-230009-p39212-t40656.dmp` 等 `0x...B714` 样本的补充分析还显示：

- 反汇编在 `rcx+0x50 / +0x58` 上读取同一组 vector 指针
- 随后按 `8-byte` 步长遍历元素并检查/交换指针

这与同一个 `pin_windows_` vector 在后续整理、遍历或清理阶段暴露损坏完全一致。也就是说：

- `0x...B095` 是“写入已损坏尾指针”时暴露
- `0x...B714` 是“遍历已损坏向量状态”时暴露
- 二者都不是首个写坏点，而是同一受害对象在不同时机暴露症状

#### 布局/隔离证据

在崩溃版本中，`MainWindow` 原本以内联成员方式持有：

- `feature_capture::CaptureOverlay capture_overlay_`
- `feature_pin::PinManager pin_manager_`

两者在 `MainWindow` 对象内相邻存放。修复中将它们改为：

- `std::unique_ptr<feature_capture::CaptureOverlay>`
- `std::unique_ptr<feature_pin::PinManager>`

改动后并没有改变 `CopyAndPin` 的业务语义，但显著改变了对象内存布局与相邻关系。随后：

- 旧 crash 不再出现
- 相同用户操作压测通过

这说明问题具有明显的“布局敏感”特征，符合典型未定义行为 / 相邻对象内存破坏特征。

#### 探针证据

2026-03-21 在旧 worktree 上增加了只用于诊断的 vector 原始三字探针。结果显示：

- 启动期：`after_callback / after_tray_icon / after_hotkey` 全为 `0,0,0`
- 全屏 `CopyAndPin` 路径：直到 `after_clipboard_copy_and_pin` 仍为 `0,0,0`
- region overlay 自动完成路径：`before_overlay_show / after_overlay_show / handle_overlay_result` 仍为 `0,0,0`
- region overlay 内部交互模拟路径：同样在进入 `CreatePin` 前保持 `0,0,0`
- 把探针推进到 `PinManager::CreatePin` 内部后，`after_prune / after_make_unique / after_set_callback / after_pin_window_create` 仍为 `0,0,0`
- overlay 工具条“确定”按钮路径与“CopyAndPin”按钮路径的内部模拟也都保持同样结果
- 仅在 `after_create_pin` 之后，vector 才变为正常的非空 begin/end/capacity 指针

这说明当前能稳定自动化驱动的旧路径里，`pin_windows_` 在进入 `CreatePin` 前并没有被立即打坏。换言之：

- 直接暴露崩溃的位置已经足够明确
- 但首个写坏动作仍然没有被稳定探针直接抓住
- 真正的写坏点仍可能依赖更具体的桌面环境、消息时序或当时的真实运行布局

#### 日志时序证据

`0x...B095` 对应的 `20260320-231949891-p20268` 与 `20260320-232057279-p40788` 两个会话日志都表现为：

- 新进程启动后第一次 `Begin capture request`
- 随后出现 `Copied capture bitmap to clipboard.`
- 接着出现 `Pin window created and shown.`
- 但没有出现 `Created pin window. open_count=1.`

这进一步说明：

- 崩溃不是发生在已有多个 pin 的累积状态上
- 也不是 `push_back` 成功后后续遍历才第一次暴露
- 而是首个 pin 的创建链路在 `PinWindow::Create` 结束后、`pin_windows_.push_back(...)` 完成前后就已经暴露异常

### 根因结论

当前结论是：

- **高置信度根因**：`Release` 下存在布局敏感的内存破坏，直接破坏了 `PinManager` 内部 `pin_windows_` vector 的 begin/end/capacity 状态；`0x...B095` 与 `0x...B714` 两组 dump 都已证明它是稳定受害点。
- **中高概率破坏来源**：写坏发生在更早的截图/overlay/相邻对象相关路径中，随后在首个 `pin_windows_.push_back(...)` 或后续 vector 遍历时暴露。
- **尚未完全证实的部分**：精确到“哪一行代码第一次越界/悬垂写入”的首个写坏点，当前仍没有直接证据；2026-03-21 的稳定自动探针尚未把它抓出来。

换言之，这次已经可以排除：

- 单纯的双击/托盘逻辑问题
- 单纯的 `PinManager` 容器重入逻辑错误
- 单纯的 `CaptureResult` / `CapturedBitmap` 所有权转移错误

但还不能把责任精确钉死到某一处具体语句，也不能仅凭当前证据把责任完全锁死在 `CaptureOverlay` 单一函数上。

### 为什么当前修复有效

当前稳定修复的本质不是“治好了越界写”，而是：

- 通过堆分配把原本相邻的内联对象拆开
- 降低同一宿主对象内相邻成员互相踩内存的概率
- 让原本立即破坏 `PinManager` 的写坏不再直接命中其关键字段

因此它是一个**有效且已验证的缓解/隔离修复**，但从工程定义上仍应视为：

- 已解决用户可见崩溃
- 但底层写坏点尚未百分之百闭环

### 验证结果

已完成验证：

- 查看 crash report 与日志，确认旧问题模式一致。
- 使用 dump 反汇编确认 `0x...B095` 是被污染后的 `pin_windows_` 尾指针写入，`0x...B714` 是同一向量的后续遍历/整理暴露点。
- 结合会话日志确认 `0x...B095` 样本发生在新进程第一次贴图链路中，日志停在 `Pin window created and shown.` 之后、`Created pin window. open_count=1.` 之前。
- 通过调整 `PinManager` 重入行为，确认重入不是根因，只会改变症状显现时机。
- 通过旧 worktree 自动探针确认多条稳定路径以及 `CreatePin` 内部关键节点在 `push_back` 前保持干净，说明直接受害点明确，但首个写坏点仍未稳定复现。
- 通过对象堆隔离修复后，由用户在 2026-03-20 完成 26 次 `copy+pin` 截图并混合 `save` / `copyOnly` 压测，无异常。

未完成验证：

- `clang-cl + AddressSanitizer` 诊断构建尝试过，但当前本机 `clang-cl + lld-link` 运行库接法未打通，暂未拿到可直接指向越界语句的 ASan 报告。

### 后续建议

如果后续再次出现类似“仅 Release 崩溃、Debug 正常”的问题，优先按以下顺序处理：

1. 先确认问题是否与对象布局、优化级别、成员相邻关系相关。
2. 第一时间保留：
   - 崩溃前后日志
   - crash report
   - `.dmp`
   - 对应构建产物和提交号
3. 用 dump 先判断是：
   - 正常逻辑分支触发崩溃
   - 还是“容器/析构/虚表/回调”在访问已被污染的对象
4. 如果改动轻微的对象布局就能显著改变现象，优先按“未定义行为/内存破坏”处理，而不要误判为普通业务逻辑 bug。
5. 若时间允许，优先准备可复现的专项诊断构建：
   - `clang-cl + ASan`
   - 或其它带额外边界检查/页堆的版本
6. 在根写坏点未锁死前，修复描述必须明确区分：
   - 已证实事实
   - 高概率推断
   - 尚未证明部分

### 当前状态

- 用户可见崩溃：已消失
- 修复可用性：已通过针对性手工压测
- 根因闭环程度：高置信度定位到“布局敏感的内存破坏，直接破坏了 `PinManager` vector 状态”；精确写坏点仍待后续专项诊断
