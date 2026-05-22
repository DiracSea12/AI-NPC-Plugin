param(
    [string]$RunId
)

$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "TestResult.ps1")
. (Join-Path $PSScriptRoot "game/VisualGameHarness.ps1")

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$gameTestRoot = Join-Path $scriptRoot "game"
$manifestPath = Join-Path (Get-AINpcRepoRoot) "Config/AINpcVisualScenarios.json"
$context = New-AINpcTestRunContext -Layer "visual-game" -RunId $RunId
$startTime = (Get-Date).ToUniversalTime()
$testResults = @()
$runtimeArtifacts = @()
$failures = @()
$status = "PASS"
$commandArgs = @("-File", $MyInvocation.MyCommand.Path)
if (-not [string]::IsNullOrWhiteSpace($RunId)) { $commandArgs += @("-RunId", $RunId) }
$command = New-AINpcCommandInfo -Executable "pwsh" -Arguments $commandArgs -WorkingDirectory (Get-AINpcRepoRoot)
$manifestEntries = @()

try {
    if (-not (Test-Path $manifestPath)) {
        throw "Visual scenario config not found: $manifestPath"
    }
    $manifestEntries = @(Get-Content -Path $manifestPath -Raw | ConvertFrom-Json)
    if ($manifestEntries.Count -eq 0) {
        throw "No entries found in visual scenario config: $manifestPath"
    }
}
catch {
    $status = "BLOCKED"
    $failures += [ordered]@{
        id = "visual-scenarios"
        message = ConvertTo-AINpcRedactedErrorSummary -ErrorRecord $_
        artifact = ConvertTo-AINpcRepoRelativePath $manifestPath
    }
}

if ($status -eq "PASS") {
    foreach ($entry in $manifestEntries) {
        $testId = [string]$entry.testId
        $testStart = (Get-Date).ToUniversalTime()
        $testStatus = "PASS"
        $exitCode = 0
        $message = ""
        $childResultPath = $null
        $runtimeResultPath = $null
        $runtimeResult = $null

        Write-Host "Running visual game test: $testId"
        try {
            $childRunId = "$($context.RunId)-$($testId -replace '[^A-Za-z0-9._-]', '-')"
            $childResultPath = Join-Path (Join-Path (Join-Path (Get-AINpcRepoRoot) "Saved/TestLogs") "visual-game") (Join-Path $childRunId "result.json")

            $harnessArgs = @(
                "-NoProfile", "-File", (Join-Path $gameTestRoot "VisualGameHarness.ps1"),
                "-TestId", $testId,
                "-RunId", $childRunId
            )
            $process = Start-Process -FilePath "pwsh" -ArgumentList $harnessArgs -Wait -PassThru
            $exitCode = $process.ExitCode

            if (-not (Test-Path $childResultPath)) {
                throw "$testId did not emit the expected visual-game result artifact: $childResultPath"
            }

            try {
                $childResult = Get-Content -Path $childResultPath -Raw | ConvertFrom-Json
            }
            catch {
                throw "$testId emitted malformed result JSON: $childResultPath"
            }

            $testStatus = [string]$childResult.status
            $runtimeResultPath = [string]$childResult.artifacts.runtimeResult
            if ([string]::IsNullOrWhiteSpace($runtimeResultPath)) {
                throw "$testId result did not reference artifacts.runtimeResult."
            }
            if (-not [System.IO.Path]::IsPathRooted($runtimeResultPath)) {
                $runtimeResultPath = Join-Path (Get-AINpcRepoRoot) $runtimeResultPath
            }
            if (-not (Test-Path $runtimeResultPath)) {
                throw "$testId runtime result JSON is missing: $runtimeResultPath"
            }

            try {
                $runtimeResult = Get-Content -Path $runtimeResultPath -Raw | ConvertFrom-Json
            }
            catch {
                throw "$testId runtime result JSON is malformed: $runtimeResultPath"
            }

            $requiredRuntimeFields = @('schemaVersion', 'runId', 'layer', 'testId', 'storyIds', 'phaseIds', 'status', 'startTimeUtc', 'endTimeUtc', 'durationMs', 'command', 'artifacts', 'diagnostics', 'observations', 'failures')
            foreach ($field in $requiredRuntimeFields) {
                if (-not ($runtimeResult.PSObject.Properties.Name -contains $field)) {
                    throw "$testId runtime result JSON missing required field '$field'."
                }
            }
            if ([int]$runtimeResult.schemaVersion -ne 1 -or [string]$runtimeResult.layer -ne "visual-game" -or [string]$runtimeResult.testId -ne $testId) {
                throw "$testId runtime result JSON identity fields are invalid."
            }
            foreach ($observationName in @($entry.requiredObservations)) {
                $name = [string]$observationName
                if ([string]::IsNullOrWhiteSpace($name)) { continue }
                if (-not ($runtimeResult.observations.PSObject.Properties.Name -contains $name)) {
                    throw "$testId runtime result missing required observation '$name'."
                }
                if (-not [bool]$runtimeResult.observations.$name) {
                    throw "$testId required observation '$name' was not satisfied."
                }
            }
            if (@($entry.allowedTerminalOutcomes) -notcontains [string]$runtimeResult.status) {
                throw "$testId runtime result status '$($runtimeResult.status)' is not allowed."
            }
            if ($exitCode -ne 0 -or $testStatus -ne "PASS") {
                throw "$testId status $testStatus exitCode $exitCode."
            }
        }
        catch {
            $testStatus = "FAIL"
            $exitCode = $(if ($exitCode -ne 0) { $exitCode } else { 1 })
            $message = ConvertTo-AINpcRedactedErrorSummary -ErrorRecord $_
            $status = "FAIL"
            $failures += [ordered]@{
                id = $testId
                message = $message
                artifact = ConvertTo-AINpcRepoRelativePath $(if ($runtimeResultPath) { $runtimeResultPath } else { $childResultPath })
            }
        }
        finally {
            $global:LASTEXITCODE = 0
        }

        $testEnd = (Get-Date).ToUniversalTime()
        $runtimeArtifacts += [ordered]@{
            testId = $testId
            runtimeResult = ConvertTo-AINpcRepoRelativePath $runtimeResultPath
            childResult = ConvertTo-AINpcRepoRelativePath $childResultPath
            status = $(if ($runtimeResult) { [string]$runtimeResult.status } else { $testStatus })
        }
        $testResults += [ordered]@{
            id = $testId
            status = $testStatus
            exitCode = $exitCode
            durationMs = [int][Math]::Max(0, [Math]::Round(($testEnd - $testStart).TotalMilliseconds))
            message = $message
            runtimeResult = ConvertTo-AINpcRepoRelativePath $runtimeResultPath
        }
    }
}

$endTime = (Get-Date).ToUniversalTime()
Write-AINpcTestResult -Context $context -Status $status -StartTimeUtc $startTime -EndTimeUtc $endTime -Command $command -Artifacts ([ordered]@{
    result = ConvertTo-AINpcRepoRelativePath $context.ResultPath
    manifest = ConvertTo-AINpcRepoRelativePath $manifestPath
    runtimeResults = @($runtimeArtifacts | ForEach-Object { $_.runtimeResult })
}) -Diagnostics ([ordered]@{
    manifestCount = $manifestEntries.Count
    failedCount = @($testResults | Where-Object { $_.status -eq "FAIL" }).Count
}) -Observations ([ordered]@{
    tests = @($testResults)
    runtimeArtifacts = @($runtimeArtifacts)
}) -Failures $failures | Out-Null

if ($status -ne "PASS") {
    Write-Host "FAIL: visual game suite had $($failures.Count) failing or blocked test(s)"
    foreach ($failure in $failures) {
        Write-Host "  $($failure.id): $($failure.message)"
    }
    exit 1
}

Write-Host "PASS: visual game suite completed"
exit 0
