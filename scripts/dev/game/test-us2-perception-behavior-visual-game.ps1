param(
    [int]$TimeoutSec = 240,
    [int]$HoldSeconds = 10,
    [double]$ActionObservationHoldSeconds = 3.0,
    [switch]$AllowExistingUEProcess,
    [switch]$ValidateOnly,
    [string]$RunId
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "VisualGameHarness.ps1")

Invoke-AINpcVisualGameTest -TestId "us2.perception-behavior" -TimeoutSec $TimeoutSec -HoldSeconds $HoldSeconds -ActionObservationHoldSeconds $ActionObservationHoldSeconds -AllowExistingUEProcess:$AllowExistingUEProcess -ValidateOnly:$ValidateOnly -RunId $RunId
