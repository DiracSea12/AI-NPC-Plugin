param(
    [int]$TimeoutSec = 3600,
    [string]$RunId
)

$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "TestResult.ps1")

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent (Split-Path -Parent $scriptRoot)
$uproject = Join-Path $repoRoot "VerifierHost.uproject"
$editor = "G:\UE5\UnrealEngine-AINpc\Engine\Binaries\Win64\UnrealEditor.exe"
$testRoot = Join-Path $repoRoot "Plugins/AINpc/Source"
$context = New-AINpcTestRunContext -Layer "editor-automation" -RunId $RunId
$reportDir = $context.RunDir
$logFile = Join-Path $reportDir "UnrealEditor.log"
$reportFile = Join-Path $reportDir "index.json"
$startTime = (Get-Date).ToUniversalTime()
$status = "PASS"
$failures = @()
$tests = @()
$reported = @()
$missing = @()
$problemTests = @()
$editorExitCode = $null
$commandArgs = @(
    $uproject,
    "-AbsLog=$logFile",
    "-ReportExportPath=$reportDir",
    "-TestExit=Automation Test Queue Empty",
    "-ExecCmds=Automation RunTests AINpc"
)
$command = New-AINpcCommandInfo -Executable $editor -Arguments $commandArgs -WorkingDirectory $repoRoot

try {
    if (-not (Test-Path $editor)) {
        $status = "BLOCKED"
        throw "UnrealEditor.exe not found at $editor"
    }

    if (-not (Test-Path $uproject)) {
        $status = "BLOCKED"
        throw "Project file not found at $uproject"
    }

    $buildArgs = @("-NoProfile", "-File", (Join-Path $scriptRoot "build-editor.ps1"))
    if (-not [string]::IsNullOrWhiteSpace($RunId)) { $buildArgs += @("-RunId", "$RunId-build") }
    $buildProcess = Start-Process -FilePath "pwsh" -ArgumentList $buildArgs -Wait -PassThru
    if ($buildProcess.ExitCode -ne 0) {
        $status = "BLOCKED"
        throw "build-editor.ps1 failed with exit code $($buildProcess.ExitCode)"
    }

    $testFiles = Get-ChildItem -Path $testRoot -Recurse -Filter "*.cpp" -File |
        Where-Object { $_.FullName -replace '\\', '/' -match '/Private/Tests/' }

    $testNames = New-Object System.Collections.Generic.HashSet[string]
    foreach ($file in $testFiles) {
        $content = Get-Content -Path $file.FullName -Raw
        foreach ($match in [regex]::Matches($content, 'IMPLEMENT_SIMPLE_AUTOMATION_TEST[\s\S]*?"(AINpc\.[^"]+)"')) {
            [void]$testNames.Add($match.Groups[1].Value)
        }
    }

    $tests = @($testNames | Sort-Object)
    if ($tests.Count -eq 0) {
        $status = "BLOCKED"
        throw "No AINpc automation tests discovered under $testRoot"
    }

    Write-Host "Discovered $($tests.Count) AINpc editor automation tests."
    foreach ($test in $tests) {
        Write-Host "  $test"
    }

    $arguments = @(
        "`"$uproject`"",
        "-AbsLog=`"$logFile`"",
        "-ReportOutputPath=`"$reportDir`"",
        "-TestExit=`"Automation Test Queue Empty`"",
        "-ExecCmds=`"Automation RunTests AINpc`""
    )

    Write-Host "Launching visible Unreal Editor automation run."
Write-Host "Editor automation is not final player-visible NPC behavior acceptance."
    $process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru
    if (-not $process.WaitForExit($TimeoutSec * 1000)) {
        Stop-Process -Id $process.Id -Force
        $status = "FAIL"
        throw "Unreal Editor automation timed out after $TimeoutSec seconds"
    }

    $editorExitCode = $process.ExitCode

    if (-not (Test-Path $reportFile)) {
        $status = "FAIL"
        throw "Automation report was not created: $reportFile"
    }

    $report = Get-Content -Path $reportFile -Raw | ConvertFrom-Json
    $failed = [int]$report.failed
    $notRun = [int]$report.notRun
    $inProcess = [int]$report.inProcess

    $reportedTests = New-Object System.Collections.Generic.HashSet[string]
    foreach ($test in @($report.tests)) {
        if ($test.fullTestPath) {
            [void]$reportedTests.Add([string]$test.fullTestPath)
        }
    }
    $reported = @($reportedTests | Sort-Object)

    $problemTests = @($report.tests | Where-Object { $_.state -ne "Success" -or $_.errors -ne 0 } | ForEach-Object {
        [ordered]@{
            fullTestPath = $_.fullTestPath
            state = $_.state
            warnings = $_.warnings
            errors = $_.errors
        }
    })

    if ($process.ExitCode -ne 0 -and $failed -eq 0 -and $notRun -eq 0 -and $inProcess -eq 0) {
        $status = "FAIL"
        throw "Unreal Editor exited with code $($process.ExitCode) despite a clean automation report. Report: $reportFile"
    }

    if ($failed -ne 0 -or $notRun -ne 0 -or $inProcess -ne 0) {
        if ($problemTests.Count -gt 0) {
            Write-Host "Problem tests:"
            foreach ($test in $problemTests) {
                Write-Host "  $($test.fullTestPath) State=$($test.state) Warnings=$($test.warnings) Errors=$($test.errors)"
            }
        }

        $status = "FAIL"
        throw "Unreal Editor automation did not complete cleanly. Failed=$failed NotRun=$notRun InProcess=$inProcess Report: $reportFile"
    }

    $missing = @($tests | Where-Object { -not $reportedTests.Contains($_) })
    if ($missing.Count -ne 0) {
        $status = "FAIL"
        throw "Automation report is missing discovered tests: $($missing -join ', '). Report: $reportFile"
    }

    Write-Host "PASS: all discovered editor automation tests completed"
    Write-Host "Report: $reportFile"
    Write-Host "Log: $logFile"
}
catch {
    if ($status -eq "PASS") {
        $status = "FAIL"
    }
    $failures += [ordered]@{
        id = "editor-automation"
        message = ConvertTo-AINpcRedactedErrorSummary -ErrorRecord $_
        artifact = ConvertTo-AINpcRepoRelativePath $reportFile
    }
    Write-Host "FAIL: editor automation completed with status $status"
    Write-Host "Error: $($failures[-1].message)"
}
finally {
    $endTime = (Get-Date).ToUniversalTime()
    Write-AINpcTestResult -Context $context -Status $status -StartTimeUtc $startTime -EndTimeUtc $endTime -Command $command -Artifacts ([ordered]@{
        report = ConvertTo-AINpcRepoRelativePath $reportFile
        log = ConvertTo-AINpcRepoRelativePath $logFile
        result = ConvertTo-AINpcRepoRelativePath $context.ResultPath
        unrealEditor = $editor
    }) -Diagnostics ([ordered]@{
        timeoutSec = $TimeoutSec
        editorExitCode = $editorExitCode
        discoveredCount = $tests.Count
        reportedCount = $reported.Count
        missingCount = $missing.Count
        problemCount = $problemTests.Count
    }) -Observations ([ordered]@{
        discoveredTests = @($tests)
        reportedTests = @($reported)
        missingTests = @($missing)
        problemTests = @($problemTests)
    }) -Failures $failures | Out-Null
}

if ($status -eq "PASS") {
    exit 0
}
exit 1
