param(
    [string]$TestId,
    [string[]]$TestIds = @(),
    [int]$TimeoutSec = 0,
    [int]$HoldSeconds = 8,
    [double]$ActionObservationHoldSeconds = 3.0,
    [switch]$AllowExistingUEProcess,
    [switch]$ValidateOnly,
    [string]$RunId,
    [string]$ResultDir
)

$ErrorActionPreference = "Stop"
. (Join-Path (Split-Path -Parent $PSScriptRoot) "TestResult.ps1")

function Get-AINpcVisualScenarioConfigPath {
    return Join-Path (Get-AINpcRepoRoot) "Config/AINpcVisualScenarios.json"
}

function Get-AINpcVisualTestManifest {
    $manifestPath = Get-AINpcVisualScenarioConfigPath
    if (-not (Test-Path $manifestPath)) {
        throw "Visual scenario config not found: $manifestPath"
    }

    return @(Get-Content -Path $manifestPath -Raw | ConvertFrom-Json)
}

function Get-AINpcVisualTestEntry {
    param([Parameter(Mandatory = $true)][string]$TestId)

    $matches = @(Get-AINpcVisualTestManifest | Where-Object { $_.testId -eq $TestId })
    if ($matches.Count -ne 1) {
        throw "Expected exactly one visual scenario entry for '$TestId', found $($matches.Count)."
    }

    return $matches[0]
}

function Resolve-AINpcGameMapFile {
    param([Parameter(Mandatory = $true)][string]$MapPath)

    if (-not $MapPath.StartsWith("/Game/")) {
        throw "Visual scenario map must be a /Game asset path, got '$MapPath'."
    }

    $relative = $MapPath.Substring("/Game/".Length).TrimStart('/')
    if ([string]::IsNullOrWhiteSpace($relative)) {
        throw "Visual scenario map path is empty after /Game/: '$MapPath'."
    }

    return Join-Path (Join-Path (Get-AINpcRepoRoot) "Content") ($relative + ".umap")
}

function Write-AINpcVisualLogTail {
    param(
        [AllowNull()][string]$LogPath,
        [int]$Tail = 100
    )

    if (-not [string]::IsNullOrWhiteSpace($LogPath) -and (Test-Path $LogPath)) {
        Write-Host "---- visual game log tail (last $Tail lines) ----"
        Get-Content -Path $LogPath -Tail $Tail | ForEach-Object { Write-Host (ConvertTo-AINpcRedactedText -Text ([string]$_) -MaxLength 1200) }
        Write-Host "---- end log tail ----"

        Write-Host "---- visual game diagnostics ----"
        Select-String -Path $LogPath -Pattern 'AINpc Visual Test|AINpc visual result|provider|config|http|Error|Fatal|ensure' -CaseSensitive:$false |
            Select-Object -Last 160 |
            ForEach-Object { Write-Host (ConvertTo-AINpcRedactedText -Text ([string]$_.Line) -MaxLength 1200) }
        Write-Host "---- end diagnostics ----"
    }
}

function Assert-AINpcVisibleLaunchContract {
    param(
        [Parameter(Mandatory = $true)][string]$EditorPath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [switch]$HiddenWindow
    )

    $editorName = [System.IO.Path]::GetFileName($EditorPath)
    if ($editorName -ne "UnrealEditor.exe") {
        throw "Visual acceptance must launch UnrealEditor.exe directly, got '$editorName'."
    }

    if ($HiddenWindow) {
        throw "Visual acceptance cannot request hidden window style."
    }

    $joinedArgs = ($Arguments -join " ")
    $forbiddenPatterns = @(
        'UnrealEditor-Cmd',
        '(?i)(^|\s)-nullrhi(\s|$)',
        '(?i)(^|\s)-unattended(\s|$)',
        '(?i)mock\s*provider',
        '(?i)(^|\s)-AINpcMockProvider(\s|=|$)',
        '(?i)(^|\s)-AINpcInjectedResponse(\s|=|$)',
        '(?i)(^|\s)-AINpc.*Bypass(\s|=|$)',
        '(?i)SetDialogueDispatchBypassForTest',
        '(?i)HandleRequestCompletedForTest'
    )

    foreach ($pattern in $forbiddenPatterns) {
        if ($EditorPath -match $pattern -or $joinedArgs -match $pattern) {
            throw "Forbidden final visual acceptance mode detected: $pattern"
        }
    }

    if (@($Arguments | Where-Object { $_ -eq "-game" }).Count -ne 1) {
        throw "Visual acceptance must launch with exactly one -game argument."
    }
}

function Assert-AINpcSingleVisibleInstance {
    param([switch]$AllowExistingUEProcess)

    $existingUE = @(Get-Process -Name "UnrealEditor" -ErrorAction SilentlyContinue)
    if ($existingUE.Count -gt 0 -and -not $AllowExistingUEProcess) {
        $processSummary = ($existingUE | ForEach-Object { "PID=$($_.Id) Path=$($_.Path)" }) -join "; "
        throw "Pre-run UE process check failed. Existing UnrealEditor process(es) detected: $processSummary. Close them or pass -AllowExistingUEProcess deliberately."
    }
}

function Get-AINpcVisualRuntimeFailureSummary($RuntimeResult) {
    $parts = @()
    foreach ($failure in @($RuntimeResult.failures)) {
        if ($failure.message) { $parts += [string]$failure.message }
        elseif ($failure.failureReason) { $parts += [string]$failure.failureReason }
        elseif ($failure) { $parts += [string]$failure }
    }
    foreach ($step in @($RuntimeResult.stepDiagnostics)) {
        if ($step.status -and [string]$step.status -ne 'PASS' -and [string]$step.status -ne 'pass' -and [string]$step.status -ne 'completed') {
            if ($step.failureReason) { $parts += "step[$($step.stepIndex)] $($step.stepType): $($step.failureReason)" }
        }
    }
    if ($RuntimeResult.diagnostics.failureReason) { $parts += [string]$RuntimeResult.diagnostics.failureReason }
    if ($parts.Count -eq 0) { return '<no runtime failure details>' }
    return ($parts | Select-Object -First 4) -join ' | '
}

function Test-AINpcVisualRuntimeResult {
    param(
        [Parameter(Mandatory = $true)][string]$ResultPath,
        [Parameter(Mandatory = $true)]$ManifestEntry,
        [int]$ProcessExitCode = 0,
        [string]$ExpectedRunId
    )

    if (-not (Test-Path $ResultPath)) {
        throw "Visual runtime result JSON was not created: $ResultPath"
    }

    try {
        $result = Get-Content -Path $ResultPath -Raw | ConvertFrom-Json
    }
    catch {
        throw "Visual runtime result JSON is malformed: $ResultPath. $($_.Exception.Message)"
    }

    $requiredTopLevel = @('schemaVersion', 'runId', 'layer', 'testId', 'storyIds', 'phaseIds', 'status', 'startTimeUtc', 'endTimeUtc', 'durationMs', 'command', 'artifacts', 'diagnostics', 'observations', 'failures')
    foreach ($field in $requiredTopLevel) {
        if (-not ($result.PSObject.Properties.Name -contains $field)) {
            throw "Visual runtime result JSON missing required field '$field': $ResultPath"
        }
    }

    if ([int]$result.schemaVersion -ne 1) { throw "Visual runtime result schemaVersion must be 1." }
    if ([string]$result.layer -ne "visual-game") { throw "Visual runtime result layer must be visual-game." }
    if ([string]$result.testId -ne [string]$ManifestEntry.testId) { throw "Visual runtime result testId mismatch. Expected '$($ManifestEntry.testId)', got '$($result.testId)'." }
    $runtimeExpectedRunId = $(if ([string]::IsNullOrWhiteSpace($ExpectedRunId)) { $RunId } else { $ExpectedRunId })
    if ([string]$result.runId -ne [string]$runtimeExpectedRunId) { throw "Visual runtime result runId mismatch. Expected '$runtimeExpectedRunId', got '$($result.runId)'." }
    if ([string]$result.status -ne "PASS") { throw "Visual runtime result status '$($result.status)' is not PASS for '$($ManifestEntry.testId)'. $(Get-AINpcVisualRuntimeFailureSummary $result)" }
    if ($ProcessExitCode -ne 0) { throw "Visual runtime process exited with code $ProcessExitCode." }

    foreach ($storyId in @($ManifestEntry.storyIds)) {
        if (@($result.storyIds) -notcontains [string]$storyId) { throw "Visual runtime result missing storyId '$storyId'." }
    }
    foreach ($phaseId in @($ManifestEntry.phaseIds)) {
        if (@($result.phaseIds) -notcontains [string]$phaseId) { throw "Visual runtime result missing phaseId '$phaseId'." }
    }

    if (-not ($result.PSObject.Properties.Name -contains "providerIdentity")) { throw "Visual runtime result missing providerIdentity evidence." }
    if (-not ($result.PSObject.Properties.Name -contains "providerRuntimeEvidence")) { throw "Visual runtime result missing providerRuntimeEvidence." }
    if (-not ($result.PSObject.Properties.Name -contains "runtimeObservationSummary")) { throw "Visual runtime result missing runtimeObservationSummary." }
    if (-not ($result.PSObject.Properties.Name -contains "visibleLaunchGuardrail")) { throw "Visual runtime result missing visibleLaunchGuardrail." }
    if (-not ($result.PSObject.Properties.Name -contains "visibleBehaviorEvidence")) { throw "Visual runtime result missing visibleBehaviorEvidence." }
    if (-not ($result.PSObject.Properties.Name -contains "stepDiagnostics") -or @($result.stepDiagnostics).Count -eq 0) { throw "Visual runtime result missing non-empty stepDiagnostics." }
    if ($result.providerRuntimeEvidence.configOnly -eq $true) { throw "Visual runtime provider evidence is config-only; runtime request path was not observed." }
    if ($result.providerRuntimeEvidence.dialogueSessionStarted -ne $true -or $result.providerRuntimeEvidence.dialogueResponseReceived -ne $true) { throw "Visual runtime provider evidence must include observed dialogue session and response delegates." }
    if ($result.runtimeObservationSummary.sessionStartedObserved -ne $true -or $result.runtimeObservationSummary.responseObserved -ne $true) { throw "Visual runtime observation summary must include real session and response observations." }
    if ($result.visibleBehaviorEvidence.dialogueVisible -ne $true) { throw "Visible behavior evidence must include dialogueVisible=true." }

    if ($result.command.executable -and ([System.IO.Path]::GetFileName([string]$result.command.executable) -ne "UnrealEditor.exe")) {
        throw "Visual runtime result command executable is not UnrealEditor.exe: $($result.command.executable)"
    }

    return $result
}

function Invoke-AINpcVisualGameSuite {
    param(
        [Parameter(Mandatory = $true)][string[]]$TestIds,
        [int]$TimeoutSec = 0,
        [int]$HoldSeconds = 8,
        [double]$ActionObservationHoldSeconds = 3.0,
        [switch]$AllowExistingUEProcess,
        [switch]$ValidateOnly,
        [string]$RunId,
        [string]$ResultDir
    )

    $suiteTestIds = @($TestIds | ForEach-Object { @(([string]$_).Split(',', [System.StringSplitOptions]::RemoveEmptyEntries)) } | ForEach-Object { ([string]$_).Trim() } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if ($suiteTestIds.Count -eq 0) { throw "At least one visual suite TestId is required." }
    $manifestEntries = @($suiteTestIds | ForEach-Object { Get-AINpcVisualTestEntry -TestId $_ })
    $maps = @($manifestEntries | ForEach-Object { [string]$_.map } | Select-Object -Unique)
    if ($maps.Count -ne 1) { throw "Visual suite mode requires all scenarios to use one map; found: $($maps -join ', ')" }
    if ($TimeoutSec -le 0) { $TimeoutSec = [int](($manifestEntries | Measure-Object -Property timeoutSec -Sum).Sum) }

    $repoRoot = Get-AINpcRepoRoot
    $uproject = Join-Path $repoRoot "VerifierHost.uproject"
    $editor = "G:\UE5\UnrealEngine-AINpc\Engine\Binaries\Win64\UnrealEditor.exe"
    $mapPath = [string]$maps[0]
    $mapAssetPath = Resolve-AINpcGameMapFile -MapPath $mapPath
    if ([string]::IsNullOrWhiteSpace($RunId)) { $RunId = New-AINpcRunId "visual-suite" }
    if ([string]::IsNullOrWhiteSpace($ResultDir)) { $ResultDir = Join-Path (Join-Path $repoRoot "Saved/TestLogs") "visual-game" }
    $context = New-AINpcTestRunContext -Layer "visual-game" -RunId "$RunId-harness"
    $logFile = Join-Path $context.RunDir "visual-game-suite.log"
    $testIdsFile = Join-Path $context.RunDir "visual-test-ids.txt"
    Set-Content -Path $testIdsFile -Value ($suiteTestIds -join "`n") -Encoding UTF8

    $arguments = @(
        "`"$uproject`"",
        $mapPath,
        "-game",
        "-nosplash",
        "-log",
        "-AbsLog=`"$logFile`"",
        "-AINpcVisualHoldSeconds=$HoldSeconds",
        "-AINpcVisualSuiteRunId=$RunId",
        "-AINpcVisualTestIdsFile=`"$testIdsFile`"",
        "-AINpcVisualResultDir=`"$ResultDir`"",
        "-AINpcActionObservationHoldSeconds=$ActionObservationHoldSeconds"
    )

    try {
        Assert-AINpcVisibleLaunchContract -EditorPath $editor -Arguments $arguments
        if (-not (Test-Path $uproject)) { throw "Project file not found at $uproject" }
        if (-not (Test-Path $mapAssetPath)) { throw "Visual scenario map not found for '$mapPath': $mapAssetPath" }
        if ($ValidateOnly) {
            Write-Host "SKIP: visual game suite validate-only checks passed; no runtime acceptance result claimed."
            return 0
        }
        if (-not (Test-Path $editor)) { throw "UnrealEditor.exe not found at $editor" }
        Assert-AINpcSingleVisibleInstance -AllowExistingUEProcess:$AllowExistingUEProcess

        $buildProcess = Start-Process -FilePath "pwsh" -ArgumentList @("-NoProfile", "-File", (Join-Path (Split-Path -Parent $PSScriptRoot) "build-editor.ps1")) -Wait -PassThru
        if ($buildProcess.ExitCode -ne 0) { throw "build-editor.ps1 failed with exit code $($buildProcess.ExitCode)" }

        Write-Host "Launching one visible NPC gameplay suite harness instance."
        Write-Host "Map: $mapPath"
        Write-Host "TestIds: $($suiteTestIds -join ', ')"
        Write-Host "ResultDir: $ResultDir"
        Write-Host "Log: $logFile"

        $process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru
        if (-not $process.WaitForExit($TimeoutSec * 1000)) {
            Stop-Process -Id $process.Id -Force
            throw "Visual gameplay suite timed out after $TimeoutSec seconds."
        }
        return $process.ExitCode
    }
    catch {
        $message = ConvertTo-AINpcRedactedErrorSummary -ErrorRecord $_
        Write-AINpcVisualLogTail -LogPath $logFile
        Write-Host "FAIL: $message"
        return 1
    }
}

function Invoke-AINpcVisualGameTest {
    param(
        [Parameter(Mandatory = $true)][string]$TestId,
        [int]$TimeoutSec = 0,
        [int]$HoldSeconds = 8,
        [double]$ActionObservationHoldSeconds = 3.0,
        [switch]$AllowExistingUEProcess,
        [switch]$ValidateOnly,
        [string]$RunId
    )

    $manifestEntry = Get-AINpcVisualTestEntry -TestId $TestId
    if ($TimeoutSec -le 0) { $TimeoutSec = [int]$manifestEntry.timeoutSec }

    $repoRoot = Get-AINpcRepoRoot
    $uproject = Join-Path $repoRoot "VerifierHost.uproject"
    $editor = "G:\UE5\UnrealEngine-AINpc\Engine\Binaries\Win64\UnrealEditor.exe"
    $mapPath = [string]$manifestEntry.map
    $mapAssetPath = Resolve-AINpcGameMapFile -MapPath $mapPath
    if ([string]::IsNullOrWhiteSpace($RunId)) {
        $RunId = New-AINpcRunId (($TestId -replace '[^A-Za-z0-9._-]', '-'))
    }
    $context = New-AINpcTestRunContext -Layer "visual-game" -RunId $RunId
    $logFile = Join-Path $context.RunDir "visual-game.log"
    $runtimeResultPath = Join-Path $context.RunDir "runtime-result.json"
    $startTime = (Get-Date).ToUniversalTime()
    $status = "PASS"
    $failures = @()
    $runtimeResult = $null
    $exitCode = $null

    $arguments = @(
        "`"$uproject`"",
        $mapPath,
        "-game",
        "-nosplash",
        "-log",
        "-AbsLog=`"$logFile`"",
        "-AINpcVisualHoldSeconds=$HoldSeconds",
        "-AINpcVisualTestId=$TestId",
        "-AINpcVisualRunId=$RunId",
        "-AINpcVisualResultPath=`"$runtimeResultPath`"",
        "-AINpcActionObservationHoldSeconds=$ActionObservationHoldSeconds"
    )
    $command = New-AINpcCommandInfo -Executable $editor -Arguments $arguments -WorkingDirectory $repoRoot

    try {
        Assert-AINpcVisibleLaunchContract -EditorPath $editor -Arguments $arguments
        if (-not (Test-Path $uproject)) { throw "Project file not found at $uproject" }
        if (-not (Test-Path $mapAssetPath)) { throw "Visual scenario map not found for '$mapPath': $mapAssetPath" }

        if ($ValidateOnly) {
            $status = "SKIP"
            Write-Host "SKIP: visual game harness validate-only checks passed for $TestId; no runtime acceptance result claimed."
        }
        else {
            if (-not (Test-Path $editor)) { throw "UnrealEditor.exe not found at $editor" }

            Assert-AINpcSingleVisibleInstance -AllowExistingUEProcess:$AllowExistingUEProcess

            $buildProcess = Start-Process -FilePath "pwsh" -ArgumentList @("-NoProfile", "-File", (Join-Path (Split-Path -Parent $PSScriptRoot) "build-editor.ps1")) -Wait -PassThru
            if ($buildProcess.ExitCode -ne 0) { throw "build-editor.ps1 failed with exit code $($buildProcess.ExitCode)" }

            Write-Host "Launching one visible NPC gameplay harness instance."
            Write-Host "Map: $mapPath"
            Write-Host "TestId: $TestId"
            Write-Host "Runtime result: $runtimeResultPath"
            Write-Host "Log: $logFile"

            $process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru
            if (-not $process.WaitForExit($TimeoutSec * 1000)) {
                Stop-Process -Id $process.Id -Force
                throw "Visual gameplay harness timed out after $TimeoutSec seconds."
            }

            $exitCode = $process.ExitCode
            $runtimeResult = Test-AINpcVisualRuntimeResult -ResultPath $runtimeResultPath -ManifestEntry $manifestEntry -ProcessExitCode $exitCode
            Write-Host "PASS: visual runtime result verified for $TestId"
        }
    }
    catch {
        $status = "FAIL"
        $message = ConvertTo-AINpcRedactedErrorSummary -ErrorRecord $_
        $failures += [ordered]@{ id = $TestId; message = $message; artifact = ConvertTo-AINpcRepoRelativePath $(if ($ValidateOnly) { $null } else { $runtimeResultPath }) }
        Write-AINpcVisualLogTail -LogPath $logFile
        Write-Host "FAIL: $message"
    }

    $endTime = (Get-Date).ToUniversalTime()
    $runtimeResultArtifact = $(if ($ValidateOnly) { $null } else { ConvertTo-AINpcRepoRelativePath $runtimeResultPath })
    Write-AINpcTestResult -Context $context -Status $status -StartTimeUtc $startTime -EndTimeUtc $endTime -Command $command -Artifacts ([ordered]@{
        result = ConvertTo-AINpcRepoRelativePath $context.ResultPath
        runtimeResult = $runtimeResultArtifact
        log = ConvertTo-AINpcRepoRelativePath $logFile
    }) -Diagnostics ([ordered]@{
        testId = $TestId
        map = $mapPath
        timeoutSec = $TimeoutSec
        processExitCode = $exitCode
        requiresProvider = $true
        validateOnly = [bool]$ValidateOnly
    }) -Observations ([ordered]@{
        manifest = $manifestEntry
        runtime = $runtimeResult
    }) -Failures $failures -TestId $TestId -StoryIds @($manifestEntry.storyIds) -PhaseIds @($manifestEntry.phaseIds) | Out-Null

    if ($status -eq "FAIL") { exit 1 }
    exit 0
}

if ($MyInvocation.InvocationName -ne '.') {
    if ($TestIds.Count -gt 0) {
        $exitCode = Invoke-AINpcVisualGameSuite `
            -TestIds $TestIds `
            -TimeoutSec $TimeoutSec `
            -HoldSeconds $HoldSeconds `
            -ActionObservationHoldSeconds $ActionObservationHoldSeconds `
            -AllowExistingUEProcess:$AllowExistingUEProcess `
            -ValidateOnly:$ValidateOnly `
            -RunId $RunId `
            -ResultDir $ResultDir
        exit $exitCode
    }

    if ([string]::IsNullOrWhiteSpace($TestId)) {
        throw "-TestId is required."
    }

    Invoke-AINpcVisualGameTest `
        -TestId $TestId `
        -TimeoutSec $TimeoutSec `
        -HoldSeconds $HoldSeconds `
        -ActionObservationHoldSeconds $ActionObservationHoldSeconds `
        -AllowExistingUEProcess:$AllowExistingUEProcess `
        -ValidateOnly:$ValidateOnly `
        -RunId $RunId
}
