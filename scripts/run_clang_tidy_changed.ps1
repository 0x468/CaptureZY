param(
    [string]$BuildDir = "out/build/windows-msvc",
    [string]$BaseRef = "HEAD",
    [switch]$Fix,
    [switch]$NoQuiet
)

$compileCommands = Join-Path $BuildDir "compile_commands.json"
if (-not (Test-Path $compileCommands)) {
    Write-Error "未找到 $compileCommands，请先执行 CMake 配置。"
    exit 1
}

$files = git diff --name-only --diff-filter=ACMR $BaseRef -- `
    'src/*.cpp' 'src/*.h' 'src/*.hpp' `
    'src/*/*.cpp' 'src/*/*.h' 'src/*/*.hpp' |
    Sort-Object -Unique

if (-not $files) {
    Write-Host "没有检测到相对于 $BaseRef 的 C/C++ 源文件改动。"
    exit 0
}

$command = @(
    "clang-tidy",
    "-p", $BuildDir
)

if (-not $NoQuiet) {
    $command += "--quiet"
}

if ($Fix) {
    $command += @("--fix", "--format-style=file")
}

Write-Host "检查文件:"
$files | ForEach-Object { Write-Host "  $_" }

& $command[0] $command[1..($command.Length - 1)] $files
