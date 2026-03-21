param(
    [string]$BuildDir = "out/build/windows-clang-asan",
    [ValidateSet("RelWithDebInfo", "Release")]
    [string]$BuildType = "RelWithDebInfo",
    [bool]$EnableDiagnosticSelfTest = $true,
    [switch]$ConfigureOnly,
    [switch]$SkipRuntimeCopy
)

function Resolve-ToolPath {
    param(
        [string]$CommandName,
        [string]$FallbackPath,
        [string]$MissingMessage
    )

    $command = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    if (-not [string]::IsNullOrWhiteSpace($FallbackPath) -and (Test-Path $FallbackPath)) {
        return $FallbackPath
    }

    throw $MissingMessage
}

function Get-ShortPath {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        throw "路径不存在，无法转换短路径: $Path"
    }

    $shortPath = cmd.exe /c "for %I in (""$Path"") do @echo %~sI"
    if ([string]::IsNullOrWhiteSpace($shortPath)) {
        throw "无法获取短路径: $Path"
    }

    return $shortPath.Trim()
}

function Resolve-AsanLibDir {
    param([string]$LlvmRoot)

    $clangLibRoot = Join-Path $LlvmRoot "lib\clang"
    if (-not (Test-Path $clangLibRoot)) {
        throw "未找到 LLVM clang 库目录: $clangLibRoot"
    }

    $candidate = Get-ChildItem -Path $clangLibRoot -Directory |
        Sort-Object Name -Descending |
        ForEach-Object {
            $libDir = Join-Path $_.FullName "lib\windows"
            $asanLib = Join-Path $libDir "clang_rt.asan_dynamic-x86_64.lib"
            $asanThunk = Join-Path $libDir "clang_rt.asan_dynamic_runtime_thunk-x86_64.lib"
            $asanDll = Join-Path $libDir "clang_rt.asan_dynamic-x86_64.dll"

            if ((Test-Path $asanLib) -and (Test-Path $asanThunk) -and (Test-Path $asanDll)) {
                [PSCustomObject]@{
                    LibDir = $libDir
                    DynamicLib = $asanLib
                    RuntimeThunk = $asanThunk
                    RuntimeDll = $asanDll
                }
            }
        } |
        Select-Object -First 1

    if ($null -eq $candidate) {
        throw "未找到可用的 LLVM ASan runtime。"
    }

    return $candidate
}

$vsDevShell = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1"
if (-not (Test-Path $vsDevShell)) {
    Write-Error "未找到 VS Dev Shell 脚本: $vsDevShell"
    exit 1
}

try {
    $clangClPath = Resolve-ToolPath -CommandName "clang-cl.exe" `
        -FallbackPath "C:\Program Files\LLVM\bin\clang-cl.exe" `
        -MissingMessage "未找到 clang-cl.exe，请先安装 LLVM。"
}
catch {
    Write-Error $_
    exit 1
}

$llvmRoot = Split-Path -Parent (Split-Path -Parent $clangClPath)

try {
    $asanRuntime = Resolve-AsanLibDir -LlvmRoot $llvmRoot
    $asanLibDirShort = Get-ShortPath -Path $asanRuntime.LibDir
}
catch {
    Write-Error $_
    exit 1
}

$buildTypeUpper = $BuildType.ToUpperInvariant()
$repoRoot = Split-Path -Parent $PSScriptRoot
$configureArgs = @(
    "-S", ".",
    "-B", $BuildDir,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-DCMAKE_CXX_COMPILER=clang-cl",
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL",
    "-DCMAKE_CXX_FLAGS_${buildTypeUpper}=/fsanitize=address /Zi -fuse-ld=link",
    "-DCMAKE_EXE_LINKER_FLAGS_${buildTypeUpper}=/LIBPATH:$asanLibDirShort clang_rt.asan_dynamic-x86_64.lib clang_rt.asan_dynamic_runtime_thunk-x86_64.lib /DEBUG",
    "-DCMAKE_SHARED_LINKER_FLAGS_${buildTypeUpper}=/LIBPATH:$asanLibDirShort clang_rt.asan_dynamic-x86_64.lib clang_rt.asan_dynamic_runtime_thunk-x86_64.lib /DEBUG",
    "-DCAPTUREZY_FORCE_DIAGNOSTIC_SELF_TEST=$(if ($EnableDiagnosticSelfTest) { 'ON' } else { 'OFF' })"
)

Push-Location $repoRoot

try {
    Write-Host "使用 clang-cl: $clangClPath"
    Write-Host "LLVM 根目录: $llvmRoot"
    Write-Host "ASan runtime 目录: $($asanRuntime.LibDir)"
    Write-Host "构建目录: $BuildDir"
    Write-Host "构建类型: $BuildType"
    Write-Host "诊断自测开关: $(if ($EnableDiagnosticSelfTest) { 'ON' } else { 'OFF' })"

    & $vsDevShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    & cmake @configureArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    if ($ConfigureOnly) {
        Write-Host "已完成 clang-cl ASan 配置。"
        exit 0
    }

    & cmake --build $BuildDir --parallel
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    $appDir = Join-Path $BuildDir "src\app"
    $appExe = Join-Path $appDir "CaptureZY.exe"
    if ((-not $SkipRuntimeCopy) -and (Test-Path $appExe)) {
        Copy-Item -Path $asanRuntime.RuntimeDll -Destination $appDir -Force
        Write-Host "已复制 ASan runtime DLL 到: $appDir"
    }

    Write-Host ""
    Write-Host "clang-cl ASan 诊断构建完成。"
    Write-Host "可执行文件: $appExe"
}
finally {
    Pop-Location
}
