# CaptureZY Agent Workflow

## Scope

These rules apply to repository-local development and verification work in this repo.

## Build Rules

- This project uses Ninja rather than MSBuild. Before any `cmake --preset ...` or `cmake --build --preset ...` command on Windows, activate the MSVC environment with:

  ```powershell
  & "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
  ```

- Run the configure/build command in the same PowerShell session after `Launch-VsDevShell.ps1`.
- When Codex needs to run `cmake --preset ...`, `cmake --build --preset ...`, `cmake --install ...`, or `cpack ...`, request sandbox bypass first and run them outside the sandbox. In-repo configure/build can time out or run unreliably inside the sandbox.

## Formatting Rules

- After every code change, run `clang-format` on the changed C++ files.
- Keep formatting close to the project style while editing so the final diff stays small.

## Clang-Tidy Rules

- After every C++ change, run `scripts\run_clang_tidy_changed.ps1`.
- Activate the repository virtual environment before running the changed-files clang-tidy script:

  ```powershell
  . .\.venv\Scripts\Activate.ps1
  ```

- `run_clang_tidy_changed.ps1` is the changed-files check and should cover new and modified `.cpp` translation units.
- Full-repo checks should continue to use `scripts\run_clang_tidy.ps1`.

## Reporting Rules

- After finishing a change, provide a short report before asking for the next task.
- The report should include:
  - what changed
  - what verification was run
  - what passed or what remains unresolved
- If manual verification or manual操作 is needed, provide:
  - exact steps
  - expected results
- When manual verification is required for the next decision, wait for user feedback before committing and before moving to the next phase.

## Commit Rules

- Inspect the worktree and split commits by concern instead of batching unrelated changes together.
- Prefer separate commits for:
  - product/function changes
  - tooling or script fixes
  - documentation updates
- Commit titles must follow Conventional Commits, for example:
  - `feat(settings): add persisted capture defaults`
  - `fix(scripts): repair changed clang-tidy runner`
  - `docs(status): add current project status summary`
- Prefer commit messages with both a title and a body for non-trivial changes.
- The commit title should stay in English and remain concise so simple `git log --oneline` output stays clean.
- Keep the title short and readable; aim for about 50 characters when practical, and treat 72 characters as a soft upper bound.
- Add a blank line after the title, then write the commit body in Chinese.
- The commit body should focus on the most useful context for later review:
  - why this change was made
  - what user-visible or architectural behavior changed
  - important scope limits, follow-up notes, or verification highlights when relevant
- Keep the body concise and readable; do not dump a file-by-file changelog into the commit message.
- Wrap or split long body text near 72 characters per line when it helps readability, but treat this as a guideline rather than a hard rule.
- Do not force fixed body sections such as feature/implementation/test headers unless they genuinely improve clarity for that specific commit.
