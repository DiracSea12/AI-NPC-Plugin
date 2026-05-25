$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$failures = New-Object System.Collections.Generic.List[string]
$supportedEntries = @(
    "build-editor.ps1",
    "test-static.ps1",
    "test-provider.ps1",
    "test-editor-context.ps1",
    "test-game.ps1",
    "test-all.ps1",
    "test-fast.ps1",
    "test-llm-connectivity.ps1"
)
$requiredResultFields = @(
    "schemaVersion", "runId", "layer", "testId", "storyIds", "phaseIds", "status",
    "startTimeUtc", "endTimeUtc", "durationMs", "command", "artifacts", "diagnostics", "observations", "stepDiagnostics", "providerRuntimeEvidence", "visibleBehaviorEvidence", "failures"
)
$validStatuses = @("PASS", "FAIL", "SKIP", "BLOCKED")
$validLayers = @("build", "static", "editor-automation", "provider-connectivity", "visual-game", "full-suite")

function Add-Failure([string]$Message) {
    [void]$failures.Add($Message)
}

function Get-RelativePath([string]$Path) {
    $full = [System.IO.Path]::GetFullPath($Path)
    $root = [System.IO.Path]::GetFullPath($repoRoot).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    if ($full.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $full.Substring($root.Length).Replace('\', '/')
    }
    return $full
}

function Get-JsonFile([string]$Path) {
    try {
        return Get-Content -Path $Path -Raw | ConvertFrom-Json
    }
    catch {
        Add-Failure "Malformed JSON: $(Get-RelativePath $Path) :: $($_.Exception.Message)"
        return $null
    }
}

function Get-MatrixGates($Matrix) {
    $gates = @()
    foreach ($gate in @($Matrix.story.default)) { $gates += $gate }
    if ($Matrix.story.overrides) {
        foreach ($prop in $Matrix.story.overrides.PSObject.Properties) {
            foreach ($gate in @($prop.Value)) { $gates += $gate }
        }
    }
    if ($Matrix.phase) {
        foreach ($prop in $Matrix.phase.PSObject.Properties) {
            foreach ($gate in @($prop.Value)) { $gates += $gate }
        }
    }
    return @($gates)
}

function Get-ReferencedDevEntry([string]$Command) {
    $normalized = $Command.Replace([char]92, [char]47)
    foreach ($entry in $supportedEntries) {
        if ($normalized.Contains("scripts/dev/$entry")) {
            return $entry
        }
    }
    return $null
}

function Test-JsonArrayField($Result, [string]$FieldName) {
    if (-not ($Result.PSObject.Properties.Name -contains $FieldName)) {
        return $false
    }

    $json = $Result | ConvertTo-Json -Depth 20
    return $json -match ('"' + [regex]::Escape($FieldName) + '"\s*:\s*\[')
}

function Assert-ResultSchema($Result, [string]$Path) {
    foreach ($field in $requiredResultFields) {
        if (-not ($Result.PSObject.Properties.Name -contains $field)) {
            Add-Failure "Result artifact missing '$field': $(Get-RelativePath $Path)"
        }
    }
    if (($Result.PSObject.Properties.Name -contains "schemaVersion") -and [int]$Result.schemaVersion -ne 1) {
        Add-Failure "Result artifact has invalid schemaVersion '$($Result.schemaVersion)': $(Get-RelativePath $Path)"
    }
    if (($Result.PSObject.Properties.Name -contains "status") -and $validStatuses -notcontains [string]$Result.status) {
        Add-Failure "Result artifact has invalid status '$($Result.status)': $(Get-RelativePath $Path)"
    }
    if (($Result.PSObject.Properties.Name -contains "layer") -and $validLayers -notcontains [string]$Result.layer) {
        Add-Failure "Result artifact has invalid layer '$($Result.layer)': $(Get-RelativePath $Path)"
    }
    foreach ($arrayField in @("storyIds", "phaseIds", "failures")) {
        if (($Result.PSObject.Properties.Name -contains $arrayField) -and -not (Test-JsonArrayField $Result $arrayField)) {
            Add-Failure "Result artifact field '$arrayField' must be a JSON array: $(Get-RelativePath $Path)"
        }
    }
    if (($Result.PSObject.Properties.Name -contains "diagnostics") -and
        ($Result.PSObject.Properties.Name -contains "status") -and
        ($Result.diagnostics.PSObject.Properties.Name -contains "validateOnly") -and
        [bool]$Result.diagnostics.validateOnly -and
        [string]$Result.status -eq "PASS") {
        Add-Failure "Validate-only result artifact must not claim PASS: $(Get-RelativePath $Path)"
    }
}

$gateFiles = @()
$gateFiles += Join-Path $repoRoot "docs/ralph/test-matrix.json"
$gateFiles += Get-ChildItem -Path (Join-Path $repoRoot "scripts/dev") -Filter "*.ps1" -File -Recurse | ForEach-Object { $_.FullName }
$gateFiles += Get-ChildItem -Path $repoRoot -Filter "test-*.bat" -File | ForEach-Object { $_.FullName }
foreach ($file in $gateFiles) {
    $text = Get-Content -Path $file -Raw
    if ($text -match "G:\\UE5\\FPS" + "Roguelike" -or $text -match ("FPS" + "RoguelikeEditor") -or $text -match ("FPS" + "Roguelike" + "\.uproject")) {
        Add-Failure "Stale unrelated project reference in $(Get-RelativePath $file)"
    }
}

$matrixPath = Join-Path $repoRoot "docs/ralph/test-matrix.json"
$matrix = Get-JsonFile $matrixPath
if ($matrix) {
    foreach ($gate in Get-MatrixGates $matrix) {
        $id = [string]$gate.id
        $command = [string]$gate.command
        $entry = Get-ReferencedDevEntry $command
        if (-not $entry) {
            Add-Failure "Ralph gate '$id' does not call a supported scripts/dev entry point."
        }
        if ($command -match "(?i)if\s+exist.+\s+else\s+") {
            Add-Failure "Ralph gate '$id' still contains if-exist/else command duplication."
        }
        if (-not $gate.expect) {
            Add-Failure "Ralph gate '$id' is missing expect metadata."
        }
        else {
            if (-not $gate.expect.layer -or $validLayers -notcontains [string]$gate.expect.layer) {
                Add-Failure "Ralph gate '$id' has invalid expect.layer '$($gate.expect.layer)'."
            }
            if ($gate.expect.resultArtifact -ne $true) {
                Add-Failure "Ralph gate '$id' must declare expect.resultArtifact=true."
            }
        }
        $claimsProviderSensitive = $id -match "(?i)(provider|llm|dialogue|memory|visual|npc|behavior|game)"
        $staticOrEditorOnly = $gate.expect -and ([bool]$gate.expect.staticOnly -or [bool]$gate.expect.editorOnly)
        if ($claimsProviderSensitive -and -not $staticOrEditorOnly) {
            if ($entry -ne "test-provider.ps1" -and $entry -ne "test-all.ps1") {
                Add-Failure "Provider-sensitive Ralph gate '$id' must call test-provider.ps1 or test-all.ps1 unless explicitly static/editor-only."
            }
        }
    }
}

foreach ($bat in Get-ChildItem -Path $repoRoot -Filter "test-*.bat" -File) {
    $name = [System.IO.Path]::GetFileNameWithoutExtension($bat.Name)
    $expected = if ($name -eq "test-llm-connectivity") { "test-llm-connectivity.ps1" } else { "$name.ps1" }
    $content = @(Get-Content -Path $bat.FullName)
    $joined = ($content -join "`n")
    if ($joined -notmatch 'cd /d "%~dp0"') { Add-Failure "$($bat.Name) does not cd to repository root." }
    if ($joined -notmatch [regex]::Escape(".\scripts\dev\$expected")) { Add-Failure "$($bat.Name) does not call scripts/dev/$expected." }
    if ($joined -notmatch "exit /b %EXIT_CODE%") { Add-Failure "$($bat.Name) does not preserve the PowerShell exit code." }
    $logicLines = @($content | Where-Object {
        $line = $_.Trim()
        $line -and
        $line -notmatch "^@echo off$" -and
        $line -notmatch "^setlocal$" -and
        $line -notmatch '^cd /d "%~dp0"$' -and
        $line -notmatch ('^pwsh .* -File "' + [regex]::Escape(".\scripts\dev\$expected") + '"$') -and
        $line -notmatch '^set EXIT_CODE=%ERRORLEVEL%$' -and
        $line -notmatch "^pause$" -and
        $line -notmatch '^exit /b %EXIT_CODE%$'
    })
    if ($logicLines.Count -gt 0) { Add-Failure "$($bat.Name) contains non-wrapper logic: $($logicLines -join ' | ')" }
}

$obsoleteManifestPath = Join-Path $repoRoot "scripts/dev/game/visual-tests.json"
if (Test-Path $obsoleteManifestPath) {
    Add-Failure "Obsolete visual manifest still exists; use Config/AINpcVisualScenarios.json as the single source of truth: $(Get-RelativePath $obsoleteManifestPath)"
}

$knownVisualObservationNames = @(
    "sessionStarted",
    "waitingStateObserved",
    "speakingStateObserved",
    "dialogueResponseObserved",
    "partialResponseObserved",
    "structuredResponseObserved",
    "actionIntentObserved",
    "eventTriggerBroadcast",
    "eventDelayMaskingStartObserved",
    "delayMaskingStartObserved",
    "delayMaskingEndObserved",
    "actionExecutionAccepted",
    "actionRejectedVisible",
    "actionTargetReached",
    "actionTargetReachedHoldElapsed",
    "responseLength",
    "partialResponseLength",
    "delayFillerLength",
    "distanceToActionTarget",
    "lastActionFailure"
)
$requiredScenarioFields = @(
    "schemaVersion", "testId", "map", "timeoutSec", "storyIds", "phaseIds", "fixture", "persona", "prompt", "steps", "expect"
)

function Test-NonEmptyJsonArray($Object, [string]$FieldName, [string]$Context) {
    if (-not ($Object.PSObject.Properties.Name -contains $FieldName)) {
        Add-Failure "$Context missing required field '$FieldName'."
        return @()
    }
    $json = $Object | ConvertTo-Json -Depth 20
    if ($json -notmatch ('"' + [regex]::Escape($FieldName) + '"\s*:\s*\[')) {
        Add-Failure "$Context field '$FieldName' must be a JSON array."
        return @()
    }
    $values = @($Object.$FieldName)
    if ($values.Count -eq 0) {
        Add-Failure "$Context field '$FieldName' must not be empty."
    }
    foreach ($value in $values) {
        if ([string]::IsNullOrWhiteSpace([string]$value)) {
            Add-Failure "$Context field '$FieldName' contains an empty value."
        }
    }
    return $values
}

function Resolve-VisualScenarioMapPath([string]$MapPath) {
    if ([string]::IsNullOrWhiteSpace($MapPath) -or -not $MapPath.StartsWith("/Game/")) {
        return $null
    }
    $relative = $MapPath.Substring("/Game/".Length).TrimStart('/')
    if ([string]::IsNullOrWhiteSpace($relative)) { return $null }
    return Join-Path (Join-Path $repoRoot "Content") ($relative + ".umap")
}

function Test-KnownVisualObservation([string]$Observation, [string]$Context) {
    if ([string]::IsNullOrWhiteSpace($Observation)) { return @("$Context observation must not be empty.") }
    if ($knownVisualObservationNames -notcontains $Observation) { return @("$Context references unknown observation '$Observation'.") }
    return @()
}

function Test-VisualScenarioAssertion($Assertion, [string]$Context) {
    $localFailures = New-Object System.Collections.Generic.List[string]
    $operatorFields = @("all", "any", "anyOf", "exists", "equals", "notExists")
    if (-not $Assertion) {
        [void]$localFailures.Add("$Context assertion must be an object.")
        return @($localFailures)
    }
    $presentOperators = @($operatorFields | Where-Object { $Assertion.PSObject.Properties.Name -contains $_ })
    if ($presentOperators.Count -ne 1) { [void]$localFailures.Add("$Context assertion must use exactly one operator.") }
    foreach ($field in @($Assertion.PSObject.Properties.Name)) {
        if ($operatorFields -notcontains $field) { [void]$localFailures.Add("$Context assertion has unknown field $field.") }
    }
    if ($Assertion.PSObject.Properties.Name -contains "exists") {
        foreach ($failure in @(Test-KnownVisualObservation ([string]$Assertion.exists) "$Context exists")) { [void]$localFailures.Add($failure) }
    }
    if ($Assertion.PSObject.Properties.Name -contains "notExists") {
        $notExistsObservation = [string]$Assertion.notExists
        foreach ($failure in @(Test-KnownVisualObservation $notExistsObservation "$Context notExists")) { [void]$localFailures.Add($failure) }
        if (@("waitingStateObserved", "speakingStateObserved", "actionTargetReached") -notcontains $notExistsObservation) {
            [void]$localFailures.Add("$Context notExists cannot prove full-window absence for observation '$notExistsObservation'.")
        }
    }
    if ($Assertion.PSObject.Properties.Name -contains "equals") {
        foreach ($failure in @(Test-KnownVisualObservation ([string]$Assertion.equals.observation) "$Context equals")) { [void]$localFailures.Add($failure) }
    }
    foreach ($groupField in @("all", "any", "anyOf")) {
        if ($Assertion.PSObject.Properties.Name -contains $groupField) {
            $children = @($Assertion.$groupField)
            if ($children.Count -eq 0) { [void]$localFailures.Add("$Context assertion $groupField must contain child assertions.") }
            for ($i = 0; $i -lt $children.Count; $i++) {
                foreach ($failure in @(Test-VisualScenarioAssertion $children[$i] "$Context.$groupField[$i]")) { [void]$localFailures.Add($failure) }
            }
        }
    }
    return @($localFailures)
}


function Test-FinalExpectNoActionAdapterFacts($Assertion, [string]$Context) {
    $localFailures = New-Object System.Collections.Generic.List[string]
    if (-not $Assertion) { return @($localFailures) }
    if (($Assertion.PSObject.Properties.Name -contains "exists") -and @("actionExecutionAccepted", "actionRejectedVisible") -contains [string]$Assertion.exists) {
        [void]$localFailures.Add("$Context final expect must not use action adapter facts '$($Assertion.exists)'.")
    }
    if (($Assertion.PSObject.Properties.Name -contains "notExists") -and @("actionExecutionAccepted", "actionRejectedVisible") -contains [string]$Assertion.notExists) {
        [void]$localFailures.Add("$Context final expect must not use action adapter facts '$($Assertion.notExists)'.")
    }
    if (($Assertion.PSObject.Properties.Name -contains "equals") -and @("actionExecutionAccepted", "actionRejectedVisible") -contains [string]$Assertion.equals.observation) {
        [void]$localFailures.Add("$Context final expect must not use action adapter facts '$($Assertion.equals.observation)'.")
    }
    foreach ($groupField in @("all", "any", "anyOf")) {
        if ($Assertion.PSObject.Properties.Name -contains $groupField) {
            $children = @($Assertion.$groupField)
            for ($i = 0; $i -lt $children.Count; $i++) {
                foreach ($failure in @(Test-FinalExpectNoActionAdapterFacts $children[$i] "$Context.$groupField[$i]")) { [void]$localFailures.Add($failure) }
            }
        }
    }
    return @($localFailures)
}

$scenariosPath = Join-Path $repoRoot "Config/AINpcVisualScenarios.json"
$scenarios = @(Get-JsonFile $scenariosPath)
$scenarioIds = @($scenarios | ForEach-Object { [string]$_.testId })
foreach ($group in $scenarioIds | Group-Object) {
    if (-not [string]::IsNullOrWhiteSpace($group.Name) -and $group.Count -gt 1) { Add-Failure "Duplicate visual scenario TestId '$($group.Name)'." }
}

foreach ($entry in $scenarios) {
    $context = "Visual scenario '$($entry.testId)'"
    foreach ($field in $requiredScenarioFields) {
        if (-not ($entry.PSObject.Properties.Name -contains $field)) { Add-Failure "$context missing required field '$field'." }
    }
    foreach ($legacyField in @("requiresProvider", "promptFile", "personaFile", "delayFillerFile", "delayFillerThreshold", "requireEventTrigger", "eventTag", "eventTriggerId", "requirePartialResponse", "requireStructuredResponse", "requireActionIntent", "allowActionRejection", "requiredObservations", "allowedTerminalOutcomes", "script")) {
        if ($entry.PSObject.Properties.Name -contains $legacyField) { Add-Failure "$context uses legacy v1 visual scenario field '$legacyField'." }
    }
    foreach ($topField in @($entry.PSObject.Properties.Name)) {
        if ($requiredScenarioFields -notcontains $topField) { Add-Failure "$context has unknown top-level field '$topField'." }
    }
    $schemaVersionNumber = [double]$entry.schemaVersion
    if ($schemaVersionNumber -ne [Math]::Truncate($schemaVersionNumber) -or [int]$schemaVersionNumber -ne 2) { Add-Failure "$context schemaVersion must be integer 2." }
    foreach ($field in @("testId", "map")) { if ([string]::IsNullOrWhiteSpace([string]$entry.$field)) { Add-Failure "$context field '$field' must not be empty." } }
    if ([double]$entry.timeoutSec -le 0) { Add-Failure "$context timeoutSec must be greater than zero." }
    [void](Test-NonEmptyJsonArray $entry "storyIds" $context)
    [void](Test-NonEmptyJsonArray $entry "phaseIds" $context)
    if ($entry.fixture.PSObject.Properties.Name -contains "type") { Add-Failure "$context fixture.type is rejected; use fixture.adapterId and fixture.kind." }
    foreach ($unknown in @($entry.fixture.PSObject.Properties.Name | Where-Object { @("adapterId", "kind") -notcontains $_ })) { Add-Failure "$context fixture has unknown field $unknown." }
    if ([string]$entry.fixture.adapterId -ne "builtin.characterFixture") { Add-Failure "$context fixture.adapterId must be builtin.characterFixture." }
    if (@("character", "characterWithSmartObject") -notcontains [string]$entry.fixture.kind) { Add-Failure "$context fixture.kind must be character or characterWithSmartObject." }
    foreach ($field in @("file", "delayFillerFile")) {
        $fileName = [string]$entry.persona.$field
        if ([string]::IsNullOrWhiteSpace($fileName)) { Add-Failure "$context persona.$field must not be empty." }
        elseif ($fileName.Contains('/') -or $fileName.Contains([char]92) -or [System.IO.Path]::IsPathRooted($fileName)) { Add-Failure "$context persona.$field must be a Config file name, not a path: '$fileName'." }
        else {
            $configFile = Join-Path (Join-Path $repoRoot "Config") $fileName
            if (-not (Test-Path $configFile)) { Add-Failure "$context references missing persona.$field file: $(Get-RelativePath $configFile)" }
        }
    }
    if ([double]$entry.persona.delayFillerThreshold -lt 0) { Add-Failure "$context persona.delayFillerThreshold must not be negative." }
    $promptFile = [string]$entry.prompt.file
    $promptPath = Join-Path (Join-Path $repoRoot "Config") $promptFile
    if (-not (Test-Path $promptPath)) { Add-Failure "$context references missing prompt.file: $promptFile" }
    else {
        $promptText = Get-Content -Path $promptPath -Raw
        foreach ($variable in @($entry.prompt.variables.PSObject.Properties)) {
            if ($promptText -notmatch ('\{' + [regex]::Escape($variable.Name) + '\}')) { Add-Failure "$context unresolved prompt variable '$($variable.Name)' for $promptFile." }
        }
        foreach ($match in [regex]::Matches($promptText, '\{([^{}]+)\}')) {
            $placeholder = [string]$match.Groups[1].Value
            if (-not ($entry.prompt.variables.PSObject.Properties.Name -contains $placeholder)) { Add-Failure "$context prompt.file contains undeclared placeholder '$placeholder' for $promptFile." }
        }
    }
    $mapAssetPath = Resolve-VisualScenarioMapPath ([string]$entry.map)
    if (-not $mapAssetPath) { Add-Failure "$context map must be a /Game asset path, got '$($entry.map)'." }
    elseif (-not (Test-Path $mapAssetPath)) { Add-Failure "$context map does not resolve to an existing .umap: $(Get-RelativePath $mapAssetPath)" }
    foreach ($step in @($entry.steps)) {
        if (@("dialogue.start", "world.event", "wait.until", "action.executeLatestIntent", "observe.hold") -notcontains [string]$step.type) { Add-Failure "$context has unknown step type '$($step.type)'." }
        if (-not $step.payload) { Add-Failure "$context step '$($step.type)' missing payload." }
        if ([string]$step.type -ne "wait.until" -and ($step.PSObject.Properties.Name -contains "condition")) { Add-Failure "$context condition is only supported by wait.until." }
        switch ([string]$step.type) {
            "dialogue.start" { if ([string]$step.payload.promptRef -ne "prompt") { Add-Failure "$context dialogue.start payload.promptRef must be prompt." } }
            "world.event" {
                if ([string]$step.payload.adapterId -ne "builtin.npcEvent") { Add-Failure "$context world.event step must use builtin.npcEvent adapterId from the C++ schema descriptor." }
                if ($step.payload.PSObject.Properties.Name -contains "eventTriggerId") { Add-Failure "$context world.event step uses legacy eventTriggerId payload field." }
            }
            "wait.until" { if ([double]$step.payload.timeoutSec -le 0) { Add-Failure "$context wait.until payload.timeoutSec must be positive." }; if (-not $step.condition) { Add-Failure "$context wait.until must include condition." } else { foreach ($failure in @(Test-VisualScenarioAssertion $step.condition "$context wait.until condition")) { Add-Failure $failure } } }
            "action.executeLatestIntent" {
                if ([string]$step.payload.adapterId -ne "builtin.smartObjectAction") { Add-Failure "$context action.executeLatestIntent step must use builtin.smartObjectAction adapterId from the C++ schema descriptor." }
            }
            "observe.hold" { foreach ($unknown in @($step.payload.PSObject.Properties.Name | Where-Object { @("observation", "durationSec") -notcontains $_ })) { Add-Failure "$context observe.hold payload has unknown field $unknown." }; if ([string]::IsNullOrWhiteSpace([string]$step.payload.observation)) { Add-Failure "$context observe.hold payload.observation must not be empty." }; if ([double]$step.payload.durationSec -le 0) { Add-Failure "$context observe.hold payload.durationSec must be positive." }; foreach ($failure in @(Test-KnownVisualObservation ([string]$step.payload.observation) "$context observe.hold")) { Add-Failure $failure } }
        }
    }
    if (-not $entry.expect) { Add-Failure "$context missing expect assertion." }
    else { foreach ($failure in @(Test-VisualScenarioAssertion $entry.expect "$context expect")) { Add-Failure $failure }; foreach ($failure in @(Test-FinalExpectNoActionAdapterFacts $entry.expect "$context expect")) { Add-Failure $failure } }
}

$registryText = Get-Content -Path (Join-Path $repoRoot "Plugins/AINpc/Source/AINpcCore/Private/Test/AINpcVisualTestRegistry.cpp") -Raw
$visualTestHeaderText = Get-Content -Path (Join-Path $repoRoot "Plugins/AINpc/Source/AINpcCore/Public/Test/AINpcVisualTest.h") -Raw
$runnerText = Get-Content -Path (Join-Path $repoRoot "Plugins/AINpc/Source/AINpcCore/Private/Test/AINpcDataDrivenVisualScenarioTest.cpp") -Raw
$observationContractText = Get-Content -Path (Join-Path $repoRoot "Plugins/AINpc/Source/AINpcCore/Private/Tests/AINpcVisualObservationContractTests.cpp") -Raw
if ($registryText -notmatch 'builtin\.npcEvent' -or $registryText -notmatch 'builtin\.smartObjectAction' -or $registryText -notmatch 'eventId' -or $registryText -notmatch 'actorRef') {
    Add-Failure "C++ visual scenario schema descriptor must validate Phase 2.8b event/action adapter payload ids and required fields."
}
if ($registryText -notmatch 'notExists') {
    Add-Failure "C++ visual scenario schema descriptor must validate Phase 2.8c notExists assertions."
}
if ($registryText -notmatch 'fixture\.type' -or $registryText -notmatch 'fixture\.adapterId' -or $registryText -notmatch 'fixture\.kind' -or $registryText -notmatch 'builtin\.characterFixture' -or $registryText -notmatch 'characterWithSmartObject') {
    Add-Failure "C++ visual scenario schema descriptor must reject fixture.type and validate fixture.adapterId/fixture.kind."
}
if ($visualTestHeaderText -notmatch 'AdapterId' -or $visualTestHeaderText -notmatch 'Kind' -or $visualTestHeaderText -match 'struct\s+FAINpcVisualScenarioFixtureSpec[^}]*FString\s+Type\s*;') {
    Add-Failure "C++ visual fixture spec must expose adapterId/kind storage only, not legacy type."
}
if ($visualTestHeaderText -notmatch 'struct\s+FAINpcVisualObservationRecord' -or $visualTestHeaderText -notmatch 'SourceKind' -or $visualTestHeaderText -notmatch 'AdapterOrProviderId' -or $visualTestHeaderText -notmatch 'StepIndex' -or $visualTestHeaderText -notmatch 'TimestampSeconds') {
    Add-Failure "C++ visual test contract must expose typed/sourced observation record metadata for Phase 2.8c."
}
if ($registryText -notmatch 'AINpc\.Visual\.Phase28d\.InternalAdapterLifecycleBoundary' -or $registryText -notmatch 'builtin\.characterFixture' -or $registryText -notmatch 'builtin\.npcEvent' -or $registryText -notmatch 'builtin\.smartObjectAction' -or $registryText -notmatch 'eventTriggerId' -or $registryText -notmatch 'legacy fixture kind' -or $registryText -notmatch 'legacy top-level action field' -or $registryText -notmatch 'payload\.payload must be an empty object' -or $registryText -notmatch 'allowActionRejection must be boolean') {
    Add-Failure "Phase 2.8d must have automation coverage for internal adapter ids, malformed adapter payloads, and rejected legacy fixture/event/action fields."
}
$gameModeText = Get-Content -Path (Join-Path $repoRoot "Plugins/AINpc/Source/AINpcCore/Private/Test/AINpcTestGameMode.cpp") -Raw
if ($gameModeText -notmatch 'CleanupActiveScenario' -or $gameModeText -notmatch 'ActiveTest\.Reset\(\)' -or $gameModeText -notmatch 'SpawnedSmartObject->Destroy\(\)' -or $gameModeText -notmatch 'SpawnedNpc->Destroy\(\)' -or $gameModeText -notmatch 'BridgeContext->ReleaseSlotForUser' -or $gameModeText -notmatch 'VisualRunId\s*=\s*FString::Printf\(TEXT\("%s-%s"\)') {
    Add-Failure "Phase 2.8d visual suite lifecycle must isolate consecutive scenarios and release per-run fixture/action refs."
}
if ($runnerText -notmatch 'FAINpcVisualScenarioRuntimeView' -or $runnerText -notmatch 'TUniquePtr<FAINpcVisualObservationStore>\s+Observations' -or $runnerText -notmatch 'DialogueObservationProvider->Stop\(\)' -or $runnerText -notmatch 'ClearTimer\(StepTimerHandle\)' -or $runnerText -notmatch 'ClearTimer\(TimeoutTimerHandle\)') {
    Add-Failure "Phase 2.8d runner lifecycle must keep per-run runtime/observation state and stop/clear refs on scenario end or world teardown."
}
if ($observationContractText -notmatch 'AINpc\.Visual\.Observation\.IsolationAndTypeFailure' -or $observationContractText -notmatch 'fresh per-run observation store starts without previous scenario records' -or $observationContractText -notmatch 'previous run store still proves the isolation test would catch shared state') {
    Add-Failure "Phase 2.8d observation contract tests must prove per-run observation stores do not reuse stale scenario records."
}
$publicApiScanFiles = Get-ChildItem -Path (Join-Path $repoRoot "Plugins/AINpc/Source/AINpcCore/Public") -Recurse -Include "*.h", "*.cpp" -File
foreach ($file in $publicApiScanFiles) {
    $text = Get-Content -Path $file.FullName -Raw
    foreach ($pattern in @(
        'I[A-Za-z0-9_]*Project[A-Za-z0-9_]*Adapter',
        'Public[A-Za-z0-9_]*AdapterRegistry',
        'I[A-Za-z0-9_]*CharacterDriver',
        'CapabilityDeclaration',
        'BlueprintType[\s\S]{0,200}Adapter'
    )) {
        if ($text -match $pattern) { Add-Failure "Phase 2.8a public adapter API guard matched '$pattern' in $(Get-RelativePath $file.FullName)." }
    }
}
$buildFiles = Get-ChildItem -Path (Join-Path $repoRoot "Plugins/AINpc/Source") -Recurse -Filter "*.Build.cs" -File
foreach ($file in $buildFiles) {
    $text = Get-Content -Path $file.FullName -Raw
    if ($text -match 'PublicDependencyModuleNames[\s\S]*Project[A-Za-z0-9_]*Adapter') { Add-Failure "Phase 2.8a must not add runtime module public dependency for project adapter API in $(Get-RelativePath $file.FullName)." }
}
foreach ($id in @("us1.dialogue-action", "us2.perception-behavior", "phase27.prompt-only-dialogue-action")) {
    if ($runnerText -match [regex]::Escape($id)) { Add-Failure "Scenario runner contains test-id-specific branch/text '$id'." }
}
$allPhase27Text = @()
$allPhase27Text += $runnerText
$allPhase27Text += Get-Content -Path (Join-Path $repoRoot "scripts/dev/game/VisualGameHarness.ps1") -Raw
$allPhase27Text += Get-Content -Path (Join-Path $repoRoot "scripts/dev/test-game.ps1") -Raw
foreach ($forbiddenInfra in @("remote service", "database", "web console", "distributed scheduler", "long-running daemon", "private service")) {
    if (($allPhase27Text -join "`n") -match $forbiddenInfra) { Add-Failure "Phase 2.7 introduces forbidden personal-OSS infrastructure text: $forbiddenInfra" }
}

function Assert-AINpcVisibleLaunchContract {
    param(
        [Parameter(Mandatory = $true)][string]$EditorPath,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )
    $editorName = [System.IO.Path]::GetFileName($EditorPath)
    if ($editorName -ne "UnrealEditor.exe") { throw "Visual acceptance must launch UnrealEditor.exe directly, got '$editorName'." }
    $joinedArgs = ($Arguments -join " ")
    foreach ($pattern in @('UnrealEditor-Cmd', '(?i)(^|\s)-nullrhi(\s|$)', '(?i)(^|\s)-unattended(\s|$)', '(?i)mock\s*provider', '(?i)(^|\s)-AINpcMockProvider(\s|=|$)', '(?i)(^|\s)-AINpcInjectedResponse(\s|=|$)', '(?i)(^|\s)-AINpc.*Bypass(\s|=|$)', '(?i)SetDialogueDispatchBypassForTest', '(?i)HandleRequestCompletedForTest')) {
        if ($EditorPath -match $pattern -or $joinedArgs -match $pattern) { throw "Forbidden final visual acceptance mode detected: $pattern" }
    }
    if (@($Arguments | Where-Object { $_ -eq "-game" }).Count -ne 1) { throw "Visual acceptance must launch with exactly one -game argument." }
}
try {
    Assert-AINpcVisibleLaunchContract -EditorPath "G:\UE5\UnrealEngine-AINpc\Engine\Binaries\Win64\UnrealEditor.exe" -Arguments @("VerifierHost.uproject", "/Game/Maps/AINpcTestMap", "-game", "-nullrhi")
    Add-Failure "Phase 2.7 negative validation case forbidden final-acceptance mode unexpectedly passed visible launch validation."
}
catch {
    if ([string]$_.Exception.Message -notmatch "Forbidden final visual acceptance mode") { Add-Failure "Phase 2.7 negative validation case forbidden final-acceptance mode failed with wrong diagnostic: $($_.Exception.Message)" }
}

$validateOnlyFinalResult = [pscustomobject]@{
    schemaVersion = 1; runId = "negative"; layer = "visual-game"; testId = "synthetic.negative-contract"; storyIds = @("US-X"); phaseIds = @("phase2.7"); status = "PASS";
    startTimeUtc = "2026-01-01T00:00:00Z"; endTimeUtc = "2026-01-01T00:00:00Z"; durationMs = 0; command = [pscustomobject]@{}; artifacts = [pscustomobject]@{};
    diagnostics = [pscustomobject]@{ validateOnly = $true }; observations = [pscustomobject]@{}; stepDiagnostics = @(); providerRuntimeEvidence = [pscustomobject]@{}; visibleBehaviorEvidence = [pscustomobject]@{}; failures = @()
}
$beforeValidateOnlyFailures = $failures.Count
Assert-ResultSchema $validateOnlyFinalResult (Join-Path $repoRoot "Saved/TestLogs/_contract-negative/validate-only-pass.json")
if ($failures.Count -eq $beforeValidateOnlyFailures) {
    Add-Failure "Phase 2.7 negative validation case validate-only-as-final unexpectedly passed result schema validation."
}
else {
    $failures.RemoveRange($beforeValidateOnlyFailures, $failures.Count - $beforeValidateOnlyFailures)
}

$testFiles = Get-ChildItem -Path (Join-Path $repoRoot "Plugins/AINpc/Source") -Recurse -Filter "*.cpp" -File |
    Where-Object { $_.FullName -replace '\\', '/' -match '/Private/Tests/' }
foreach ($file in $testFiles) {
    $content = Get-Content -Path $file.FullName -Raw
    foreach ($match in [regex]::Matches($content, 'IMPLEMENT_[A-Z_]*AUTOMATION_TEST[\s\S]*?"([^"]+)"')) {
        $path = $match.Groups[1].Value
        if (-not $path.StartsWith("AINpc.")) {
            Add-Failure "Automation test path '$path' in $(Get-RelativePath $file.FullName) must start with AINpc."
        }
    }
}

$testGameScript = Join-Path $repoRoot "scripts/dev/test-game.ps1"
$testGameText = Get-Content -Path $testGameScript -Raw
if ($testGameText -match "Get-AINpcLatestResultPath") {
    Add-Failure "test-game.ps1 must not use Get-AINpcLatestResultPath; pass deterministic child RunId/ResultPath instead."
}
if ($testGameText -notmatch "Invoke-AINpcVisualGameSuite") {
    Add-Failure "test-game.ps1 must launch the visual-game manifest through one suite harness invocation."
}
if ($testGameText -match 'Start-Process\s+-FilePath\s+"pwsh"[\s\S]*VisualGameHarness\.ps1') {
    Add-Failure "test-game.ps1 must not launch VisualGameHarness.ps1 once per scenario."
}
if ($testGameText -notmatch "expectedRunId" -or $testGameText -notmatch "runtime-result\.json") {
    Add-Failure "test-game.ps1 must validate deterministic per-scenario runtime result paths."
}

$testAllScript = Join-Path $repoRoot "scripts/dev/test-all.ps1"
$testAllText = Get-Content -Path $testAllScript -Raw
if ($testAllText -match "Get-AINpcLatestResultPath") {
    Add-Failure "test-all.ps1 must not use Get-AINpcLatestResultPath; pass deterministic child RunId/ResultPath instead."
}
if ($testAllText -notmatch "childRunId" -or $testAllText -notmatch '"-RunId"' -or $testAllText -notmatch "Saved/TestLogs") {
    Add-Failure "test-all.ps1 must pass deterministic child RunIds and read exact expected result paths."
}
foreach ($entryScript in @("build-editor.ps1", "test-static.ps1", "test-editor-context.ps1", "test-provider.ps1", "test-game.ps1")) {
    $entryText = Get-Content -Path (Join-Path $repoRoot "scripts/dev/$entryScript") -Raw
    if ($entryText -notmatch '\[string\]\$RunId' -or $entryText -notmatch 'New-AINpcTestRunContext[\s\S]*-RunId\s+\$RunId') {
        Add-Failure "$entryScript must accept optional -RunId and pass it to New-AINpcTestRunContext."
    }
}

$visualScripts = @()
$visualScripts += Get-ChildItem -Path (Join-Path $repoRoot "scripts/dev/game") -Filter "*.ps1" -File
$visualScripts += Get-Item (Join-Path $repoRoot "scripts/dev/test-game.ps1")
$forbiddenVisualPatterns = @(
    "UnrealEditor-Cmd",
    "(?i)-nullrhi",
    "(?i)-unattended",
    "(?i)WindowStyle\s+Hidden",
    "(?i)mock\s*provider",
    "(?i)AINpcMockProvider",
    "(?i)AINpcInjectedResponse",
    "(?i)SetDialogueDispatchBypassForTest",
    "(?i)HandleRequestCompletedForTest",
    "(?i)DialogueDispatchBypass"
)
foreach ($file in $visualScripts) {
    $text = Get-Content -Path $file.FullName -Raw
    $textForForbiddenScan = [regex]::Replace($text, '(?ms)^\s*\$forbiddenPatterns\s*=\s*@\([\s\S]*?^\s*\)\s*', '')
    foreach ($pattern in $forbiddenVisualPatterns) {
        if ($textForForbiddenScan -match $pattern) {
            Add-Failure "Forbidden visual acceptance mode '$pattern' appears in $(Get-RelativePath $file.FullName)."
        }
    }
}

$editorScript = Join-Path $repoRoot "scripts/dev/test-editor-context.ps1"
$editorText = Get-Content -Path $editorScript -Raw
if ($editorText -notmatch 'Start-Process\s+-FilePath\s+\$editor' -or $editorText -match "UnrealEditor-Cmd|(?i)-nullrhi|(?i)-unattended") {
    Add-Failure "test-editor-context.ps1 must preserve visible UnrealEditor execution without headless/commandlet flags."
}
if ($editorText -notmatch "not final player-visible NPC behavior acceptance") {
    Add-Failure "test-editor-context.ps1 must explicitly state editor automation is not final player-visible NPC behavior acceptance."
}

$visualGameModePath = Join-Path $repoRoot "Plugins/AINpc/Source/AINpcCore/Private/Test/AINpcTestGameMode.cpp"
$visualGameModeText = Get-Content -Path $visualGameModePath -Raw
foreach ($match in [regex]::Matches($visualGameModeText, 'SetStringField\s*\(\s*TEXT\("(arguments|display)"\)[\s\S]*?FCommandLine::Get\s*\(')) {
    $snippet = $match.Value
    if ($snippet -notmatch 'RedactSensitiveText') {
        Add-Failure "Visual runtime result writes raw FCommandLine::Get() into command.$($match.Groups[1].Value)."
    }
}
foreach ($field in @('summary', 'message')) {
    foreach ($match in [regex]::Matches($visualGameModeText, 'SetStringField\s*\(\s*TEXT\("' + $field + '"\)[^;]+;')) {
        if ($match.Value -match '(DiagnosticSummary|FailureReason)' -and $match.Value -notmatch 'RedactSensitiveText') {
            Add-Failure "Visual runtime result writes raw $field text without redaction."
        }
    }
}
if ($visualGameModeText -notmatch 'TEXT\("bearer"\)') {
    Add-Failure "RedactSensitiveText key list must include bearer."
}
if ($visualGameModeText -match 'SetStringField\s*\(\s*Field\.Key\s*,\s*Field\.Value\s*\)') {
    Add-Failure "BuildObservationJson writes raw observation string fields without redaction."
}
if ($visualGameModeText -notmatch 'records' -or $visualGameModeText -notmatch 'sourceKind' -or $visualGameModeText -notmatch 'sourceIdentity' -or $visualGameModeText -notmatch 'adapterOrProviderId') {
    Add-Failure "Visual runtime result must serialize typed observation records with source metadata."
}

$testCharacterPath = Join-Path $repoRoot "Plugins/AINpc/Source/AINpcCore/Private/Test/AINpcTestCharacter.cpp"
$testCharacterText = Get-Content -Path $testCharacterPath -Raw
if ($testCharacterText -match 'VInterpConstantTo|SetActorLocation\s*\(') {
    Add-Failure "AAINpcTestCharacter action movement must use CharacterMovement/AddMovementInput, not direct actor relocation."
}
if ($testCharacterText -notmatch 'AddMovementInput' -or $testCharacterText -notmatch 'StopMovementImmediately' -or $testCharacterText -notmatch 'bRunPhysicsWithNoController') {
    Add-Failure "AAINpcTestCharacter action movement must drive CharacterMovement without a controller and stop it at the target."
}

$visualTestPaths = @(
    (Join-Path $repoRoot "Plugins/AINpc/Source/AINpcCore/Private/Test/AINpcDataDrivenVisualScenarioTest.cpp")
)
$rawVisualTextPatterns = @(
    'ShowStatus.*(Text|TrimmedText|LastNpcResponseText|FallbackResponse)\.Left\s*\(\s*(?!2[0-9][0-9]\s*\))',
    'Fail.*(Text|TrimmedText|LastNpcResponseText|FallbackResponse|ErrorMessage|FailureReasonText)\.Left?\s*\(',
    'Fail.*\*(ErrorMessage|FailureReasonText)'
)
foreach ($visualTestPath in $visualTestPaths) {
    $visualTestLines = Get-Content -Path $visualTestPath
    foreach ($line in $visualTestLines) {
        foreach ($pattern in $rawVisualTextPatterns) {
            if ($line -match $pattern) {
                Add-Failure "Visual test ShowStatus/Fail path exposes raw provider/model text in $(Get-RelativePath $visualTestPath)."
                break
            }
        }
    }
}

$diagnosticsHeaderPath = Join-Path $repoRoot "Plugins/AINpc/Source/AINpcCore/Private/LLM/AINpcLLMDiagnostics.h"
$diagnosticsHeaderText = Get-Content -Path $diagnosticsHeaderPath -Raw
if ($diagnosticsHeaderText -match 'BuildSafeResponseSummary[\s\S]*?ResponseBody\.Left\s*\(') {
    Add-Failure "BuildSafeResponseSummary must not expose response body text with ResponseBody.Left(...)."
}

$sampleResultPath = Join-Path $repoRoot "Saved/TestLogs/_contract-sample/result.json"
$sampleStoryIds = [System.Collections.Generic.List[object]]::new()
$sampleStoryIds.Add("US-contract")
$samplePhaseIds = [System.Collections.Generic.List[object]]::new()
$samplePhaseIds.Add("phase2.5")
$sampleFailures = [System.Collections.Generic.List[object]]::new()
$sampleFailures.Add([pscustomobject]@{ id = "contract-failure"; message = "sample"; artifact = $null })
$sampleResult = [pscustomobject]@{
    schemaVersion = 1
    runId = "contract-sample"
    layer = "static"
    testId = "contract-schema"
    storyIds = $sampleStoryIds
    phaseIds = $samplePhaseIds
    status = "BLOCKED"
    startTimeUtc = "2026-01-01T00:00:00.0000000Z"
    endTimeUtc = "2026-01-01T00:00:00.0000000Z"
    durationMs = 0
    command = [pscustomobject]@{}
    artifacts = [pscustomobject]@{}
    diagnostics = [pscustomobject]@{ validateOnly = $true }
    observations = [pscustomobject]@{}
    stepDiagnostics = @([pscustomobject]@{ stepIndex = 0; stepType = "sample"; status = "FAIL"; durationMs = 0; failureReason = "sample" })
    providerRuntimeEvidence = [pscustomobject]@{ configOnly = $false; dialogueSessionStarted = $false; dialogueResponseReceived = $false }
    visibleBehaviorEvidence = [pscustomobject]@{}
    failures = $sampleFailures
}
Assert-ResultSchema $sampleResult $sampleResultPath

if ($failures.Count -gt 0) {
    Write-Host "CONTRACT_FAIL: test-system contract violations found:"
    foreach ($failure in $failures) { Write-Host "  - $failure" }
    exit 1
}

Write-Host "CONTRACT_PASS: test-system contract verified"
exit 0
