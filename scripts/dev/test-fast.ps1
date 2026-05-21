$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path

Write-Host "Running fast AI NPC checks: build + static only."
Write-Host "This is not provider connectivity acceptance and not visual runtime/NPC behavior acceptance."

& (Join-Path $scriptRoot "build-editor.ps1")
if ($LASTEXITCODE -ne 0) {
    throw "build-editor.ps1 failed with exit code $LASTEXITCODE"
}

& (Join-Path $scriptRoot "test-static.ps1")
if ($LASTEXITCODE -ne 0) {
    throw "test-static.ps1 failed with exit code $LASTEXITCODE"
}

Write-Host "PASS: fast AI NPC checks completed"
Write-Host "Reminder: provider connectivity and visual runtime acceptance were not run."
