$ErrorActionPreference = "Stop"
. (Join-Path (Split-Path -Parent $PSScriptRoot) "TestResult.ps1")

function Get-AINpcVisualTestManifest {
    $manifestPath = Join-Path $PSScriptRoot "visual-tests.json"
    if (-not (Test-Path $manifestPath)) {
        throw "Visual test manifest not found: $manifestPath"
    }

    return @(Get-Content -Path $manifestPath -Raw | ConvertFrom-Json)
}

function Get-AINpcVisualTestEntry {
    param([Parameter(Mandatory = $true)][string]$TestId)

    $matches = @(Get-AINpcVisualTestManifest | Where-Object { $_.testId -eq $TestId })
    if ($matches.Count -ne 1) {
        throw "Expected exactly one visual manifest entry for '$TestId', found $($matches.Count)."
    }

    return $matches[0]
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

function Test-AINpcVisualRuntimeResult {
    param(
        [Parameter(Mandatory = $true)][string]$ResultPath,
        [Parameter(Mandatory = $true)]$ManifestEntry,
        [int]$ProcessExitCode = 0
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
    if (@($ManifestEntry.allowedTerminalOutcomes) -notcontains [string]$result.status) { throw "Visual runtime result status '$($result.status)' is not allowed for '$($ManifestEntry.testId)'." }
    if ($ProcessExitCode -ne 0) { throw "Visual runtime process exited with code $ProcessExitCode." }

    foreach ($storyId in @($ManifestEntry.storyIds)) {
        if (@($result.storyIds) -notcontains [string]$storyId) { throw "Visual runtime result missing storyId '$storyId'." }
    }
    foreach ($phaseId in @($ManifestEntry.phaseIds)) {
        if (@($result.phaseIds) -notcontains [string]$phaseId) { throw "Visual runtime result missing phaseId '$phaseId'." }
    }

    foreach ($observationName in @($ManifestEntry.requiredObservations)) {
        if (-not ($result.observations.PSObject.Properties.Name -contains $observationName)) {
            throw "Visual runtime result missing required observation '$observationName'."
        }
        if ($result.observations.$observationName -ne $true) {
            throw "Visual runtime required observation '$observationName' was not true."
        }
    }

    if ([string]$ManifestEntry.testId -eq "us2.perception-behavior") {
        if ($result.observations.actionExecutionAccepted -ne $true -and $result.observations.actionRejectedVisible -ne $true) {
            throw "US-2 visual runtime result requires actionExecutionAccepted or actionRejectedVisible."
        }
        if ($result.observations.actionExecutionAccepted -eq $true) {
            foreach ($field in @('actionTargetReached', 'actionObservationHoldElapsed')) {
                if ($result.observations.$field -ne $true) {
                    throw "US-2 accepted action is missing required true observation '$field'."
                }
            }
        }
    }

    if ($result.command.executable -and ([System.IO.Path]::GetFileName([string]$result.command.executable) -ne "UnrealEditor.exe")) {
        throw "Visual runtime result command executable is not UnrealEditor.exe: $($result.command.executable)"
    }

    return $result
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
    $editor = "G:\UE5\UnrealEngine\Engine\Binaries\Win64\UnrealEditor.exe"
    $mapPath = [string]$manifestEntry.map
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
        "-AINpcVisualResultPath=`"$runtimeResultPath`"",
        "-AINpcActionObservationHoldSeconds=$ActionObservationHoldSeconds"
    )
    $command = New-AINpcCommandInfo -Executable $editor -Arguments $arguments -WorkingDirectory $repoRoot

    try {
        Assert-AINpcVisibleLaunchContract -EditorPath $editor -Arguments $arguments

        if ($ValidateOnly) {
            $status = "SKIP"
            Write-Host "SKIP: visual game harness validate-only checks passed for $TestId; no runtime acceptance result claimed."
        }
        else {
            if (-not (Test-Path $editor)) { throw "UnrealEditor.exe not found at $editor" }
            if (-not (Test-Path $uproject)) { throw "Project file not found at $uproject" }
            $mapAssetPath = Join-Path $repoRoot "Content/Maps/AINpcTestMap.umap"
            if (-not (Test-Path $mapAssetPath)) { throw "Visual NPC test map not found: $mapAssetPath" }

            Assert-AINpcSingleVisibleInstance -AllowExistingUEProcess:$AllowExistingUEProcess

            & (Join-Path (Split-Path -Parent $PSScriptRoot) "build-editor.ps1")
            if ($LASTEXITCODE -ne 0) { throw "build-editor.ps1 failed with exit code $LASTEXITCODE" }

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
        requiresProvider = [bool]$manifestEntry.requiresProvider
        validateOnly = [bool]$ValidateOnly
    }) -Observations ([ordered]@{
        manifest = $manifestEntry
        runtime = $runtimeResult
    }) -Failures $failures -TestId $TestId -StoryIds @($manifestEntry.storyIds) -PhaseIds @($manifestEntry.phaseIds) | Out-Null

    if ($status -eq "FAIL") { exit 1 }
    exit 0
}
