[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$sourceRoot = Join-Path $projectRoot 'src'
$cmakePath = Join-Path $projectRoot 'CMakeLists.txt'
$failures = [System.Collections.Generic.List[string]]::new()
$strictUtf8 = [System.Text.UTF8Encoding]::new($false, $true)

function Add-Failure([string]$message) {
    $script:failures.Add($message)
}

$sourceFiles = @(Get-ChildItem -LiteralPath $sourceRoot -Recurse -File |
    Where-Object { $_.Extension -in '.h', '.cpp' })

foreach ($file in $sourceFiles) {
    try {
        [void]$strictUtf8.GetString([System.IO.File]::ReadAllBytes($file.FullName))
    }
    catch {
        Add-Failure "Invalid UTF-8: $($file.FullName)"
    }

    $lineNumber = 0
    foreach ($line in [System.IO.File]::ReadAllLines($file.FullName, $strictUtf8)) {
        ++$lineNumber
        if ($line -notmatch '^\s*#include\s+"([^"]+)"') {
            continue
        }
        $include = $Matches[1].Replace('/', [System.IO.Path]::DirectorySeparatorChar)
        $candidates = @(
            (Join-Path $file.DirectoryName $include),
            (Join-Path $sourceRoot $include),
            (Join-Path $projectRoot $include)
        )
        if (-not ($candidates | Where-Object { Test-Path -LiteralPath $_ })) {
            Add-Failure "Missing local include at $($file.FullName):$lineNumber -> $($Matches[1])"
        }
    }
}

$cmakeText = [System.IO.File]::ReadAllText($cmakePath, $strictUtf8)
$listedSources = @([regex]::Matches(
    $cmakeText, 'src/[A-Za-z0-9_./-]+\.(?:h|cpp)') |
    ForEach-Object { $_.Value.Replace('/', [System.IO.Path]::DirectorySeparatorChar) } |
    Sort-Object -Unique)

foreach ($listed in $listedSources) {
    if (-not (Test-Path -LiteralPath (Join-Path $projectRoot $listed))) {
        Add-Failure "CMake lists a missing file: $listed"
    }
}

foreach ($file in $sourceFiles) {
    $relative = $file.FullName.Substring($projectRoot.Length + 1)
    if ($listedSources -notcontains $relative) {
        Add-Failure "Source file is not listed in CMake: $relative"
    }
}

$allSource = ($sourceFiles | ForEach-Object {
    [System.IO.File]::ReadAllText($_.FullName, $strictUtf8)
}) -join "`n"

$knownBadPatterns = @(
    @{ Pattern = 'qMax\s*\(\s*0\s*,[^\r\n]*\.size\s*\('; Message = 'qMax mixes an int literal with a Qt container size; use one explicit type.' },
    @{ Pattern = '\bwait\s*\(\s*3000\s*\)'; Message = 'A QThread shutdown still ignores the old fixed three-second timeout.' },
    @{ Pattern = 'contains\s*\(\s*QChar\s*\(\s*0xFFFD\s*\)'; Message = 'UTF-8 validation still treats every replacement character as a decoder error.' },
    @{ Pattern = 'autoReplyAsciiFramesReceived|autoReplyHexFramesReceived'; Message = 'Automatic reply has regressed to two independently emitted format pipelines.' },
    @{ Pattern = 'm_frames\.size\(\)\s*<=\s*1\s*\|\|\s*rowCount\(\)'; Message = 'A single receive frame can bypass the configured display-row limit.' },
    @{ Pattern = 'angleDelta\(\)\.y\(\)\s*>\s*0\s*\?\s*1\s*:\s*-1'; Message = 'A horizontal Ctrl+wheel event still changes a font size.' }
)

foreach ($check in $knownBadPatterns) {
    if ($allSource -match $check.Pattern) {
        Add-Failure $check.Message
    }
}

$mainWindowPath = Join-Path $sourceRoot 'ui\MainWindow.cpp'
$mainWindowText = [System.IO.File]::ReadAllText($mainWindowPath, $strictUtf8)
if ($mainWindowText -match 'm_logWriter\.isLogging\(\)[\s\S]{0,500}QString::fromUtf8\s*\(\s*frame\.data\s*\)') {
    Add-Failure 'Logging raw serial data through a lossy UTF-8 round trip is not allowed.'
}

if ($failures.Count -gt 0) {
    Write-Host "Static checks failed ($($failures.Count)):" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host "  - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Static checks passed: $($sourceFiles.Count) C++ files, $($listedSources.Count) CMake entries." -ForegroundColor Green
Write-Host 'Note: this script checks project structure and known regressions; a real Qt/C++ build is still required.'
