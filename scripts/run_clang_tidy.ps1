param(
    [string]$BuildDir = "out/build/windows-msvc",
    [string]$PathRegex = '.*[\\/]src[\\/].*',
    [string]$HeaderFilter = 'src/.*',
    [int]$Jobs = 0,
    [switch]$Fix,
    [switch]$NoQuiet
)

$compileCommands = Join-Path $BuildDir "compile_commands.json"
if (-not (Test-Path $compileCommands)) {
    Write-Error "未找到 $compileCommands，请先执行 CMake 配置。"
    exit 1
}

if ($Jobs -le 0) {
    $Jobs = [Environment]::ProcessorCount
}

$runClangTidyScript = $null
$pythonScript = $false
foreach ($candidate in @("run-clang-tidy.py", "run-clang-tidy")) {
    $commandInfo = Get-Command $candidate -ErrorAction SilentlyContinue
    if ($commandInfo) {
        $runClangTidyScript = $commandInfo.Source
        $pythonScript = $runClangTidyScript.EndsWith(".py")
        break
    }
}

if (-not $runClangTidyScript) {
    $repoLocalScript = Join-Path $PSScriptRoot "..\run-clang-tidy.py"
    if (Test-Path $repoLocalScript) {
        $runClangTidyScript = $repoLocalScript
        $pythonScript = $true
    } else {
        Write-Error "未找到 run-clang-tidy.py 或 run-clang-tidy。请安装 LLVM 附带脚本，或将 run-clang-tidy.py 放到仓库根目录。"
        exit 1
    }
}

$command = @()
if ($pythonScript) {
    $command += @("python", $runClangTidyScript)
} else {
    $command += $runClangTidyScript
}

$command += @(
    "-p", $BuildDir,
    "-j", "$Jobs",
    "-header-filter", $HeaderFilter
)

if (-not $NoQuiet) {
    $command += "-quiet"
}

if ($Fix) {
    $command += @("-fix", "-format")
}

$command += $PathRegex

Write-Host "执行命令:"
Write-Host ($command -join " ")

& $command[0] $command[1..($command.Length - 1)]
