param(
    [int]$TimeoutSec = 180,
    [int]$HoldSeconds = 8,
    [string]$TestId = "us1.dialogue-action",
    [double]$ActionObservationHoldSeconds = 3.0,
    [switch]$AllowExistingUEProcess,
    [switch]$ValidateOnly,
    [string]$RunId
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "VisualGameHarness.ps1")

Invoke-AINpcVisualGameTest -TestId $TestId -TimeoutSec $TimeoutSec -HoldSeconds $HoldSeconds -ActionObservationHoldSeconds $ActionObservationHoldSeconds -AllowExistingUEProcess:$AllowExistingUEProcess -ValidateOnly:$ValidateOnly -RunId $RunId
