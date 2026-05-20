$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path

& (Join-Path $scriptRoot "build-editor.ps1")
if ($LASTEXITCODE -ne 0) {
    throw "build-editor.ps1 failed with exit code $LASTEXITCODE"
}
& (Join-Path $scriptRoot "verify-baseurl-contract.ps1")
& (Join-Path $scriptRoot "verify-sse-contract.ps1")

Write-Host "PASS: fast AI NPC checks completed"
