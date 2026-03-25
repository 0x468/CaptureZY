function Invoke-ClangTidyHeaderProbes {
    param(
        [string[]]$Headers,
        [string]$BuildDir,
        [switch]$NoQuiet
    )

    $headersToCheck = $Headers |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        Sort-Object -Unique

    if (-not $headersToCheck) {
        return 0
    }

    $projectRoot = (Get-Location).Path
    $includeDir = (Join-Path $projectRoot 'src').Replace('\', '/')
    $probeDir = Join-Path $BuildDir 'clang-tidy-header-probes'
    New-Item -ItemType Directory -Path $probeDir -Force | Out-Null

    Write-Host "检查头文件:"
    $overallExitCode = 0

    foreach ($header in $headersToCheck) {
        Write-Host "  $header"

        $headerAbsolutePath = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $header)).Replace('\', '/')
        $probeFileName = ($header -replace '[\\/:\.]', '_') + '.cpp'
        $probePath = Join-Path $probeDir $probeFileName
        Set-Content -Path $probePath -Value "#include `"$headerAbsolutePath`"" -NoNewline

        $arguments = @(
            $probePath,
            '--header-filter=.*'
        )

        if (-not $NoQuiet) {
            $arguments += '--quiet'
        }

        $arguments += @(
            '--',
            '-x', 'c++',
            '-std=c++23',
            '-I', $includeDir,
            '-DUNICODE',
            '-D_UNICODE',
            '-DNOMINMAX',
            '-DWIN32_LEAN_AND_MEAN'
        )

        $probeOutput = & clang-tidy @arguments 2>&1
        $probeExitCode = $LASTEXITCODE

        $diagnosticLines = New-Object System.Collections.Generic.List[string]
        $printFollowingLines = 0
        foreach ($entry in $probeOutput) {
            $line = [string]$entry
            if ($line.Contains($headerAbsolutePath)) {
                $diagnosticLines.Add($line)
                $printFollowingLines = 2
                continue
            }

            if ($printFollowingLines -gt 0) {
                if ($line -match '^\s*\d+\s+\|.*$' -or $line -match '^\s*\|\s*[\^~].*$') {
                    $diagnosticLines.Add($line)
                    $printFollowingLines--
                    continue
                }

                $printFollowingLines = 0
            }
        }

        if ($diagnosticLines.Count -gt 0) {
            $diagnosticLines | ForEach-Object { Write-Host $_ }
        } else {
            Write-Host "    (no direct header diagnostics)"
        }

        if ($probeExitCode -ne 0) {
            $overallExitCode = $probeExitCode
        }

        Remove-Item -Path $probePath -Force -ErrorAction SilentlyContinue
    }

    Remove-Item -Path $probeDir -Recurse -Force -ErrorAction SilentlyContinue
    return $overallExitCode
}
