param(
    [string]$RunId
)

$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "TestResult.ps1")

function ConvertTo-AINpcNormalizedRelativePath {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) {
        return ""
    }
    return ([string]$Path).Replace('\', '/').TrimStart('./')
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$gameTestRoot = Join-Path $scriptRoot "game"
$manifestPath = Join-Path $gameTestRoot "visual-tests.json"
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
$testScripts = @()

try {
    if (-not (Test-Path $manifestPath)) {
        throw "Visual game manifest not found: $manifestPath"
    }
    $manifestEntries = @(Get-Content -Path $manifestPath -Raw | ConvertFrom-Json)
}
catch {
    $status = "BLOCKED"
    $failures += [ordered]@{
        id = "visual-manifest"
        message = ConvertTo-AINpcRedactedErrorSummary -ErrorRecord $_
        artifact = ConvertTo-AINpcRepoRelativePath $manifestPath
    }
}

try {
    if (-not (Test-Path $gameTestRoot)) {
        throw "Visual game script directory not found: $gameTestRoot"
    }
    $testScripts = @(Get-ChildItem -Path $gameTestRoot -Filter "test-*.ps1" -File | Sort-Object Name)
    if ($testScripts.Count -eq 0) {
        throw "No visual game scripts discovered under $gameTestRoot"
    }
}
catch {
    $status = "BLOCKED"
    $failures += [ordered]@{
        id = "visual-discovery"
        message = ConvertTo-AINpcRedactedErrorSummary -ErrorRecord $_
        artifact = ConvertTo-AINpcRepoRelativePath $gameTestRoot
    }
}

if ($status -eq "PASS") {
    $discoveredRelativeScripts = @($testScripts | ForEach-Object { ConvertTo-AINpcNormalizedRelativePath (ConvertTo-AINpcRepoRelativePath $_.FullName) })
    foreach ($entry in $manifestEntries) {
        $manifestScript = ConvertTo-AINpcNormalizedRelativePath ([string]$entry.script)
        if ($discoveredRelativeScripts -notcontains $manifestScript) {
            $status = "BLOCKED"
            $failures += [ordered]@{
                id = "visual-manifest-discovery"
                message = "Visual manifest references a script that dynamic discovery did not find: $manifestScript"
                artifact = ConvertTo-AINpcRepoRelativePath $manifestPath
            }
        }
    }
}

if ($status -eq "PASS") {
    foreach ($script in $testScripts) {
        $scriptPath = $script.FullName
        $scriptRelativePath = ConvertTo-AINpcNormalizedRelativePath (ConvertTo-AINpcRepoRelativePath $scriptPath)
        $matches = @($manifestEntries | Where-Object { (ConvertTo-AINpcNormalizedRelativePath ([string]$_.script)) -eq $scriptRelativePath })
        if ($matches.Count -ne 1) {
            $status = "FAIL"
            $failures += [ordered]@{
                id = "visual-manifest-match"
                message = "Expected exactly one visual manifest entry for dynamically discovered script '$scriptRelativePath', found $($matches.Count)."
                artifact = $scriptRelativePath
            }
            continue
        }

        $entry = $matches[0]
        $testId = [string]$entry.testId
        $testStart = (Get-Date).ToUniversalTime()
        $testStatus = "PASS"
        $exitCode = 0
        $message = ""
        $scriptResultPath = $null
        $runtimeResultPath = $null
        $runtimeResult = $null

        Write-Host "Running visual game test: $testId"
        try {
            $childRunId = "$($context.RunId)-$($testId -replace '[^A-Za-z0-9._-]', '-')"
            $scriptResultPath = Join-Path (Join-Path (Join-Path (Get-AINpcRepoRoot) "Saved/TestLogs") "visual-game") (Join-Path $childRunId "result.json")
            $process = Start-Process -FilePath "pwsh" -ArgumentList @("-NoProfile", "-File", $scriptPath, "-RunId", $childRunId) -Wait -PassThru
            $exitCode = $process.ExitCode

            if (-not (Test-Path $scriptResultPath)) {
                throw "$testId did not emit the expected visual-game result artifact: $scriptResultPath"
            }

            try {
                $scriptResult = Get-Content -Path $scriptResultPath -Raw | ConvertFrom-Json
            }
            catch {
                throw "$testId emitted malformed script result JSON: $scriptResultPath"
            }

            $testStatus = [string]$scriptResult.status
            if ([string]$scriptResult.testId -ne $testId) {
                throw "$testId script result identity mismatch: $($scriptResult.testId)"
            }

            $runtimeResultPath = [string]$scriptResult.artifacts.runtimeResult
            if ([string]::IsNullOrWhiteSpace($runtimeResultPath)) {
                throw "$testId script result did not reference artifacts.runtimeResult."
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
            foreach ($requiredObservation in @($entry.requiredObservations)) {
                $observationName = [string]$requiredObservation
                if ([string]::IsNullOrWhiteSpace($observationName)) {
                    continue
                }
                if (-not ($runtimeResult.observations.PSObject.Properties.Name -contains $observationName)) {
                    throw "$testId runtime result missing required observation '$observationName'."
                }
                if (-not [bool]$runtimeResult.observations.$observationName) {
                    throw "$testId required observation '$observationName' was not satisfied."
                }
            }
            if (@($entry.allowedTerminalOutcomes) -notcontains [string]$runtimeResult.status) {
                throw "$testId runtime result status '$($runtimeResult.status)' is not allowed."
            }
            if ($exitCode -ne 0 -or $testStatus -ne "PASS") {
                throw "$testId script status $testStatus exitCode $exitCode."
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
                artifact = ConvertTo-AINpcRepoRelativePath $(if ($runtimeResultPath) { $runtimeResultPath } else { $scriptResultPath })
            }
        }
        finally {
            $global:LASTEXITCODE = 0
        }

        $testEnd = (Get-Date).ToUniversalTime()
        $runtimeArtifacts += [ordered]@{
            testId = $testId
            runtimeResult = ConvertTo-AINpcRepoRelativePath $runtimeResultPath
            scriptResult = ConvertTo-AINpcRepoRelativePath $scriptResultPath
            status = $(if ($runtimeResult) { [string]$runtimeResult.status } else { $testStatus })
        }
        $testResults += [ordered]@{
            id = $testId
            script = ConvertTo-AINpcRepoRelativePath $scriptPath
            status = $testStatus
            exitCode = $exitCode
            durationMs = [int][Math]::Max(0, [Math]::Round(($testEnd - $testStart).TotalMilliseconds))
            message = $message
            runtimeResult = ConvertTo-AINpcRepoRelativePath $runtimeResultPath
            scriptResult = ConvertTo-AINpcRepoRelativePath $scriptResultPath
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
    discoveredCount = $testScripts.Count
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
