param(
    [ValidateSet("query", "enable", "disable")]
    [string]$Mode = "query",
    [string]$ImageName = "CaptureZY.exe",
    [string]$GflagsPath = ""
)

function Resolve-GflagsPath {
    param([string]$RequestedPath)

    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
        if (-not (Test-Path $RequestedPath)) {
            throw "指定的 gflags 路径不存在: $RequestedPath"
        }

        return $RequestedPath
    }

    $command = Get-Command gflags.exe -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    $wellKnownPath = "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\gflags.exe"
    if (Test-Path $wellKnownPath) {
        return $wellKnownPath
    }

    $fallback = Get-ChildItem -LiteralPath "C:\Program Files (x86)\Windows Kits\10\Debuggers" -Recurse `
        -Filter "gflags.exe" -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like "*\\x64\\gflags.exe" } |
        Select-Object -First 1

    if ($null -ne $fallback) {
        return $fallback.FullName
    }

    throw "未找到 gflags.exe，请先安装 Debugging Tools for Windows。"
}

try {
    $resolvedGflagsPath = Resolve-GflagsPath -RequestedPath $GflagsPath
}
catch {
    Write-Error $_
    exit 1
}

Write-Host "使用 gflags: $resolvedGflagsPath"
Write-Host "目标映像: $ImageName"
Write-Host "执行模式: $Mode"

$gflagsArgs = switch ($Mode) {
    "query" { @("/p", "/query", $ImageName) }
    "enable" { @("/p", "/enable", $ImageName, "/full") }
    "disable" { @("/p", "/disable", $ImageName) }
}

switch ($Mode) {
    "query" {
        break
    }

    "enable" {
        Write-Warning "这会修改本机 $ImageName 的 page heap 设置，并影响后续启动的进程。"
        break
    }

    "disable" {
        Write-Warning "这会移除本机 $ImageName 的 page heap 设置。"
        break
    }
}

$gflagsOutput = & $resolvedGflagsPath @gflagsArgs 2>&1
$gflagsOutput

if ($LASTEXITCODE -ne 0) {
    $combinedOutput = [string]::Join([Environment]::NewLine, ($gflagsOutput | ForEach-Object { $_.ToString() }))
    if ($combinedOutput -match 'error 5') {
        Write-Error "gflags 返回访问被拒绝。请在管理员 PowerShell 中重试。"
        exit $LASTEXITCODE
    }

    Write-Error "gflags 执行失败，退出码: $LASTEXITCODE"
    exit $LASTEXITCODE
}
