# CaptureZY Agent Workflow

## Scope

These rules apply to repository-local development and verification work in this repo.

## Build Rules

- This project uses Ninja rather than MSBuild. Before any `cmake --preset ...` or `cmake --build --preset ...` command on Windows, activate the MSVC environment with:

  ```powershell
  & "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
  ```

- Run the configure/build command in the same PowerShell session after `Launch-VsDevShell.ps1`.

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
- Commit messages must follow Conventional Commits, for example:
  - `feat(settings): add persisted capture defaults`
  - `fix(scripts): repair changed clang-tidy runner`
  - `docs(status): add current project status summary`
