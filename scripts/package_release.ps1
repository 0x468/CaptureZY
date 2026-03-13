param(
    [string]$ConfigurePreset = "windows-msvc",
    [string]$BuildPreset = "windows-msvc-release",
    [string]$BuildDir = "out/build/windows-msvc",
    [string]$InstallPrefix = "out/install/CaptureZY"
)

$vsDevShell = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1"
if (-not (Test-Path $vsDevShell)) {
    Write-Error "未找到 VS Dev Shell 脚本: $vsDevShell"
    exit 1
}

$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot

try {
    & $vsDevShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    & cmake --preset $ConfigurePreset
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    & cmake --build --preset $BuildPreset
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    & cmake --install $BuildDir --config Release --prefix $InstallPrefix
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    $cpackConfig = Join-Path $BuildDir "CPackConfig.cmake"
    & cpack --config $cpackConfig -C Release
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    Write-Host ""
    Write-Host "Release 打包完成。"
    Write-Host "安装目录: $InstallPrefix"
    Write-Host "ZIP 目录: $BuildDir"
}
finally {
    Pop-Location
}
