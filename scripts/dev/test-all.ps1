param(
    [string]$RunId
)

$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "TestResult.ps1")

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent (Split-Path -Parent $scriptRoot)
$context = New-AINpcTestRunContext -Layer "full-suite" -RunId $RunId
$startTime = (Get-Date).ToUniversalTime()
$layers = @()
$failures = @()
$suitePhaseIds = New-Object System.Collections.Generic.HashSet[string]
$status = "PASS"
$commandArgs = @("-File", $MyInvocation.MyCommand.Path)
if (-not [string]::IsNullOrWhiteSpace($RunId)) { $commandArgs += @("-RunId", $RunId) }
$command = New-AINpcCommandInfo -Executable "pwsh" -Arguments $commandArgs -WorkingDirectory $repoRoot

$steps = @(
    @{ Name = "build"; Layer = "build"; Script = "build-editor.ps1" },
    @{ Name = "static verification"; Layer = "static"; Script = "test-static.ps1" },
    @{ Name = "editor-context automation"; Layer = "editor-automation"; Script = "test-editor-context.ps1" },
    @{ Name = "provider connectivity"; Layer = "provider-connectivity"; Script = "test-provider.ps1" },
    @{ Name = "game tests"; Layer = "visual-game"; Script = "test-game.ps1" }
)

foreach ($step in $steps) {
    Write-Host "Running $($step.Name): $($step.Script)"
    $stepStart = (Get-Date).ToUniversalTime()
    $exitCode = 0
    $stepStatus = "PASS"
    $message = ""
    $resultPath = $null
    try {
        $global:LASTEXITCODE = 0
        $childRunId = "$($context.RunId)-$($step.Layer)"
        $resultPath = Join-Path (Join-Path (Join-Path $repoRoot "Saved/TestLogs") $step.Layer) (Join-Path $childRunId "result.json")
        $process = Start-Process -FilePath "pwsh" -ArgumentList @("-NoProfile", "-File", (Join-Path $scriptRoot $step.Script), "-RunId", $childRunId) -Wait -PassThru
        $exitCode = $process.ExitCode
        if ($exitCode -ne 0) {
            $stepStatus = "FAIL"
            $message = "$($step.Script) exited with code $exitCode"
        }
    }
    catch {
        $stepStatus = "FAIL"
        $exitCode = 1
        $message = ConvertTo-AINpcRedactedErrorSummary -ErrorRecord $_
    }
    finally {
        $global:LASTEXITCODE = 0
    }

    if ($resultPath -and (Test-Path $resultPath)) {
        try {
            $layerResult = Get-Content -Path $resultPath -Raw | ConvertFrom-Json
            if ($exitCode -eq 0) {
                $stepStatus = [string]$layerResult.status
            }
            foreach ($phaseId in @($layerResult.phaseIds)) {
                if (-not [string]::IsNullOrWhiteSpace([string]$phaseId)) { [void]$suitePhaseIds.Add([string]$phaseId) }
            }
        }
        catch {
            $stepStatus = "FAIL"
            $message = "Layer result artifact is malformed: $resultPath"
        }
    }
    else {
        $stepStatus = "FAIL"
        $message = "Layer did not emit result artifact for $($step.Layer)"
    }

    $stepEnd = (Get-Date).ToUniversalTime()
    $layers += [ordered]@{
        name = $step.Name
        layer = $step.Layer
        script = ConvertTo-AINpcRepoRelativePath (Join-Path $scriptRoot $step.Script)
        status = $stepStatus
        exitCode = $exitCode
        result = ConvertTo-AINpcRepoRelativePath $resultPath
        durationMs = [int][Math]::Max(0, [Math]::Round(($stepEnd - $stepStart).TotalMilliseconds))
        message = $message
    }

    if ($stepStatus -eq "FAIL" -or $stepStatus -eq "BLOCKED") {
        $status = "FAIL"
        $failures += [ordered]@{
            id = $step.Layer
            message = $(if ([string]::IsNullOrWhiteSpace($message)) { "$($step.Name) status $stepStatus" } else { $message })
            artifact = ConvertTo-AINpcRepoRelativePath $resultPath
        }
    }
}

$endTime = (Get-Date).ToUniversalTime()
Write-AINpcTestResult -Context $context -Status $status -StartTimeUtc $startTime -EndTimeUtc $endTime -Command $command -Artifacts ([ordered]@{
    result = ConvertTo-AINpcRepoRelativePath $context.ResultPath
}) -Diagnostics ([ordered]@{
    layerCount = $layers.Count
    failedOrBlockedCount = $failures.Count
}) -Observations ([ordered]@{
    layers = @($layers)
}) -Failures $failures -PhaseIds @($suitePhaseIds) | Out-Null

if ($status -ne "PASS") {
    Write-Host "FAIL: full AI NPC test suite had $($failures.Count) failing or blocked layer(s)"
    foreach ($failure in $failures) {
        Write-Host "  $($failure.id): $($failure.message)"
    }
    exit 1
}

Write-Host "PASS: full AI NPC test suite completed"
exit 0
