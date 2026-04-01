# Annotation Screen Canvas Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将截图编辑态中的矩形标注从“依附选区的局部对象”切换为“依附整屏冻结画板的可编辑对象”，同时保持选区只负责交互闸门与最终导出裁剪。

**Architecture:** 保持 `feature_capture::CaptureOverlay` 作为截图编辑器控制器，但将标注对象坐标统一迁移到“相对冻结画板左上角的像素坐标”。`AnnotationSession` 继续只保存对象和历史；选区内交互闸门、对象命中和编辑状态仍归 `CaptureOverlay` 管理。实现按“坐标模型切换 -> 渲染切换 -> 交互闸门切换 -> 文档回归收口”分批提交，避免把旧语义和新语义混在同一提交中。

**Tech Stack:** Win32, GDI, C++23, CMake/Ninja, clang-format, clang-tidy, ctest

---

## File Map

- Modify: `src/feature_capture/capture_annotation.h`
  - 将标注几何从相对选区 `NormalizedRectF` 迁移为相对整屏画板左上角的像素矩形。
- Modify: `src/feature_capture/capture_annotation.cpp`
  - 保持对象替换与 undo/redo 语义不变，但切到新的像素坐标对象结构。
- Modify: `src/feature_capture/capture_annotation_geometry.h`
  - 将平移/约束辅助从选区归一化坐标改为画板像素坐标。
- Modify: `src/feature_capture/capture_annotation_geometry.cpp`
  - 实现像素矩形平移、选区交集和控制点可见性所需的基础几何辅助。
- Modify: `src/feature_capture/capture_overlay.h`
  - 调整草稿对象、选中对象、拖拽对象的状态字段与辅助函数签名到画板像素坐标。
- Modify: `src/feature_capture/capture_overlay.cpp`
  - 将新建、绘制、命中、移动语义切换到整屏画板；增加“选区内才允许标注交互”的总闸。
- Modify: `tests/feature_capture/annotation_session_test.cpp`
  - 用新的像素矩形对象更新现有测试，并补充像素平移/不再依附选区的基础测试。
- Modify: `tests/CMakeLists.txt`
  - 继续确保 `capture_annotation_geometry.cpp` 被测试目标编译进来。
- Modify: `docs/manual-regression.md`
  - 补充“跨选区可见但不可交互”“调整选区不影响已有标注”的回归项。
- Modify: `docs/current-status.md`
  - 同步截图编辑态从“选区画板”转向“整屏画板”的真实阶段状态。

## Planned Commits

1. `refactor(capture): store annotation bounds in canvas pixels`
2. `feat(capture): render annotations on the screen canvas`
3. `feat(capture): gate annotation editing to selection`
4. `docs(status): update screen canvas annotation progress`

### Task 1: Replace Annotation Bounds With Screen-Canvas Pixels

**Files:**

- Modify: `src/feature_capture/capture_annotation.h`
- Modify: `src/feature_capture/capture_annotation.cpp`
- Modify: `src/feature_capture/capture_annotation_geometry.h`
- Modify: `src/feature_capture/capture_annotation_geometry.cpp`
- Modify: `tests/feature_capture/annotation_session_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

在 `tests/feature_capture/annotation_session_test.cpp` 新增或改写失败用例，明确标注对象使用像素矩形，而不是相对选区比例。至少覆盖：

- `AnnotationObject` 能保存画板像素矩形
- `ReplaceObject()` 在像素矩形下仍保持作用范围与 undo/redo 语义
- 几何辅助平移像素矩形时，只按画板边界裁剪，而不是按当前选区比例换算

示例方向：

```cpp
bool TestTranslateCanvasRectWithinCanvas()
{
    RECT const canvas{.left = 0, .top = 0, .right = 1920, .bottom = 1080};
    RECT const original{.left = 100, .top = 120, .right = 300, .bottom = 260};

    AnnotationTranslationResult const result =
        TranslateAnnotationBoundsWithinRect(original, canvas, 50, -20);

    if (!Expect(result.moved, "translation should report movement"))
    {
        return false;
    }
    return Expect(EqualRect(&result.bounds, &RECT{150, 100, 350, 240}) != FALSE,
                  "translation should stay in pixel space");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location D:\Repos\CaptureZY\.worktrees\rectangle-annotation-edit
cmake --build --preset windows-msvc-debug --target capturezy_annotation_session_test
& .\out\build\windows-msvc\tests\Debug\capturezy_annotation_session_test.exe
```

Expected:

- 编译失败，或测试失败并指出对象几何仍是 `NormalizedRectF`

- [ ] **Step 3: Write minimal implementation**

在 `capture_annotation.h/.cpp` 与 `capture_annotation_geometry.h/.cpp` 中完成最小切换：

- 用画板像素矩形替换 `AnnotationObject::bounds`
- 保持 `AnnotationStyle`、`AddObject()`、`ReplaceObject()`、`Undo()`、`Redo()` 行为不变
- 几何辅助输入输出都改成像素矩形
- 继续把对象坐标定义为“相对冻结画板左上角”，不是桌面绝对屏幕坐标

约束：

- 不在本任务修改 overlay 事件流
- 不在本任务改变选区交互语义
- 不在本任务引入新的 UI 或命中规则

- [ ] **Step 4: Run test to verify it passes**

Run:

```powershell
& .\out\build\windows-msvc\tests\Debug\capturezy_annotation_session_test.exe
```

Expected:

- 轻量测试通过

- [ ] **Step 5: Run static checks and build**

Run:

```powershell
clang-format -i src\feature_capture\capture_annotation.h src\feature_capture\capture_annotation.cpp src\feature_capture\capture_annotation_geometry.h src\feature_capture\capture_annotation_geometry.cpp tests\feature_capture\annotation_session_test.cpp
. D:\Repos\CaptureZY\.venv\Scripts\Activate.ps1
.\scripts\run_clang_tidy_changed.ps1
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
cmake --build --preset windows-msvc-debug
```

Expected:

- changed clang-tidy 无新增诊断
- Debug 构建通过

- [ ] **Step 6: Commit**

```bash
git add src/feature_capture/capture_annotation.h src/feature_capture/capture_annotation.cpp src/feature_capture/capture_annotation_geometry.h src/feature_capture/capture_annotation_geometry.cpp tests/feature_capture/annotation_session_test.cpp tests/CMakeLists.txt
git commit -m "refactor(capture): store annotation bounds in canvas pixels"
```

### Task 2: Render Draft and Committed Annotations On The Screen Canvas

**Files:**

- Modify: `src/feature_capture/capture_overlay.h`
- Modify: `src/feature_capture/capture_overlay.cpp`
- Test: manual overlay verification

- [ ] **Step 1: Write the failing behavior probe**

先明确当前失败行为，并把预期写进局部注释或计划执行记录：

- 调整截图选区时，已有标注会跟随变化
- 标注绘制仍然依赖 `committed_selection_rect_`
- 草稿矩形只能在选区画板里存在

- [ ] **Step 2: Implement minimal render-space switch**

在 `CaptureOverlay` 中完成绘制语义切换：

- 标注对象和草稿对象都直接使用整屏画板像素坐标
- `PaintAnnotations(...)` 不再以选区矩形作为对象坐标基准
- `PaintDraftAnnotation(...)` 不再把草稿矩形限制为选区归一化坐标
- 选区框、工具条、顶部提示保持现有行为

建议实现方向：

- 对象绘制基准改为 overlay client/canvas rect
- 保留对象可见性，不做“超出选区即不绘制”的裁剪
- 若对象横跨选区边界，选区外部分照常显示

- [ ] **Step 3: Verify selection resize no longer transforms existing objects**

在代码里确保：

- 调整 `committed_selection_rect_` 时，不再反算已有对象 bounds
- 已有对象位置只受对象编辑操作影响，不受截图选区变化影响

- [ ] **Step 4: Run build and static checks**

Run:

```powershell
clang-format -i src\feature_capture\capture_overlay.h src\feature_capture\capture_overlay.cpp
. D:\Repos\CaptureZY\.venv\Scripts\Activate.ps1
.\scripts\run_clang_tidy_changed.ps1
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
cmake --build --preset windows-msvc-debug --clean-first
```

Expected:

- changed clang-tidy 无新增诊断
- clean build 通过

- [ ] **Step 5: Manual verification**

验证点：

1. 进入截图并创建选区
2. 开启矩形工具并新建一个矩形
3. 调整截图选区大小与位置
4. 观察已有矩形是否保持屏幕位置不变
5. 观察矩形跨出选区后是否仍然完整显示

- [ ] **Step 6: Commit**

```bash
git add src/feature_capture/capture_overlay.h src/feature_capture/capture_overlay.cpp
git commit -m "feat(capture): render annotations on the screen canvas"
```

### Task 3: Gate Annotation Interaction To The Current Selection

**Files:**

- Modify: `src/feature_capture/capture_overlay.h`
- Modify: `src/feature_capture/capture_overlay.cpp`
- Modify: `docs/manual-regression.md`
- Test: manual overlay verification

- [ ] **Step 1: Write the failing manual cases**

先明确以下失败预期：

- 选区外空白仍可能触发标注逻辑
- 命中测试没有“先判断是否位于选区内”的总闸
- 对象移动仍被限制在选区内，无法拖出选区

把这些点登记到 `docs/manual-regression.md` 的待更新项中。

- [ ] **Step 2: Add the selection interaction gate**

在 overlay 命中与输入分发中增加总判断：

- 若鼠标点不在当前选区内，直接屏蔽所有标注交互
- 若鼠标点在当前选区内，才继续命中标注对象或开始新建标注
- 只有起笔点在选区内时，形状工具才允许开始新建矩形

- [ ] **Step 3: Allow draft and object movement beyond the selection**

更新创建与移动语义：

- 草稿矩形起点必须在选区内，但拖拽终点可超出选区
- 已有对象命中后，允许整体移动到选区外
- 对象移动不再 clamp 到选区内
- 一旦对象完全移出选区，本轮截图中应无法再直接命中它

- [ ] **Step 4: Restrict handles and object hit-testing to visible interactive area**

明确并实现：

- 控制点只有落在选区内时才可显示、可命中
- 对象边框/填充区只有与选区相交且鼠标落在选区内时才参与命中
- 对象横跨选区边界时，从选区内命中后仍操作整个对象

- [ ] **Step 5: Verify history and export semantics**

要求：

- 对象拖动提交时仍只写一次历史
- 新建、移动、调整选区后，最终复制/保存/贴图只导出选区内可见部分
- 现有工具条 `取消 / 贴图 / 快存 / 复制` 行为不回归

- [ ] **Step 6: Run verification**

Run:

```powershell
clang-format -i src\feature_capture\capture_overlay.h src\feature_capture\capture_overlay.cpp docs\manual-regression.md
. D:\Repos\CaptureZY\.venv\Scripts\Activate.ps1
.\scripts\run_clang_tidy_changed.ps1
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
cmake --build --preset windows-msvc-debug --clean-first
& .\out\build\windows-msvc\tests\Debug\capturezy_annotation_session_test.exe
```

Expected:

- changed clang-tidy 无新增诊断
- clean build 通过
- 轻量测试通过

- [ ] **Step 7: Manual verification**

验证点：

1. 新建矩形时，必须从选区内开始
2. 拖拽新建过程中，可以把矩形拉出选区边界
3. 已有矩形可整体拖到选区外
4. 选区外仍能看到矩形，但在选区外无法命中和编辑它
5. 矩形横跨选区边界时，可从选区内部分命中并移动整个对象
6. 调整选区后，已有矩形不跟随缩放或位移
7. 导出结果只包含选区内可见部分
8. 右键回退、`Esc` 退出、工具条动作不回归

- [ ] **Step 8: Commit**

```bash
git add src/feature_capture/capture_overlay.h src/feature_capture/capture_overlay.cpp docs/manual-regression.md
git commit -m "feat(capture): gate annotation editing to selection"
```

### Task 4: Refresh Status And Regression Docs

**Files:**

- Modify: `docs/current-status.md`
- Modify: `docs/manual-regression.md`

- [ ] **Step 1: Update current status**

在 `docs/current-status.md` 中同步以下事实：

- 标注坐标系已从“相对选区”切换为“相对整屏冻结画板”
- 当前截图选区只负责交互闸门与导出裁剪
- 矩形已支持跨选区显示，但交互仍限制在选区内
- 8 点缩放、删除、其他工具族仍留在下一阶段

- [ ] **Step 2: Update manual regression checklist**

在 `docs/manual-regression.md` 补充专项回归：

- 调整选区不影响已有标注位置
- 标注可超出选区显示
- 选区外可见但不可交互
- 从选区内命中跨边界对象仍操作整个对象
- 导出只保留选区内部分

- [ ] **Step 3: Commit**

```bash
git add docs/current-status.md docs/manual-regression.md
git commit -m "docs(status): update screen canvas annotation progress"
```

## Scope Guardrails

- 本计划不实现矩形 8 点缩放的完整产品化体验
- 本计划不实现矩形删除
- 本计划不实现箭头、文字、马赛克等工具
- 本计划不实现标注样式面板与持久化
- 本计划不实现多选、编组和 z-order 管理
- 本计划不触碰贴图产品线

## Manual Verification Baseline

整个计划完成后，至少统一回归以下路径：

1. 进入截图并创建选区
2. 开启矩形工具并新建矩形，起笔必须位于选区内
3. 拖拽新建时允许超出选区边界
4. 调整选区后，已有矩形保持屏幕位置不变
5. 已有矩形可以整体移动到选区外
6. 选区外仍显示对象，但不能在选区外直接选中或拖动对象
7. 横跨选区边界的对象，可从选区内部分命中并操作整个对象
8. 最终复制/保存/贴图只包含选区内可见部分
9. 工具条 `取消 / 贴图 / 快存 / 复制` 与右键/`Esc` 不回归