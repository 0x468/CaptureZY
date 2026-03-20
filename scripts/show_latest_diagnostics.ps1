param(
    [string]$DiagnosticsDir = "$env:LOCALAPPDATA\CaptureZY\Diagnostics",
    [string]$LogPath = "$env:LOCALAPPDATA\CaptureZY\Logs\capturezy.log",
    [int]$TailLines = 120
)

if (-not (Test-Path $DiagnosticsDir)) {
    Write-Error "未找到诊断目录: $DiagnosticsDir"
    exit 1
}

$latestReport = Get-ChildItem -Path $DiagnosticsDir -Filter "crash-*.txt" -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

$latestDump = Get-ChildItem -Path $DiagnosticsDir -Filter "crash-*.dmp" -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

if ($null -eq $latestReport -and $null -eq $latestDump) {
    Write-Error "未找到 crash report 或 dump。"
    exit 1
}

Write-Host "诊断目录: $DiagnosticsDir"
if ($null -ne $latestReport) {
    Write-Host "最新 crash report: $($latestReport.FullName)"
}
if ($null -ne $latestDump) {
    Write-Host "最新 dump: $($latestDump.FullName)"
}

$sessionId = $null
$reportLogPath = $null

if ($null -ne $latestReport) {
    Write-Host ""
    Write-Host "===== crash report ====="
    $reportLines = Get-Content -Path $latestReport.FullName
    $reportLines

    foreach ($line in $reportLines) {
        if ($line -match '^session_id:\s*(.+)$') {
            $sessionId = $Matches[1].Trim()
            continue
        }

        if ($line -match '^log_file:\s*(.+)$') {
            $reportLogPath = $Matches[1].Trim()
        }
    }
}

if (-not [string]::IsNullOrWhiteSpace($reportLogPath)) {
    $LogPath = $reportLogPath
}

if (-not (Test-Path $LogPath)) {
    Write-Warning "未找到日志文件: $LogPath"
    exit 0
}

Write-Host ""
Write-Host "===== log tail ====="
Write-Host "日志文件: $LogPath"

if (-not [string]::IsNullOrWhiteSpace($sessionId)) {
    $sessionLines = Select-String -Path $LogPath -SimpleMatch "[sid=$sessionId" |
        ForEach-Object { $_.Line }
    if ($sessionLines.Count -gt 0) {
        Write-Host "按 session_id 过滤: $sessionId"
        $sessionLines
        exit 0
    }
}

$logTailLines = Get-Content -Path $LogPath -Tail $TailLines
$logTailLines
