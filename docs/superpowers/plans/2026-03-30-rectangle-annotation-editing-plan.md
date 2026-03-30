# Rectangle Annotation Editing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将截图编辑态中的矩形标注从“只能新建”提升为“可命中、可选中、可移动”的真实对象编辑能力，同时保持现有截图选区主链路不回归。

**Architecture:** 继续以 `feature_capture::CaptureOverlay` 作为编辑器控制器，把对象级 hit-test、选中状态和拖拽状态保留在 overlay 运行期状态中；`AnnotationSession` 只负责对象列表与历史快照。对象模型在本轮补齐样式基础字段与对象替换接口，为下一轮矩形缩放、删除和样式编辑留出稳定边界。

**Tech Stack:** Win32, GDI, C++23, CMake/Ninja, clang-format, clang-tidy, ctest

---

## File Map

- Modify: `src/feature_capture/capture_annotation.h`
  - 扩展 `AnnotationObject` 的样式字段，补充对象替换所需的基础接口与辅助类型。
- Modify: `src/feature_capture/capture_annotation.cpp`
  - 实现对象替换、对象查询等纯数据路径，保证 undo/redo 仍以会话快照为准。
- Modify: `src/feature_capture/capture_overlay.h`
  - 增加对象选中、hit-test 结果、对象拖拽状态与绘制拆分接口声明。
- Modify: `src/feature_capture/capture_overlay.cpp`
  - 实现对象级命中、光标反馈、选中态绘制、对象移动与空白拖拽新建共存逻辑。
- Modify: `tests/feature_capture/annotation_session_test.cpp`
  - 扩展现有轻量测试，覆盖对象替换与样式数据不丢失的基础语义。
- Test: `out/build/windows-msvc/tests/Debug/capturezy_annotation_session_test.exe`
  - 现有轻量测试目标继续作为基础回归入口。
- Modify: `docs/manual-regression.md`
  - 补充矩形对象选中、移动与命中语义的手工回归步骤。
- Modify: `docs/current-status.md`
  - 同步截图编辑态进入“对象级编辑”阶段的实际完成度。

## Planned Commits

1. `feat(capture): add annotation object selection`
2. `feat(capture): support moving rectangle annotations`
3. `docs(status): update rectangle editing progress`

### Task 1: Extend Annotation Data Model and Tests

**Files:**

- Modify: `src/feature_capture/capture_annotation.h`
- Modify: `src/feature_capture/capture_annotation.cpp`
- Modify: `tests/feature_capture/annotation_session_test.cpp`
- Test: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

在 `tests/feature_capture/annotation_session_test.cpp` 增加新的失败用例，至少覆盖：

- 对象新增后能保留样式字段
- 对象替换后只更新目标对象
- 对象替换后 `Undo()` 能回到旧对象快照

示例方向：

```cpp
bool TestReplaceObjectPreservesHistory()
{
    AnnotationSession session;
    session.Reset();
    session.AddObject(MakeRectangle(/*...*/));

    bool const replaced = session.ReplaceObject(0, MakeFilledRectangle(/*...*/));

    if (!Expect(replaced, "replace should succeed for a valid index"))
    {
        return false;
    }
    if (!Expect(session.Objects()[0].style.has_fill, "replacement should update style"))
    {
        return false;
    }
    if (!Expect(session.Undo(), "replacement should participate in undo history"))
    {
        return false;
    }
    return Expect(!session.Objects()[0].style.has_fill, "undo should restore the previous object");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation; cmake --build --preset windows-msvc-debug --target capturezy_annotation_session_test
ctest --test-dir out\build\windows-msvc -C Debug --output-on-failure --tests-regex capturezy_annotation_session_test
```

Expected:

- 测试编译失败，或测试运行失败并提示缺少 `ReplaceObject` / 样式字段

- [ ] **Step 3: Write minimal implementation**

在 `src/feature_capture/capture_annotation.h` / `src/feature_capture/capture_annotation.cpp` 中补齐最小数据模型：

- 增加 `AnnotationStyle`
- 为 `AnnotationObject` 引入 `style`
- 增加 `ReplaceObject(std::size_t index, AnnotationObject object)`
- 如有必要，补充只读访问辅助函数，避免 overlay 端直接写裸容器

约束：

- 不在本任务引入对象选中态
- 不在本任务引入 overlay 输入逻辑
- 继续保持 session 只做纯数据与历史快照

- [ ] **Step 4: Run test to verify it passes**

Run:

```powershell
ctest --test-dir out\build\windows-msvc -C Debug --output-on-failure --tests-regex capturezy_annotation_session_test
```

Expected:

- `capturezy_annotation_session_test` 通过

- [ ] **Step 5: Run static checks and build**

Run:

```powershell
clang-format -i src\feature_capture\capture_annotation.h src\feature_capture\capture_annotation.cpp tests\feature_capture\annotation_session_test.cpp
. .\.venv\Scripts\Activate.ps1; .\scripts\run_clang_tidy_changed.ps1
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation; cmake --build --preset windows-msvc-debug
```

Expected:

- changed clang-tidy 无新增诊断
- Debug 构建通过

- [ ] **Step 6: Commit**

```bash
git add src/feature_capture/capture_annotation.h src/feature_capture/capture_annotation.cpp tests/feature_capture/annotation_session_test.cpp
git commit -m "feat(capture): extend annotation object model"
```

### Task 2: Add Object Hit-Test and Selected Adorners

**Files:**

- Modify: `src/feature_capture/capture_overlay.h`
- Modify: `src/feature_capture/capture_overlay.cpp`
- Test: manual overlay verification

- [ ] **Step 1: Write the failing behavior probe**

先在代码里明确对象级 hit-test 的结构与枚举，并增加最小日志或 debug 断言辅助，确保“未命中对象”和“命中对象元素”分支清晰可见。此任务不要求写自动化 UI 测试，但要求把行为预期明确写入注释和回归步骤。

需要先失败的行为：

- 开启 `形状工具` 后，鼠标移到已有矩形边框没有选中态
- 无法区分控制点、边框和填充区
- 对象重叠时没有最上层优先语义

- [ ] **Step 2: Implement minimal hit-test model**

在 `src/feature_capture/capture_overlay.h` 中增加：

- `AnnotationHitRegion` 枚举
- `AnnotationHitResult` 结构
- `selected_annotation_index_`
- `hovered_annotation_index_`
- `active_annotation_hit_region_`

在 `src/feature_capture/capture_overlay.cpp` 中实现：

- 对象倒序 hit-test
- 8 个控制点与边框/填充区判定
- 只选中最上层对象
- 鼠标移动时基于 hit-test 设置光标

约束：

- 本任务只建立选中与 hover 反馈
- 不在本任务实现对象移动
- 空白区域仍保持当前“形状工具开启时可新建矩形”的路径

- [ ] **Step 3: Split annotation paint entry points**

从 `PaintOverlay()` 中抽出至少以下入口：

- `PaintAnnotations(...)`
- `PaintDraftAnnotation(...)`
- `PaintSelectedAnnotationAdorners(...)`

要求：

- 已有对象绘制保持不回归
- 选中对象出现 8 个控制点
- 未选中对象不绘制控制点

- [ ] **Step 4: Run build and static checks**

Run:

```powershell
clang-format -i src\feature_capture\capture_overlay.h src\feature_capture\capture_overlay.cpp
. .\.venv\Scripts\Activate.ps1; .\scripts\run_clang_tidy_changed.ps1
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation; cmake --build --preset windows-msvc-debug
```

Expected:

- changed clang-tidy 无新增诊断
- Debug 构建通过

- [ ] **Step 5: Manual verification**

验证点：

1. 开启 `形状工具`
2. 新建一个空心矩形
3. 鼠标移动到矩形边框、控制点、内部空白时光标不同
4. 空心矩形内部空白不应误显示移动光标
5. 新建两个重叠矩形时，最上层优先被命中

- [ ] **Step 6: Commit**

```bash
git add src/feature_capture/capture_overlay.h src/feature_capture/capture_overlay.cpp
git commit -m "feat(capture): add annotation object selection"
```

### Task 3: Support Moving Selected Rectangle Objects

**Files:**

- Modify: `src/feature_capture/capture_overlay.h`
- Modify: `src/feature_capture/capture_overlay.cpp`
- Test: manual overlay verification

- [ ] **Step 1: Define failing manual cases**

当前应先确认以下行为仍然失败，然后再修：

- 边框按住无法移动已有矩形
- 带填充矩形内部无法移动
- 对象移动与空白拖拽新建冲突

将这些失败预期写入 `docs/manual-regression.md` 待更新条目中。

- [ ] **Step 2: Implement object drag state**

在 overlay 中补充对象拖拽状态，例如：

- `PointerDragMode::MoveAnnotation`
- `drag_annotation_index_`
- 记录拖拽起点与对象初始归一化 bounds

逻辑要求：

- 命中边框主体时可移动对象
- 若对象 `style.has_fill == true`，命中填充区也可移动对象
- 空心矩形内部空白不允许移动
- 未命中对象时，形状工具仍可拖拽新建矩形

- [ ] **Step 3: Clamp movement to current selection**

对象移动后仍需保持在当前截图选区内。建议在当前选区 client rect 中完成：

- 把对象 bounds 投影到 client rect
- 计算拖拽位移
- 裁剪到选区范围
- 再换算回归一化坐标写回对象

写回路径优先走 `AnnotationSession::ReplaceObject(...)`，不要在 overlay 中直接篡改底层容器。

- [ ] **Step 4: Verify history semantics**

要求明确：

- 鼠标拖动过程中不反复写入 undo 栈
- 仅在拖拽提交时写入一次历史
- 拖拽取消或无实际位移时不新增历史

如现有 `ReplaceObject(...)` 不能满足，需要增加“拖拽提交时替换对象”的单次写入路径。

- [ ] **Step 5: Run verification**

Run:

```powershell
clang-format -i src\feature_capture\capture_overlay.h src\feature_capture\capture_overlay.cpp docs\manual-regression.md
. .\.venv\Scripts\Activate.ps1; .\scripts\run_clang_tidy_changed.ps1
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation; cmake --build --preset windows-msvc-debug
ctest --test-dir out\build\windows-msvc -C Debug --output-on-failure --tests-regex capturezy_annotation_session_test
```

Expected:

- 格式化完成
- changed clang-tidy 无新增诊断
- Debug 构建通过
- 轻量测试仍通过

- [ ] **Step 6: Manual verification**

验证点：

1. 开启 `形状工具` 后新建一个空心矩形
2. 边框按住可移动，内部空白不可移动
3. 新建一个带填充矩形
4. 带填充矩形边框与内部都可移动
5. 空白处按住仍然新建新矩形
6. 对象移动后位置正确，且不会越出截图选区
7. 新建矩形、移动矩形后，`撤` / `重` 不回归
8. 现有截图选区移动、缩放、确认动作不回归

- [ ] **Step 7: Commit**

```bash
git add src/feature_capture/capture_overlay.h src/feature_capture/capture_overlay.cpp docs/manual-regression.md
git commit -m "feat(capture): support moving rectangle annotations"
```

### Task 4: Refresh Status and Regression Docs

**Files:**

- Modify: `docs/current-status.md`
- Modify: `docs/manual-regression.md`

- [ ] **Step 1: Update current status**

在 `docs/current-status.md` 中同步以下事实：

- 截图编辑态已从“仅能新建矩形”进入“对象级编辑起步”
- 当前已支持矩形对象选中与移动
- 8 点缩放、删除、样式编辑仍留在下一阶段

- [ ] **Step 2: Update manual regression checklist**

在 `docs/manual-regression.md` 补充矩形对象专项回归：

- 空心矩形边框移动
- 空心矩形内部空白不响应
- 带填充矩形内部可移动
- 重叠矩形最上层优先命中
- 空白处新建与对象编辑共存

- [ ] **Step 3: Commit**

```bash
git add docs/current-status.md docs/manual-regression.md
git commit -m "docs(status): update rectangle editing progress"
```

## Scope Guardrails

- 本计划不实现矩形 8 点缩放
- 本计划不实现矩形删除
- 本计划不实现颜色、线宽、填充样式 UI
- 本计划不实现多选和 z-order 调整
- 本计划不触碰贴图产品线

## Manual Verification Baseline

整个计划执行完成后，至少统一回归以下路径：

1. 进入截图并创建选区
2. 关闭 `形状工具` 时，选区移动与缩放保持正常
3. 开启 `形状工具` 后，空白拖拽仍能新建矩形
4. 已有空心矩形边框可移动、内部不可移动
5. 已有带填充矩形边框与内部可移动
6. 多个矩形重叠时，最上层优先命中
7. `撤` / `重` 不回归
8. 工具条 `取消 / 贴图 / 快存 / 复制` 不回归
9. 右键返回上一级与 `Esc` 退出不回归
