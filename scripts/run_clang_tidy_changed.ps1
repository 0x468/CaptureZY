param(
    [string]$BuildDir = "out/build/windows-msvc",
    [string]$BaseRef = "HEAD",
    [string]$CompileConfig = "Debug",
    [switch]$Fix,
    [switch]$NoQuiet
)

$sourcePatterns = @('src/*.cpp', 'src/*/*.cpp')
$headerPatterns = @('src/*.h', 'src/*/*.h')
$projectRoot = (Get-Location).Path
$sourceRoot = Join-Path $projectRoot 'src'

function Normalize-RepoPath([string]$path) {
    return $path.Replace('\', '/')
}

function Resolve-IncludeTarget([string]$file, [string]$includePath) {
    $fileDirectory = Split-Path (Join-Path $projectRoot $file) -Parent
    $relativeCandidate = [System.IO.Path]::GetFullPath((Join-Path $fileDirectory $includePath))
    $sourceCandidate = [System.IO.Path]::GetFullPath((Join-Path $sourceRoot $includePath))

    foreach ($candidate in @($relativeCandidate, $sourceCandidate)) {
        if (-not $candidate.StartsWith($sourceRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            continue
        }

        if (Test-Path $candidate) {
            return Normalize-RepoPath((Resolve-Path -Relative $candidate).TrimStart('.','\'))
        }
    }

    return $null
}

function Get-ImpactedSources([string[]]$changedHeaders, [string[]]$allFiles) {
    $reverseIncludes = @{}
    $projectFiles = $allFiles | ForEach-Object { Normalize-RepoPath($_) }

    foreach ($file in $projectFiles) {
        foreach ($line in Get-Content $file) {
            if ($line -match '^\s*#include\s+"([^"]+)"') {
                $target = Resolve-IncludeTarget $file $Matches[1]
                if (-not $target) {
                    continue
                }

                if (-not $reverseIncludes.ContainsKey($target)) {
                    $reverseIncludes[$target] = New-Object System.Collections.Generic.HashSet[string]
                }

                $null = $reverseIncludes[$target].Add($file)
            }
        }
    }

    $pending = New-Object System.Collections.Generic.Queue[string]
    $visited = New-Object System.Collections.Generic.HashSet[string]
    $impactedSources = New-Object System.Collections.Generic.HashSet[string]

    foreach ($header in $changedHeaders | ForEach-Object { Normalize-RepoPath($_) }) {
        $pending.Enqueue($header)
        $null = $visited.Add($header)
    }

    while ($pending.Count -gt 0) {
        $current = $pending.Dequeue()
        if (-not $reverseIncludes.ContainsKey($current)) {
            continue
        }

        foreach ($dependent in $reverseIncludes[$current]) {
            if ($dependent -like '*.cpp') {
                $null = $impactedSources.Add($dependent)
                continue
            }

            if ($visited.Add($dependent)) {
                $pending.Enqueue($dependent)
            }
        }
    }

    return @($impactedSources | Sort-Object)
}

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
        $filteredCompileDir = Join-Path $env:TEMP ("capturezy-clang-tidy-" + [guid]::NewGuid().ToString("N"))
        New-Item -ItemType Directory -Path $filteredCompileDir | Out-Null
        $filteredCompileCommands = Join-Path $filteredCompileDir "compile_commands.json"
        $filteredDatabase | ConvertTo-Json -Depth 4 | Set-Content -Path $filteredCompileCommands -NoNewline
        $effectiveBuildDir = $filteredCompileDir
    }
}

$trackedFiles = git diff --name-only --diff-filter=ACMR $BaseRef -- `
    $sourcePatterns $headerPatterns

$untrackedFiles = git ls-files --others --exclude-standard -- `
    $sourcePatterns $headerPatterns

$changedFiles = @(@($trackedFiles) + @($untrackedFiles)) |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
    Sort-Object -Unique

if (-not $changedFiles) {
    Write-Host "没有检测到相对于 $BaseRef 的 C/C++ 源文件改动。"
    exit 0
}

$changedSources = $changedFiles | Where-Object { $_ -like '*.cpp' }
$changedHeaders = $changedFiles | Where-Object { $_ -like '*.h' }
$allProjectFiles = git ls-files -- $sourcePatterns $headerPatterns |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
    Sort-Object -Unique

if ($changedHeaders) {
    $headerImpactedSources = Get-ImpactedSources $changedHeaders $allProjectFiles
    $files = @(@($changedSources) + @($headerImpactedSources)) |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        Sort-Object -Unique

    if (-not $files) {
        $files = $allProjectFiles | Where-Object { $_ -like '*.cpp' }
    }
} else {
    $files = $changedSources
}

if (-not $files) {
    Write-Host "没有可供 clang-tidy 检查的 C/C++ 翻译单元。"
    exit 0
}

$arguments = @("-p", $effectiveBuildDir)

if (-not $NoQuiet) {
    $arguments += "--quiet"
}

if ($Fix) {
    $arguments += @("--fix", "--format-style=file")
}

if ($changedHeaders) {
    Write-Host "检测到头文件改动，将检查受影响的翻译单元以覆盖头文件诊断。"
}

Write-Host "检查文件:"
$files | ForEach-Object { Write-Host "  $_" }

$arguments += $files
& clang-tidy @arguments
$clangTidyExitCode = $LASTEXITCODE

if ($filteredCompileDir) {
    Remove-Item -Path $filteredCompileDir -Recurse -Force -ErrorAction SilentlyContinue
}

exit $clangTidyExitCode
