$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

$qtBin = 'C:\Qt\6.10.1\mingw_64\bin'
$mingwBin = 'C:\Qt\Tools\mingw1310_64\bin'
$env:Path = "$qtBin;$mingwBin;$env:Path"

function Invoke-VerifiedWinDeploy {
    param(
        [Parameter(Mandatory = $true)][string]$ExePath,
        [string[]]$RequiredQtFiles = @()
    )

    & (Join-Path $qtBin 'windeployqt.exe') --release --compiler-runtime $ExePath
    $deployExitCode = $LASTEXITCODE
    $deployDir = Split-Path -Parent $ExePath
    $requiredFiles = @(
        'Qt6Core.dll',
        'libgcc_s_seh-1.dll',
        'libstdc++-6.dll',
        'libwinpthread-1.dll'
    ) + $RequiredQtFiles
    $missingFiles = @($requiredFiles | Where-Object {
        -not (Test-Path -LiteralPath (Join-Path $deployDir $_) -PathType Leaf)
    })
    if ($missingFiles.Count -gt 0) {
        throw "windeployqt部署不完整: $ExePath 缺少 $($missingFiles -join ', ') (exit=$deployExitCode)"
    }
    if ($deployExitCode -ne 0) {
        Write-Warning "windeployqt返回$deployExitCode，但必需运行库已验证齐全；继续执行后续测试。"
    }
}

function Install-OffscreenPlatformPlugin {
    param([Parameter(Mandatory = $true)][string]$ExePath)

    $pluginSource = Join-Path (Split-Path -Parent $qtBin) 'plugins\platforms\qoffscreen.dll'
    $pluginDir = Join-Path (Split-Path -Parent $ExePath) 'platforms'
    $pluginTarget = Join-Path $pluginDir 'qoffscreen.dll'
    if (-not (Test-Path -LiteralPath $pluginSource -PathType Leaf)) {
        throw "Qt offscreen平台插件不存在: $pluginSource"
    }
    New-Item -ItemType Directory -Force -Path $pluginDir | Out-Null
    Copy-Item -LiteralPath $pluginSource -Destination $pluginTarget -Force
    if (-not (Test-Path -LiteralPath $pluginTarget -PathType Leaf)) {
        throw "Qt offscreen平台插件部署失败: $pluginTarget"
    }
}

$projectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $projectDir 'build-qmake'
$testBuildDir = Join-Path $projectDir 'build-qmake-tests'
$hexTestBuildDir = Join-Path $projectDir 'build-qmake-intel-hex-tests'
$serialTestBuildDir = Join-Path $projectDir 'build-qmake-serial-smoke'
$calibrationTestBuildDir = Join-Path $projectDir 'build-qmake-sensor-calibration-ui'
$viewTestBuildDir = Join-Path $projectDir 'build-qmake-architecture-view-tests'
$watchdogTestBuildDir = Join-Path $projectDir 'build-qmake-single-tank-watchdog-tests'
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
New-Item -ItemType Directory -Force -Path $testBuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $hexTestBuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $serialTestBuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $calibrationTestBuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $viewTestBuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $watchdogTestBuildDir | Out-Null

Push-Location $buildDir
try {
    & (Join-Path $qtBin 'qmake.exe') (Join-Path $projectDir 'PressureFixtureHost.pro') -spec win32-g++ CONFIG+=release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & (Join-Path $mingwBin 'mingw32-make.exe') -j1
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Invoke-VerifiedWinDeploy (Join-Path $buildDir 'release\PressureFixtureHost.exe') @(
        'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Network.dll', 'Qt6Charts.dll',
        'Qt6OpenGL.dll', 'Qt6OpenGLWidgets.dll', 'platforms\qwindows.dll'
    )
} finally {
    Pop-Location
}

Push-Location $testBuildDir
try {
    & (Join-Path $qtBin 'qmake.exe') (Join-Path $projectDir 'tests\protocol_smoke.pro') -spec win32-g++ CONFIG+=release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & (Join-Path $mingwBin 'mingw32-make.exe') -j1
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $testExe = Join-Path $testBuildDir 'release\ProtocolSmoke.exe'
    Invoke-VerifiedWinDeploy $testExe
    & $testExe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Pop-Location
}

Push-Location $hexTestBuildDir
try {
    & (Join-Path $qtBin 'qmake.exe') (Join-Path $projectDir 'tests\intel_hex_validator_smoke.pro') -spec win32-g++ CONFIG+=release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & (Join-Path $mingwBin 'mingw32-make.exe') -j1
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $hexTestExe = Join-Path $hexTestBuildDir 'release\IntelHexValidatorSmoke.exe'
    Invoke-VerifiedWinDeploy $hexTestExe
    & $hexTestExe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Pop-Location
}

Push-Location $serialTestBuildDir
try {
    & (Join-Path $qtBin 'qmake.exe') (Join-Path $projectDir 'tests\serial_transport_smoke.pro') -spec win32-g++ CONFIG+=release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & (Join-Path $mingwBin 'mingw32-make.exe') -j1
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $serialTestExe = Join-Path $serialTestBuildDir 'release\SerialTransportSmoke.exe'
    Invoke-VerifiedWinDeploy $serialTestExe @('Qt6Network.dll')
    & $serialTestExe --classify-only
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Pop-Location
}

Push-Location $calibrationTestBuildDir
try {
    & (Join-Path $qtBin 'qmake.exe') (Join-Path $projectDir 'tests\sensor_calibration_ui_smoke.pro') -spec win32-g++ CONFIG+=release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & (Join-Path $mingwBin 'mingw32-make.exe') -j1
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $calibrationTestExe = Join-Path $calibrationTestBuildDir 'release\SensorCalibrationUiSmoke.exe'
    Invoke-VerifiedWinDeploy $calibrationTestExe @(
        'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Network.dll', 'Qt6Charts.dll',
        'Qt6OpenGL.dll', 'Qt6OpenGLWidgets.dll', 'platforms\qwindows.dll'
    )
    Install-OffscreenPlatformPlugin $calibrationTestExe
    & $calibrationTestExe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Pop-Location
}

Push-Location $viewTestBuildDir
try {
    & (Join-Path $qtBin 'qmake.exe') (Join-Path $projectDir 'tests\architecture_view_smoke.pro') -spec win32-g++ CONFIG+=release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & (Join-Path $mingwBin 'mingw32-make.exe') -j1
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $viewTestExe = Join-Path $viewTestBuildDir 'release\ArchitectureViewSmoke.exe'
    Invoke-VerifiedWinDeploy $viewTestExe @(
        'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Test.dll', 'platforms\qwindows.dll'
    )
    & $viewTestExe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Pop-Location
}

Push-Location $watchdogTestBuildDir
try {
    & (Join-Path $qtBin 'qmake.exe') (Join-Path $projectDir 'tests\single_tank_pcba_watchdog_smoke.pro') -spec win32-g++ CONFIG+=release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & (Join-Path $mingwBin 'mingw32-make.exe') -j1
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $watchdogTestExe = Join-Path $watchdogTestBuildDir 'release\SingleTankPcbaWatchdogSmoke.exe'
    Invoke-VerifiedWinDeploy $watchdogTestExe @(
        'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Network.dll', 'Qt6Charts.dll',
        'Qt6OpenGL.dll', 'Qt6OpenGLWidgets.dll', 'platforms\qwindows.dll'
    )
    Install-OffscreenPlatformPlugin $watchdogTestExe
    & $watchdogTestExe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Pop-Location
}
