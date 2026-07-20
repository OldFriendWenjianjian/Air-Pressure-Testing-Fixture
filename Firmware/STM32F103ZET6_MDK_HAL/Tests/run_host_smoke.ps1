$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

$testDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $testDir
$includeDir = Join-Path $projectDir 'App\Inc'
$sourceDir = Join-Path $projectDir 'App\Src'
$buildDir = Join-Path $testDir 'build'
$gcc = 'C:\Qt\Tools\mingw1310_64\bin\gcc.exe'

if (-not (Test-Path -LiteralPath $gcc -PathType Leaf)) {
    throw "GCC not found: $gcc"
}
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$tests = @(
    @{
        Name = 'calibration_smoke'
        Sources = @(
            'Tests\calibration_smoke.c',
            'App\Src\app_pressure_calibration_math.c',
            'App\Src\app_pressure_calibration_store_logic.c',
            'App\Src\app_sensor_calibration_route.c',
            'App\Src\app_sensor_calibration_protocol.c'
        )
    },
    @{ Name = 'pcba_protocol_smoke'; Sources = @('Tests\pcba_protocol_smoke.c', 'App\Src\app_protocol.c') },
    @{ Name = 'pcba_soft_uart_logic_smoke'; Sources = @('Tests\pcba_soft_uart_logic_smoke.c') },
    @{
        Name = 'pcba_rx_stream_smoke'
        Sources = @(
            'Tests\pcba_rx_stream_smoke.c',
            'App\Src\app_pcba_rx_stream.c',
            'App\Src\app_protocol.c'
        )
    },
    @{ Name = 'pressure_hotplug_smoke'; Sources = @('Tests\pressure_hotplug_smoke.c') },
    @{ Name = 'pressure_math_saturation_smoke'; Sources = @('Tests\pressure_math_saturation_smoke.c') },
    @{ Name = 'pressure_scope_smoke'; Sources = @('Tests\pressure_scope_smoke.c') },
    @{ Name = 'pressure_settle_smoke'; Sources = @('Tests\pressure_settle_smoke.c') },
    @{ Name = 'pressure_trend_smoke'; Sources = @('Tests\pressure_trend_smoke.c') },
    @{ Name = 'pressure_vent_smoke'; Sources = @('Tests\pressure_vent_smoke.c') }
)

foreach ($test in $tests) {
    $output = Join-Path $buildDir ($test.Name + '.exe')
    $sources = @($test.Sources | ForEach-Object { Join-Path $projectDir $_ })
    & $gcc -std=c11 -Wall -Wextra -Werror -I $includeDir @sources -o $output
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed: $($test.Name)"
    }
    & $output
    if ($LASTEXITCODE -ne 0) {
        throw "Test failed: $($test.Name)"
    }
}

Write-Output "All $($tests.Count) MCU host smoke tests passed."
