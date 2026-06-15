# Capture Editing Next Phase Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将截图后的编辑态正式收敛为产品主入口，为后续标注工具、撤销重做和截图设置页打下稳定基础。

**Architecture:** 继续以 `feature_capture::CaptureOverlay` 为编辑态主承载，但不再把所有语义继续堆在同一层分支判断里。先收敛工具条结构、动作分发和状态表达，再引入最小的标注状态模型与撤销/重做基础接口，保证后续功能是在稳定骨架上叠加。

**Tech Stack:** Win32, GDI, C++20, clang-format, clang-tidy, CMake/Ninja

---

## File Map

- Modify: `src/feature_capture/capture_overlay.h`
  - 继续收敛编辑态数据结构、工具条动作和输入分发接口。
- Modify: `src/feature_capture/capture_overlay.cpp`
  - 实现编辑态工具条、提示、动作分发、占位禁用态和后续标注入口。
- Create: `src/feature_capture/capture_annotation.h`
  - 定义最小标注对象模型和撤销/重做所需的基础类型。
- Create: `src/feature_capture/capture_annotation.cpp`
  - 放置最小实现或空实现，避免把标注相关细节继续塞进 overlay。
- Modify: `src/feature_capture/CMakeLists.txt`
  - 接入新增的标注基础文件。
- Modify: `docs/current-status.md`
  - 更新截图编辑态的当前完成度与后续优先级。
- Modify: `docs/manual-regression.md`
  - 补充编辑态工具条和后续标注底座的回归步骤。

## Planned Commits

1. `refactor(capture): prepare icon-ready editing toolbar`
2. `feat(capture): add editing action dispatch model`
3. `feat(capture): introduce annotation foundation`
4. `docs(status): refresh capture editing progress`

### Task 1: Prepare Icon-Ready Editing Toolbar

**Files:**

- Modify: `src/feature_capture/capture_overlay.h`
- Modify: `src/feature_capture/capture_overlay.cpp`
- Test: manual overlay verification

- [ ] **Step 1: 收敛工具条按钮元数据**

把当前分散的 `ToolbarActionLabel`、分组、宽度、是否可交互等规则整理成单一元数据来源，为后续图标替换和 hover 提示做准备。

- [ ] **Step 2: 区分“展示按钮”和“动作按钮”**

明确左侧标注占位、中间撤销重做占位、右侧结果动作三类按钮的绘制和命中策略，避免后续再出现占位控件误参与输入的问题。

- [ ] **Step 3: 增加 hover 提示基础接口**

先不实现真正的提示浮层，但至少为按钮准备简短提示文本来源，后面可以无痛切换到图标加提示模式。

- [ ] **Step 4: 运行验证**

Run:

```powershell
clang-format -i src\feature_capture\capture_overlay.h src\feature_capture\capture_overlay.cpp
. .\.venv\Scripts\Activate.ps1; .\scripts\run_clang_tidy_changed.ps1
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation; cmake --build --preset windows-msvc-debug
```

Expected:

- 格式化完成
- changed clang-tidy 无新增诊断
- debug 构建通过

- [ ] **Step 5: 手工验证**

验证点：

- 工具条仍保持三段结构
- 占位按钮不可点击、无 hover/pressed 反馈
- 右侧动作按钮反馈保持正常
- 后续替换图标时不需要再次改动命中或布局算法

- [ ] **Step 6: Commit**

```bash
git add src/feature_capture/capture_overlay.h src/feature_capture/capture_overlay.cpp
git commit -m "refactor(capture): prepare icon-ready editing toolbar"
```

### Task 2: Add Editing Action Dispatch Model

**Files:**

- Modify: `src/feature_capture/capture_overlay.h`
- Modify: `src/feature_capture/capture_overlay.cpp`
- Modify: `docs/manual-regression.md`
- Test: manual overlay verification

- [ ] **Step 1: 收敛动作分发语义**

把双击、Enter、中键、工具条右侧按钮、右键、Esc 的语义集中整理，避免继续分散在多个 `switch` 路径里。

- [ ] **Step 2: 拆出“编辑态动作”层**

引入比 `OverlayResult` 更贴近编辑态的内部动作表达，例如：

- 提交复制
- 提交贴图
- 提交快存
- 退出截图
- 回到未选区

最终再映射到 `OverlayResult` 或内部状态切换。

- [ ] **Step 3: 同步顶部提示文案**

让顶部提示与当前真实语义一致，不再混用历史行为描述。优先保证：

- 取消按钮直接退出
- 右键回到上一级
- 双击和 Enter 为复制

- [ ] **Step 4: 运行验证**

Run:

```powershell
clang-format -i src\feature_capture\capture_overlay.h src\feature_capture\capture_overlay.cpp
. .\.venv\Scripts\Activate.ps1; .\scripts\run_clang_tidy_changed.ps1
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation; cmake --build --preset windows-msvc-debug
```

Expected:

- 无新增 clang-tidy 诊断
- 构建通过

- [ ] **Step 5: 手工验证**

验证点：

- `Esc` 始终直接退出
- 右键在有选区时回到未选区，无选区时退出
- 工具条 `取消` 直接退出
- 双击、Enter、中键、贴图、复制、快存全部与提示文案一致

- [ ] **Step 6: Commit**

```bash
git add src/feature_capture/capture_overlay.h src/feature_capture/capture_overlay.cpp docs/manual-regression.md
git commit -m "feat(capture): add editing action dispatch model"
```

### Task 3: Introduce Annotation Foundation

**Files:**

- Create: `src/feature_capture/capture_annotation.h`
- Create: `src/feature_capture/capture_annotation.cpp`
- Modify: `src/feature_capture/capture_overlay.h`
- Modify: `src/feature_capture/capture_overlay.cpp`
- Modify: `src/feature_capture/CMakeLists.txt`
- Test: build verification and manual placeholder verification

- [ ] **Step 1: 定义最小标注模型**

先只定义基础类型，不急着做真正工具：

- 标注类型枚举
- 标注对象基础结构
- 编辑会话中的标注列表
- 撤销/重做堆栈入口

- [ ] **Step 2: 为撤销/重做占位接入真实状态**

即使暂时没有实际标注数据，也要让中间两个按钮具备“可启用 / 不可启用”的状态来源，避免后面再次推翻工具条结构。

- [ ] **Step 3: 保持标注功能未开放但结构可扩展**

左侧按钮仍可保持不可点击，或者只允许切换当前工具而不产生绘制结果。此阶段不要引入真实画笔、文字或马赛克。

- [ ] **Step 4: 运行验证**

Run:

```powershell
clang-format -i src\feature_capture\capture_overlay.h src\feature_capture\capture_overlay.cpp src\feature_capture\capture_annotation.h src\feature_capture\capture_annotation.cpp
. .\.venv\Scripts\Activate.ps1; .\scripts\run_clang_tidy_changed.ps1
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation; cmake --build --preset windows-msvc-debug
```

Expected:

- 新文件接入成功
- 无新增 clang-tidy 诊断
- debug 构建通过

- [ ] **Step 5: 手工验证**

验证点：

- 工具条布局不回退
- 中间撤销/重做具备明确的启用态来源
- 引入标注基础结构后，不影响当前截图、复制、贴图、快存主链路

- [ ] **Step 6: Commit**

```bash
git add src/feature_capture/capture_overlay.h src/feature_capture/capture_overlay.cpp src/feature_capture/capture_annotation.h src/feature_capture/capture_annotation.cpp src/feature_capture/CMakeLists.txt
git commit -m "feat(capture): introduce annotation foundation"
```

### Task 4: Refresh Status and Regression Docs

**Files:**

- Modify: `docs/current-status.md`
- Modify: `docs/roadmap.md`
- Modify: `docs/manual-regression.md`

- [ ] **Step 1: 更新当前完成度**

把截图编辑态从“基础调整态外壳”更新为“工具条基础闭环已成型，进入标注底座阶段”。

- [ ] **Step 2: 更新下一阶段优先级**

明确接下来主线为：

- 编辑态工具条与语义收敛
- 标注底座
- 截图设置页

- [ ] **Step 3: 补回归清单**

补充编辑态的专项回归条目，至少覆盖：

- 工具条按钮语义
- 右键与取消分层
- 后续撤销/重做启用态

- [ ] **Step 4: Commit**

```bash
git add docs/current-status.md docs/roadmap.md docs/manual-regression.md
git commit -m "docs(status): refresh capture editing progress"
```

## Scope Guardrails

- 本阶段不实现完整标注工具集。
- 本阶段不接入设置面板持久化。
- 本阶段不研究开始菜单、通知中心、快速设置等瞬态 shell flyout。
- 本阶段不扩展贴图侧新功能，除非出现明显阻塞编辑态设计的问题。

## Manual Verification Baseline

每个任务至少回归以下路径：

1. 区域截图进入编辑态
2. 拖拽移动选区
3. 拖拽 8 个控制点调整选区
4. 双击复制
5. 中键贴图
6. `Ctrl+C` 复制
7. `Ctrl+S` 快存
8. 工具条 `取消`
9. 右键回到未选区
10. `Esc` 退出
