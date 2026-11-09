param([string]$QtRoot = 'D:\Qt', [switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -QtRoot $QtRoot
    return
}
$dist = Join-Path $PSScriptRoot 'build\runtime'
$work = Join-Path $PSScriptRoot 'build\single-file'
$compiler = Join-Path $QtRoot 'Tools\mingw1310_64\bin'
$icon = Join-Path $PSScriptRoot 'build\generated\easyserial.ico'
$app = Join-Path $dist 'EasySerial.exe'
if (-not (Test-Path -LiteralPath $app) -or -not (Test-Path -LiteralPath $icon)) {
    throw 'Build the application before packaging.'
}
[void][IO.Directory]::CreateDirectory($work)
$utf8 = [Text.UTF8Encoding]::new($false)
# These generated build inputs are not source files. Recreate them each package run.
$ddf = [Collections.Generic.List[string]]::new()
@('.OPTION EXPLICIT', '.Set Cabinet=ON', '.Set Compress=ON', '.Set CompressionType=LZX',
  '.Set CompressionMemory=21', '.Set MaxDiskSize=0', '.Set CabinetNameTemplate=payload.cab',
  ('.Set DiskDirectory1="' + $work + '"'),
  ('.Set RptFileName="' + (Join-Path $work 'payload.rpt') + '"'),
  ('.Set InfFileName="' + (Join-Path $work 'payload.inf') + '"')) | ForEach-Object { $ddf.Add($_) }
$files = @(Get-ChildItem -LiteralPath $dist -Recurse -File | Where-Object {
    $_.Extension -in '.dll', '.qm' -or $_.FullName -eq $app -or $_.Name -eq 'qt.conf'
} | Sort-Object FullName)
foreach ($file in $files) {
    $relative = $file.FullName.Substring($dist.Length + 1)
    if ($relative -notmatch '^[a-zA-Z0-9_.+\\-]+$') { throw "Unsupported bundle filename: $relative" }
    $ddf.Add(('"' + $file.FullName + '" "' + $relative + '"'))
}
$directives = Join-Path $work 'payload.ddf'
[IO.File]::WriteAllLines($directives, $ddf, $utf8)
$oldPath = $env:PATH
try {
    $env:PATH = "$compiler;$oldPath"
    & "$env:SystemRoot\System32\makecab.exe" /V1 /F $directives | Out-File (Join-Path $work 'makecab.log')
    if ($LASTEXITCODE -ne 0) { throw 'Cabinet compression failed; see build\single-file\makecab.log' }
    $cab = (Join-Path $work 'payload.cab').Replace('\', '/')
    $template = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'assets\EasySerial.rc.in'))
    $rc = $template.Replace('@APP_ICON@', $icon.Replace('\', '/')) + "`n101 RCDATA `"$cab`"`n"
    $manifest = (Join-Path $PSScriptRoot 'assets\portable.manifest').Replace('\', '/')
    $rc += "1 24 `"$manifest`"`n"
    $resource = Join-Path $work 'portable.rc'
    [IO.File]::WriteAllText($resource, $rc, $utf8)
    $object = Join-Path $work 'portable-res.o'
    & "$compiler\windres.exe" -i $resource -o $object -O coff
    if ($LASTEXITCODE -ne 0) { throw 'Portable resource build failed' }
    $output = Join-Path $work 'EasySerial-Portable.exe'
    & "$compiler\g++.exe" '-std=c++20' '-Os' '-Wall' '-Wextra' '-Wpedantic' '-static' '-static-libgcc' '-static-libstdc++' '-municode' '-mwindows' '-s' (Join-Path $PSScriptRoot 'scripts\portable_launcher.cpp') $object '-lcabinet' '-lshell32' '-lole32' '-o' $output
    if ($LASTEXITCODE -ne 0) { throw 'Portable launcher build failed' }
    & (Join-Path $PSScriptRoot 'scripts\test-portable.ps1') -Executable $output
    $release = Join-Path $PSScriptRoot 'dist'
    [void][IO.Directory]::CreateDirectory($release)
    $destination = Join-Path $release 'EasySerial.exe'
    Copy-Item -LiteralPath $output -Destination $destination -Force
    # The new single-file executable has passed verification. Remove only known
    # deployed runtime files from the previous folder release, never user data.
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($dist.Length + 1)
        if ($relative -eq 'EasySerial.exe') { continue }
        $old = Join-Path $release $relative
        if (Test-Path -LiteralPath $old -PathType Leaf) { Remove-Item -LiteralPath $old }
    }
    $directories = @(Get-ChildItem -LiteralPath $dist -Directory -Recurse | Sort-Object { $_.FullName.Length } -Descending)
    foreach ($directory in $directories) {
        $old = Join-Path $release $directory.FullName.Substring($dist.Length + 1)
        if (Test-Path -LiteralPath $old -PathType Container) {
            # Non-recursive: an unexpected/user-created file prevents deletion.
            if (@(Get-ChildItem -LiteralPath $old -Force).Count -eq 0) { [IO.Directory]::Delete($old) }
        }
    }
    Write-Output ("Ready: {0} ({1:N1} MiB; only this file is required)" -f $destination, ((Get-Item -LiteralPath $destination).Length / 1MB))
} finally { $env:PATH = $oldPath }
