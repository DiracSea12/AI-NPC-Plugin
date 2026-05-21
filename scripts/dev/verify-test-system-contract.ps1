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
    "startTimeUtc", "endTimeUtc", "durationMs", "command", "artifacts", "diagnostics", "observations", "failures"
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

# 7.2: stale unrelated paths in matrix or gate scripts.
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

# 8.1-8.3: Ralph matrix delegates to supported scripts and declares expected result contract.
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

# 7.3: root test-*.bat wrappers are thin wrappers around matching scripts/dev entry points.
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

# 7.4: visual manifest, scripts, and C++ registry TestIds must line up.
$manifestPath = Join-Path $repoRoot "scripts/dev/game/visual-tests.json"
$manifest = @(Get-JsonFile $manifestPath)
$registryPath = Join-Path $repoRoot "Plugins/AINpc/Source/AINpcCore/Private/Test/AINpcVisualTestRegistry.cpp"
$registryText = Get-Content -Path $registryPath -Raw
$registryIds = @([regex]::Matches($registryText, 'TEXT\("([^"]+)"\)') | ForEach-Object { $_.Groups[1].Value } | Where-Object { $_ -match '^us[0-9]+\.[a-z0-9.-]+$' } | Sort-Object -Unique)
$manifestIds = @($manifest | ForEach-Object { [string]$_.testId })
$discoveredVisualScripts = @(Get-ChildItem -Path (Join-Path $repoRoot "scripts/dev/game") -Filter "test-*.ps1" -File | ForEach-Object { Get-RelativePath $_.FullName })
foreach ($group in $manifestIds | Group-Object) {
    if ($group.Count -gt 1) { Add-Failure "Duplicate visual manifest TestId '$($group.Name)'." }
}
foreach ($id in $manifestIds) {
    if ($registryIds -notcontains $id) { Add-Failure "Visual manifest TestId '$id' is not registered in FAINpcVisualTestRegistry." }
}
foreach ($id in $registryIds) {
    if ($manifestIds -notcontains $id) { Add-Failure "Registered visual TestId '$id' has no scripts/dev/game/visual-tests.json entry." }
}
foreach ($script in $discoveredVisualScripts) {
    $matchingEntries = @($manifest | Where-Object { ([string]$_.script).Replace('\\', '/') -eq $script })
    if ($matchingEntries.Count -ne 1) {
        Add-Failure "Dynamically discovered visual script must have exactly one manifest entry: $script"
    }
}
foreach ($entry in $manifest) {
    $scriptPath = Join-Path $repoRoot ([string]$entry.script)
    if (-not (Test-Path $scriptPath)) {
        Add-Failure "Visual manifest script is missing for '$($entry.testId)': $($entry.script)"
        continue
    }
    if ($discoveredVisualScripts -notcontains ([string]$entry.script).Replace('\\', '/')) {
        Add-Failure "Visual manifest script is not dynamically discovered by scripts/dev/game/test-*.ps1: $($entry.script)"
    }
    $scriptText = Get-Content -Path $scriptPath -Raw
    if ($scriptText -notmatch [regex]::Escape([string]$entry.testId)) {
        Add-Failure "Visual script '$($entry.script)' does not reference manifest TestId '$($entry.testId)'."
    }
}

# 7.5: C++ Automation test paths under Private/Tests use AINpc. prefix.
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

# 7.6a: suites must aggregate deterministic child artifacts, not guess by latest timestamp.
$testGameScript = Join-Path $repoRoot "scripts/dev/test-game.ps1"
$testGameText = Get-Content -Path $testGameScript -Raw
if ($testGameText -match "Get-AINpcLatestResultPath") {
    Add-Failure "test-game.ps1 must not use Get-AINpcLatestResultPath; pass deterministic child RunId/ResultPath instead."
}
if ($testGameText -notmatch "Get-ChildItem[\s\S]*test-\*\.ps1") {
    Add-Failure "test-game.ps1 must dynamically discover scripts/dev/game/test-*.ps1."
}
if ($testGameText -notmatch '"-RunId"' -or $testGameText -notmatch "childRunId") {
    Add-Failure "test-game.ps1 must pass a deterministic child RunId to visual game scripts."
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

# 7.6: final visual acceptance scripts must not use forbidden headless/mock/bypass final modes.
$visualScripts = @()
$visualScripts += Get-ChildItem -Path (Join-Path $repoRoot "scripts/dev/game") -Filter "test-*.ps1" -File
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
    foreach ($pattern in $forbiddenVisualPatterns) {
        if ($text -match $pattern) {
            Add-Failure "Forbidden visual acceptance mode '$pattern' appears in $(Get-RelativePath $file.FullName)."
        }
    }
}

# 6.3: editor-context script must use visible UnrealEditor and not present itself as visual-game acceptance.
$editorScript = Join-Path $repoRoot "scripts/dev/test-editor-context.ps1"
$editorText = Get-Content -Path $editorScript -Raw
if ($editorText -notmatch 'Start-Process\s+-FilePath\s+\$editor' -or $editorText -match "UnrealEditor-Cmd|(?i)-nullrhi|(?i)-unattended") {
    Add-Failure "test-editor-context.ps1 must preserve visible UnrealEditor execution without headless/commandlet flags."
}
if ($editorText -notmatch "not final player-visible NPC behavior acceptance") {
    Add-Failure "test-editor-context.ps1 must explicitly state editor automation is not final player-visible NPC behavior acceptance."
}


# Security: visual runtime artifacts must not persist raw command lines or provider bodies.
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

$visualTestPaths = @(
    (Join-Path $repoRoot "Plugins/AINpc/Source/AINpcCore/Private/Test/AINpcUs1DialogueActionVisualTest.cpp"),
    (Join-Path $repoRoot "Plugins/AINpc/Source/AINpcCore/Private/Test/AINpcUs2PerceptionBehaviorVisualTest.cpp")
)
$rawVisualTextPatterns = @(
    'ShowStatus.*(Text|TrimmedText|LastNpcResponseText|FallbackResponse)\.Left\s*\(',
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

# 7.7: validate result schema deterministically. Do not scan historical Saved/TestLogs;
# stale local artifacts are arbitrary trash and must not decide static contract status.
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
