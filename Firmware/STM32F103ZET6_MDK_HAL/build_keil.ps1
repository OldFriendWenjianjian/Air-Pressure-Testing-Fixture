param(
    [string]$LogName = 'uv4-build.log'
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

$projectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$mdkDir = Join-Path $projectDir 'MDK-ARM'
$project = Join-Path $mdkDir 'PressureFixture_STM32F103ZET6.uvprojx'
$log = Join-Path $mdkDir $LogName
$uv4 = 'C:\Users\a1258\AppData\Local\Keil_v5\UV4\UV4.exe'

foreach ($required in @($uv4, $project)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required file not found: $required"
    }
}

& $uv4 -r $project -o $log
$uv4ExitCode = $LASTEXITCODE

$summary = Select-String -Path $log -Pattern '0 Error\(s\)' | Select-Object -Last 1
if (-not $summary) {
    Get-Content -Path $log
    throw "Keil build failed or did not finish (UV4 exit=$uv4ExitCode)."
}

Get-Content -Path $log
if ($null -ne $uv4ExitCode -and $uv4ExitCode -ne 0) {
    Write-Warning "UV4 returned $uv4ExitCode, but the build log confirms zero errors."
}
exit 0
