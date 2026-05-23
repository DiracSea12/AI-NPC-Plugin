[CmdletBinding()]
param(
    [string]$RunId
)

$ErrorActionPreference = "Continue"
$requestedRunId = $RunId
. (Join-Path $PSScriptRoot "TestResult.ps1")
. (Join-Path $PSScriptRoot "game/VisualGameHarness.ps1")

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$gameTestRoot = Join-Path $scriptRoot "game"
$repoRoot = Get-AINpcRepoRoot
$manifestPath = Join-Path $repoRoot "Config/AINpcVisualScenarios.json"
$RunId = $requestedRunId
$context = New-AINpcTestRunContext -Layer "visual-game" -RunId $RunId
$startTime = (Get-Date).ToUniversalTime()
$testResults = @()
$runtimeArtifacts = @()
$failures = @()
$status = "PASS"
$manifestEntries = @()
$commandArgs = @("-File", $MyInvocation.MyCommand.Path)
if (-not [string]::IsNullOrWhiteSpace($RunId)) { $commandArgs += @("-RunId", $RunId) }
$command = New-AINpcCommandInfo -Executable "pwsh" -Arguments $commandArgs -WorkingDirectory $repoRoot

function Get-AINpcSanitizedRunIdFragment([string]$Value) {
    return ($Value -replace '[^A-Za-z0-9._-]', '-')
}

try {
    if (-not (Test-Path $manifestPath)) { throw "Visual scenario config not found: $manifestPath" }
    $manifestEntries = @(Get-Content -Path $manifestPath -Raw | ConvertFrom-Json)
    if ($manifestEntries.Count -eq 0) { throw "No entries found in visual scenario config: $manifestPath" }

    $testIds = @($manifestEntries | ForEach-Object { [string]$_.testId })
    foreach ($group in $testIds | Group-Object) {
        if ([string]::IsNullOrWhiteSpace($group.Name) -or $group.Count -ne 1) { throw "Visual scenario TestId must be unique and non-empty: '$($group.Name)' count=$($group.Count)." }
    }
    $maps = @($manifestEntries | ForEach-Object { [string]$_.map } | Select-Object -Unique)
    if ($maps.Count -ne 1) { throw "Visual game suite requires all scenarios to use one map; found: $($maps -join ', ')" }
}
catch {
    $status = "BLOCKED"
    $failures += [ordered]@{
        id = "visual-scenarios"
        message = ConvertTo-AINpcRedactedErrorSummary -ErrorRecord $_
        artifact = ConvertTo-AINpcRepoRelativePath $manifestPath
    }
}

$suiteExitCode = 0
if ($status -eq "PASS") {
    $suiteStart = (Get-Date).ToUniversalTime()
    $testIds = @($manifestEntries | ForEach-Object { [string]$_.testId })
    $suiteExitCode = Invoke-AINpcVisualGameSuite -TestIds $testIds -RunId $context.RunId -ResultDir (Join-Path (Join-Path $repoRoot "Saved/TestLogs") "visual-game")
    $suiteEnd = (Get-Date).ToUniversalTime()

    if ($suiteExitCode -ne 0) { $status = "FAIL" }

    foreach ($entry in $manifestEntries) {
        $testId = [string]$entry.testId
        $expectedRunId = "$($context.RunId)-$(Get-AINpcSanitizedRunIdFragment $testId)"
        $runtimeResultPath = Join-Path (Join-Path (Join-Path $repoRoot "Saved/TestLogs") "visual-game") (Join-Path $expectedRunId "runtime-result.json")
        $testStatus = "PASS"
        $message = ""
        $runtimeResult = $null
        $durationMs = [int][Math]::Max(0, [Math]::Round(($suiteEnd - $suiteStart).TotalMilliseconds))

        try {
            $runtimeResult = Test-AINpcVisualRuntimeResult -ResultPath $runtimeResultPath -ManifestEntry $entry -ProcessExitCode $suiteExitCode -ExpectedRunId $expectedRunId
            $durationMs = [int]$runtimeResult.durationMs
        }
        catch {
            $testStatus = "FAIL"
            $message = ConvertTo-AINpcRedactedErrorSummary -ErrorRecord $_
            $status = "FAIL"
            $failures += [ordered]@{
                id = $testId
                message = $message
                artifact = ConvertTo-AINpcRepoRelativePath $runtimeResultPath
            }
        }

        $runtimeArtifacts += [ordered]@{
            testId = $testId
            runtimeResult = ConvertTo-AINpcRepoRelativePath $runtimeResultPath
            status = $(if ($runtimeResult) { [string]$runtimeResult.status } else { $testStatus })
        }
        $testResults += [ordered]@{
            id = $testId
            status = $testStatus
            exitCode = $suiteExitCode
            durationMs = $durationMs
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
    suiteExitCode = $suiteExitCode
    editorLaunchCount = $(if ($manifestEntries.Count -gt 0) { 1 } else { 0 })
}) -Observations ([ordered]@{
    tests = @($testResults)
    runtimeArtifacts = @($runtimeArtifacts)
}) -Failures $failures -PhaseIds @($manifestEntries | ForEach-Object { @($_.phaseIds) } | Select-Object -Unique) | Out-Null

if ($status -ne "PASS") {
    Write-Host "FAIL: visual game suite had $($failures.Count) failing or blocked test(s)"
    foreach ($failure in $failures) {
        Write-Host "  $($failure.id): $($failure.message)"
    }
    exit 1
}

Write-Host "PASS: visual game suite completed"
exit 0
