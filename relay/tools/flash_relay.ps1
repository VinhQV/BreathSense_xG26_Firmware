param(
    [Parameter(Mandatory=$true)][string]$SerialNumber,
    [string]$Commander = 'C:/SiliconLabs/Commander/commander.exe'
)
$ErrorActionPreference = 'Stop'
$relayRoot = Split-Path -Parent $PSScriptRoot
$firmware = Join-Path $relayRoot 'cmake_gcc/build/base/BreathSense_Relay.s37'
if (!(Test-Path -LiteralPath $firmware)) { throw 'Build BreathSense_Relay first.' }
$probe = & $Commander adapter probe --serialno $SerialNumber 2>&1
if ($LASTEXITCODE -ne 0 -or ($probe -join "`n") -notmatch 'BRD2709A') {
    throw "Expected BRD2709A Explorer Kit. Refusing to flash a different board. $probe"
}
$probe | Write-Output
& $Commander flash $firmware --serialno $SerialNumber --device EFR32MG26B510F3200IM48
if ($LASTEXITCODE -ne 0) { throw 'Relay flash failed.' }
