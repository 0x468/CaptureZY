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

## 当前限制

- 不以 Linux 构建为目标
- 当前骨架只验证仓库布局与 Win32 入口
- 等截图 API 接入后，再补充 Windows SDK 版本要求
