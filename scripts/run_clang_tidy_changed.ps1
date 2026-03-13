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

$trackedFiles = git diff --name-only --diff-filter=ACMR $BaseRef -- `
    'src/*.cpp' 'src/*/*.cpp'

$untrackedFiles = git ls-files --others --exclude-standard -- `
    'src/*.cpp' 'src/*/*.cpp'

$files = @(@($trackedFiles) + @($untrackedFiles)) |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
    Sort-Object -Unique

if (-not $files) {
    Write-Host "没有检测到相对于 $BaseRef 的 C/C++ 源文件改动。"
    exit 0
}

$arguments = @("-p", $BuildDir)

if (-not $NoQuiet) {
    $arguments += "--quiet"
}

if ($Fix) {
    $arguments += @("--fix", "--format-style=file")
}

Write-Host "检查文件:"
$files | ForEach-Object { Write-Host "  $_" }

$arguments += $files
& clang-tidy @arguments
