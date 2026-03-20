param(
    [string]$DumpPath = "",
    [string]$DiagnosticsDir = "$env:LOCALAPPDATA\CaptureZY\Diagnostics",
    [string]$CdbPath = "",
    [string]$InitialCommand = "",
    [switch]$ShowDisassembly
)

function Resolve-CdbPath {
    param([string]$RequestedPath)

    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
        if (-not (Test-Path $RequestedPath)) {
            throw "指定的 cdb 路径不存在: $RequestedPath"
        }

        return $RequestedPath
    }

    $command = Get-Command cdb.exe -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    $wellKnownPath = "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe"
    if (Test-Path $wellKnownPath) {
        return $wellKnownPath
    }

    $fallback = Get-ChildItem -LiteralPath "C:\Program Files (x86)\Windows Kits\10\Debuggers" -Recurse `
        -Filter "cdb.exe" -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like "*\\x64\\cdb.exe" } |
        Select-Object -First 1

    if ($null -ne $fallback) {
        return $fallback.FullName
    }

    throw "未找到 cdb.exe，请先安装 WinDbg 或 Debugging Tools for Windows。"
}

if ([string]::IsNullOrWhiteSpace($DumpPath)) {
    if (-not (Test-Path $DiagnosticsDir)) {
        Write-Error "未找到诊断目录: $DiagnosticsDir"
        exit 1
    }

    $latestDump = Get-ChildItem -Path $DiagnosticsDir -Filter "crash-*.dmp" -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1

    if ($null -eq $latestDump) {
        Write-Error "未找到 dump 文件。"
        exit 1
    }

    $DumpPath = $latestDump.FullName
}

if (-not (Test-Path $DumpPath)) {
    Write-Error "未找到 dump 文件: $DumpPath"
    exit 1
}

try {
    $resolvedCdbPath = Resolve-CdbPath -RequestedPath $CdbPath
}
catch {
    Write-Error $_
    exit 1
}

$commands = @(
    ".ecxr",
    "r",
    "kb"
)

if ($ShowDisassembly) {
    $commands += "u @rip-32 L64"
}

if (-not [string]::IsNullOrWhiteSpace($InitialCommand)) {
    $commands += $InitialCommand
}

$commands += "q"
$commandString = [string]::Join("; ", $commands)

Write-Host "使用 cdb: $resolvedCdbPath"
Write-Host "分析 dump: $DumpPath"
Write-Host "执行命令: $commandString"

& $resolvedCdbPath -z $DumpPath -c $commandString
