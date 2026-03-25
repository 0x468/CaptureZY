param(
    [string]$BuildDir = "out/build/windows-msvc",
    [string]$PathRegex = '.*[\\/]src[\\/].*',
    [string]$HeaderFilter = 'src/.*',
    [string]$HeaderPathRegex = '.*[\\/]src[\\/].*',
    [string]$CompileConfig = "Debug",
    [int]$Jobs = 0,
    [switch]$Fix,
    [switch]$NoQuiet
)

. (Join-Path $PSScriptRoot 'clang_tidy_header_probe.ps1')

$headerPatterns = @('src/*.h', 'src/*/*.h')
$compileCommands = Join-Path $BuildDir "compile_commands.json"
if (-not (Test-Path $compileCommands)) {
    Write-Error "未找到 $compileCommands，请先执行 CMake 配置。"
    exit 1
}

$effectiveBuildDir = $BuildDir
$filteredCompileDir = $null
$compileDatabase = Get-Content $compileCommands -Raw | ConvertFrom-Json
if ($compileDatabase -is [System.Array]) {
    $configOutputPattern = ('*\{0}\*' -f $CompileConfig)
    $filteredDatabase = $compileDatabase | Where-Object {
        $_.output -like $configOutputPattern
    }

    if ($filteredDatabase) {
        $filteredCompileDir = Join-Path $env:TEMP ("capturezy-clang-tidy-full-" + [guid]::NewGuid().ToString("N"))
        New-Item -ItemType Directory -Path $filteredCompileDir | Out-Null
        $filteredCompileCommands = Join-Path $filteredCompileDir "compile_commands.json"
        $filteredDatabase | ConvertTo-Json -Depth 4 | Set-Content -Path $filteredCompileCommands -NoNewline
        $effectiveBuildDir = $filteredCompileDir
    }
}

if ($Jobs -le 0) {
    $Jobs = [Environment]::ProcessorCount
}

$runClangTidyScript = $null
$invokeWithPython = $false
foreach ($candidate in @("run-clang-tidy.py", "run-clang-tidy")) {
    $commandInfo = Get-Command $candidate -ErrorAction SilentlyContinue
    if ($commandInfo) {
        $runClangTidyScript = $commandInfo.Source
        $extension = [System.IO.Path]::GetExtension($runClangTidyScript)
        $invokeWithPython = [string]::IsNullOrEmpty($extension) -or $extension -eq ".py"
        break
    }
}

if (-not $runClangTidyScript) {
    $repoLocalScript = Join-Path $PSScriptRoot "..\run-clang-tidy.py"
    if (Test-Path $repoLocalScript) {
        $runClangTidyScript = $repoLocalScript
        $invokeWithPython = $true
    } else {
        Write-Error "未找到 run-clang-tidy.py 或 run-clang-tidy。请安装 LLVM 附带脚本，或将 run-clang-tidy.py 放到仓库根目录。"
        exit 1
    }
}

$command = @()
if ($invokeWithPython) {
    $command += @("python", $runClangTidyScript)
} else {
    $command += $runClangTidyScript
}

$command += @(
    "-p", $effectiveBuildDir,
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
$clangTidyExitCode = $LASTEXITCODE

$projectRoot = (Get-Location).Path
$headers = git ls-files -- $headerPatterns |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
    Where-Object {
        $absolutePath = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $_))
        $absolutePath -match $HeaderPathRegex
    } |
    Sort-Object -Unique

$headerProbeExitCode = Invoke-ClangTidyHeaderProbes -Headers $headers -BuildDir $BuildDir -NoQuiet:$NoQuiet

if ($filteredCompileDir) {
    Remove-Item -Path $filteredCompileDir -Recurse -Force -ErrorAction SilentlyContinue
}

if ($clangTidyExitCode -ne 0) {
    exit $clangTidyExitCode
}

exit $headerProbeExitCode
