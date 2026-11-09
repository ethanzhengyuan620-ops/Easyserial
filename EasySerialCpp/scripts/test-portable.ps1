param([Parameter(Mandatory)][string]$Executable)
$ErrorActionPreference = 'Stop'
$root = Join-Path (Split-Path -Parent $PSScriptRoot) ('build\portable-check-' + [Guid]::NewGuid().ToString('N'))
$isolated = Join-Path $root '单文件 测试'
$runtime = Join-Path $root '临时 文件'
[void][IO.Directory]::CreateDirectory($isolated)
[void][IO.Directory]::CreateDirectory($runtime)
$exe = Join-Path $isolated 'EasySerial.exe'
Copy-Item -LiteralPath $Executable -Destination $exe
$oldPath = $env:PATH
$oldTemp = $env:TEMP
$oldTmp = $env:TMP
$oldPlugins = $env:QT_PLUGIN_PATH
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $env:TEMP = $runtime
    $env:TMP = $runtime
    $env:QT_PLUGIN_PATH = ''
    $watch = [Diagnostics.Stopwatch]::StartNew()
    # Concurrent launches must have independent extraction directories.
    $processes = @(1..2 | ForEach-Object {
        Start-Process -FilePath $exe -ArgumentList '--smoke-test' -WorkingDirectory $isolated -WindowStyle Hidden -PassThru
    })
    foreach ($process in $processes) {
        if (-not $process.WaitForExit(30000)) {
            # Do not forcibly terminate an application with possible user state.
            throw "Portable smoke test timed out (PID $($process.Id)); inspect it manually."
        }
        if ($process.ExitCode -ne 0) { throw "Portable smoke test failed: $($process.ExitCode)" }
    }
    # Qt may leave a small cache file directly in TEMP. The launcher-owned
    # extraction directory must still be gone; cache files are removed with the
    # isolated test root below and are not application data.
    if (@(Get-ChildItem -LiteralPath $runtime -Directory -Force).Count -ne 0) { throw 'Temporary extraction directories were not cleaned up' }
    if (@(Get-ChildItem -LiteralPath $isolated -Force).Count -ne 1) { throw 'Portable version created files beside the EXE' }
    $beforeFailure = @(Get-ChildItem -LiteralPath $runtime -Force | ForEach-Object { $_.Name + ':' + $_.Length } | Sort-Object)
    $env:TEMP = Join-Path $runtime 'nonexistent-parent'
    $env:TMP = $env:TEMP
    $failure = Start-Process -FilePath $exe -ArgumentList '--smoke-test' -WorkingDirectory $isolated -WindowStyle Hidden -PassThru
    if (-not $failure.WaitForExit(15000) -or $failure.ExitCode -ne 12) {
        throw 'Invalid TEMP folder was not handled safely'
    }
    $afterFailure = @(Get-ChildItem -LiteralPath $runtime -Force | ForEach-Object { $_.Name + ':' + $_.Length } | Sort-Object)
    # Existing Qt cache files predate the intentional failed launch. The failure
    # path must leave the directory unchanged, not erase another component's cache.
    if (($beforeFailure -join "`n") -cne ($afterFailure -join "`n")) { throw 'Failure handling left files behind' }
    Write-Output ("Single-file test passed: no adjacent DLLs, no Qt PATH, two concurrent launches, cleanup OK ({0:N2}s)." -f $watch.Elapsed.TotalSeconds)
} finally {
    $env:PATH = $oldPath
    $env:TEMP = $oldTemp
    $env:TMP = $oldTmp
    $env:QT_PLUGIN_PATH = $oldPlugins
}
# Only this exact generated test root; it is never a user data directory.
if ((Resolve-Path -LiteralPath $root -ErrorAction SilentlyContinue) -and
    (Split-Path -Parent $root) -eq (Resolve-Path -LiteralPath (Join-Path (Split-Path -Parent $PSScriptRoot) 'build')).Path) {
    try {
        Remove-Item -LiteralPath $root -Recurse -Force
    } catch [System.UnauthorizedAccessException], [System.IO.IOException] {
        # Windows/Qt can retain a cache file after both processes have exited.
        # Launcher extraction cleanup is asserted above; deleting this test-only
        # cache is best effort and must not invalidate successful launch tests.
        Write-Warning "Could not remove an in-use test cache in ${root}: $($_.Exception.Message)"
    }
}
