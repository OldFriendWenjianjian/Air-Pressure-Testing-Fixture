$ErrorActionPreference = 'Stop'

$qtBin = 'C:\Qt\6.10.1\mingw_64\bin'
$mingwBin = 'C:\Qt\Tools\mingw1310_64\bin'
$env:Path = "$qtBin;$mingwBin;$env:Path"

$projectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $projectDir 'build-qmake'
$testBuildDir = Join-Path $projectDir 'build-qmake-tests'
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
New-Item -ItemType Directory -Force -Path $testBuildDir | Out-Null

Push-Location $buildDir
try {
    & (Join-Path $qtBin 'qmake.exe') (Join-Path $projectDir 'PressureFixtureHost.pro') -spec win32-g++ CONFIG+=release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & (Join-Path $mingwBin 'mingw32-make.exe') -j4
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Pop-Location
}

Push-Location $testBuildDir
try {
    & (Join-Path $qtBin 'qmake.exe') (Join-Path $projectDir 'tests\protocol_smoke.pro') -spec win32-g++ CONFIG+=release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & (Join-Path $mingwBin 'mingw32-make.exe') -j4
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & (Join-Path $testBuildDir 'release\ProtocolSmoke.exe')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Pop-Location
}
