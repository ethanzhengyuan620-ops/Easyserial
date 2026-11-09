param([string]$QtRoot = 'D:\Qt', [string]$QtVersion = '6.11.2')
$ErrorActionPreference = 'Stop'
$qt = Join-Path $QtRoot "$QtVersion\mingw_64"
$cmake = Join-Path $QtRoot 'Tools\CMake_64\bin\cmake.exe'
$ctest = Join-Path $QtRoot 'Tools\CMake_64\bin\ctest.exe'
$compiler = Join-Path $QtRoot 'Tools\mingw1310_64\bin'
$build = Join-Path $PSScriptRoot 'build'
$dist = Join-Path $PSScriptRoot 'build\runtime'
$oldPath = $env:PATH
try {
    $env:PATH = "$compiler;$(Join-Path $QtRoot 'Tools\Ninja');$qt\bin;$oldPath"
    & $cmake -S $PSScriptRoot -B $build -G Ninja "-DCMAKE_PREFIX_PATH=$qt" '-DCMAKE_BUILD_TYPE=Release' "-DCMAKE_CXX_COMPILER=$compiler\g++.exe" '-DBUILD_TESTING=ON'
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
    & $cmake --build $build --parallel 2
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
    & $ctest --test-dir $build --output-on-failure
    $testExit = $LASTEXITCODE
    $testReport = Join-Path $build 'regression-results.txt'
    if (Test-Path -LiteralPath $testReport) { Get-Content -LiteralPath $testReport }
    if ($testExit -ne 0) { throw 'Regression tests failed; deployment skipped' }
    & $cmake --install $build --prefix $dist
    if ($LASTEXITCODE -ne 0) { throw 'Deployment failed' }
    & (Join-Path $PSScriptRoot 'package-single.ps1') -QtRoot $QtRoot -SkipBuild
} finally {
    $env:PATH = $oldPath
}
