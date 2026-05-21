$script:AINpcResultSchemaVersion = 1
$script:AINpcResultStatuses = @("PASS", "FAIL", "SKIP", "BLOCKED")
$script:AINpcResultLayers = @("build", "static", "editor-automation", "provider-connectivity", "visual-game", "full-suite")

function Get-AINpcRepoRoot {
    return (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
}

function New-AINpcRunId {
    param([string]$Prefix)
    $stamp = (Get-Date).ToUniversalTime().ToString("yyyyMMdd-HHmmss")
    $suffix = [System.Guid]::NewGuid().ToString("N").Substring(0, 8)
    if ([string]::IsNullOrWhiteSpace($Prefix)) {
        return "$stamp-$suffix"
    }
    return "$Prefix-$stamp-$suffix"
}

function Assert-AINpcResultStatus {
    param([string]$Status)
    if ($script:AINpcResultStatuses -notcontains $Status) {
        throw "Invalid test result status '$Status'. Expected one of: $($script:AINpcResultStatuses -join ', ')"
    }
}

function Assert-AINpcResultLayer {
    param([string]$Layer)
    if ($script:AINpcResultLayers -notcontains $Layer) {
        throw "Invalid test result layer '$Layer'. Expected one of: $($script:AINpcResultLayers -join ', ')"
    }
}

function ConvertTo-AINpcRepoRelativePath {
    param([AllowNull()][string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) {
        return $null
    }

    $repoRoot = [System.IO.Path]::GetFullPath((Get-AINpcRepoRoot))
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $repoWithSeparator = $repoRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar

    if ($fullPath.StartsWith($repoWithSeparator, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $fullPath.Substring($repoWithSeparator.Length).Replace('\', '/')
    }

    return $fullPath
}

function New-AINpcTestRunContext {
    param(
        [Parameter(Mandatory = $true)][string]$Layer,
        [string]$RunId
    )

    Assert-AINpcResultLayer $Layer
    if ([string]::IsNullOrWhiteSpace($RunId)) {
        $RunId = New-AINpcRunId $Layer
    }

    $repoRoot = Get-AINpcRepoRoot
    $runDir = Join-Path (Join-Path (Join-Path $repoRoot "Saved/TestLogs") $Layer) $RunId
    New-Item -ItemType Directory -Force -Path $runDir | Out-Null

    return [pscustomobject]@{
        Layer = $Layer
        RunId = $RunId
        RunDir = $runDir
        ResultPath = (Join-Path $runDir "result.json")
    }
}

function ConvertTo-AINpcMaskedSecret {
    param([AllowNull()][string]$Value)
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return "missing"
    }
    $trimmed = $Value.Trim()
    if ($trimmed.Length -le 8) {
        return "present(length=$($trimmed.Length))"
    }
    return "present(****$($trimmed.Substring($trimmed.Length - 4)))"
}

function ConvertTo-AINpcJsonArray {
    param([AllowNull()][object]$Value)

    $items = [System.Collections.Generic.List[object]]::new()
    if ($null -eq $Value) {
        return ,$items
    }

    if ($Value -is [System.Collections.IEnumerable] -and -not ($Value -is [string])) {
        foreach ($item in $Value) {
            $items.Add($item)
        }
    }
    else {
        $items.Add($Value)
    }

    return ,$items
}

function ConvertTo-AINpcRedactedText {
    param(
        [AllowNull()][string]$Text,
        [string[]]$Secrets = @(),
        [int]$MaxLength = 1200
    )

    if ([string]::IsNullOrEmpty($Text)) {
        return ""
    }

    $redacted = $Text
    foreach ($secret in @($Secrets)) {
        if (-not [string]::IsNullOrWhiteSpace($secret)) {
            $redacted = $redacted.Replace($secret, "<redacted-secret>")
        }
    }

    $sensitiveNames = '(Authorization|x-api-key|apiKey|token|password|secret|bearer)'
    $keyValuePattern = '(?i)(["'']?' + $sensitiveNames + '["'']?\s*[:=]\s*["'']?)(Bearer\s+)?[^\s,''";}\]]+'
    $adjacentValuePattern = '(?i)((?:--?|/)?' + $sensitiveNames + '\s+)(Bearer\s+)?[^\s,''";}\]]+'
    $bearerPattern = '(?i)(Bearer\s+)[^\s,''";}\]]+'
    $redacted = [regex]::Replace($redacted, $keyValuePattern, '$1<redacted>')
    $redacted = [regex]::Replace($redacted, $adjacentValuePattern, '$1<redacted>')
    $redacted = [regex]::Replace($redacted, $bearerPattern, '$1<redacted>')

    if ($redacted.Length -gt $MaxLength) {
        return $redacted.Substring(0, $MaxLength) + "...<truncated>"
    }
    return $redacted
}

function ConvertTo-AINpcRedactedObject {
    param([AllowNull()][object]$Value)

    if ($null -eq $Value) {
        return $null
    }
    if ($Value -is [string]) {
        return ConvertTo-AINpcRedactedText -Text $Value -MaxLength 1200
    }
    if ($Value -is [System.Collections.IDictionary]) {
        $copy = [ordered]@{}
        foreach ($key in $Value.Keys) {
            $copy[$key] = ConvertTo-AINpcRedactedObject -Value $Value[$key]
        }
        return $copy
    }
    if ($Value -is [System.Management.Automation.PSCustomObject]) {
        $copy = [ordered]@{}
        foreach ($property in $Value.PSObject.Properties) {
            $copy[$property.Name] = ConvertTo-AINpcRedactedObject -Value $property.Value
        }
        return $copy
    }
    if ($Value -is [System.Collections.IEnumerable] -and -not ($Value -is [string])) {
        $items = [System.Collections.Generic.List[object]]::new()
        foreach ($item in $Value) {
            $items.Add((ConvertTo-AINpcRedactedObject -Value $item))
        }
        return ,$items
    }
    return $Value
}

function ConvertTo-AINpcRedactedErrorSummary {
    param(
        [AllowNull()][object]$ErrorRecord,
        [string[]]$Secrets = @()
    )

    if ($null -eq $ErrorRecord) {
        return ""
    }
    return ConvertTo-AINpcRedactedText -Text ([string]$ErrorRecord.Exception.Message) -Secrets $Secrets -MaxLength 600
}

function New-AINpcCommandInfo {
    param(
        [AllowNull()][string]$Executable,
        [string[]]$Arguments = @(),
        [AllowNull()][string]$WorkingDirectory,
        [AllowNull()][string]$Display
    )

    $safeArgs = @($Arguments | ForEach-Object { ConvertTo-AINpcRedactedText -Text ([string]$_) -MaxLength 300 })
    if ([string]::IsNullOrWhiteSpace($Display)) {
        $Display = ((@($Executable) + $safeArgs) -join " ").Trim()
    }

    return [ordered]@{
        executable = $Executable
        arguments = $safeArgs
        workingDirectory = $WorkingDirectory
        display = (ConvertTo-AINpcRedactedText -Text $Display -MaxLength 1000)
    }
}

function Write-AINpcTestResult {
    param(
        [Parameter(Mandatory = $true)][object]$Context,
        [Parameter(Mandatory = $true)][string]$Status,
        [Parameter(Mandatory = $true)][datetime]$StartTimeUtc,
        [Parameter(Mandatory = $true)][datetime]$EndTimeUtc,
        [AllowNull()][object]$Command = $null,
        [AllowNull()][object]$Artifacts = $null,
        [AllowNull()][object]$Diagnostics = $null,
        [AllowNull()][object]$Observations = $null,
        [object[]]$Failures = @(),
        [AllowNull()][object]$TestId = $null,
        [string[]]$StoryIds = @(),
        [string[]]$PhaseIds = @("phase2.5")
    )

    Assert-AINpcResultLayer $Context.Layer
    Assert-AINpcResultStatus $Status

    $durationMs = [int][Math]::Max(0, [Math]::Round(($EndTimeUtc - $StartTimeUtc).TotalMilliseconds))
    if ($null -eq $Command) { $Command = [ordered]@{} }
    if ($null -eq $Artifacts) { $Artifacts = [ordered]@{} }
    if ($null -eq $Diagnostics) { $Diagnostics = [ordered]@{} }
    if ($null -eq $Observations) { $Observations = [ordered]@{} }

    $result = [ordered]@{
        schemaVersion = $script:AINpcResultSchemaVersion
        runId = $Context.RunId
        layer = $Context.Layer
        testId = $(if ([string]::IsNullOrWhiteSpace([string]$TestId)) { $null } else { [string]$TestId })
        storyIds = ConvertTo-AINpcJsonArray $StoryIds
        phaseIds = ConvertTo-AINpcJsonArray $PhaseIds
        status = $Status
        startTimeUtc = $StartTimeUtc.ToUniversalTime().ToString("o")
        endTimeUtc = $EndTimeUtc.ToUniversalTime().ToString("o")
        durationMs = $durationMs
        command = $Command
        artifacts = $Artifacts
        diagnostics = $Diagnostics
        observations = $Observations
        failures = ConvertTo-AINpcJsonArray $Failures
    }

    $result = ConvertTo-AINpcRedactedObject -Value $result
    $result | ConvertTo-Json -Depth 20 | Set-Content -Path $Context.ResultPath -Encoding UTF8
    Write-Host "Result: $($Context.ResultPath)"
    return $Context.ResultPath
}

function Get-AINpcLatestResultPath {
    param([Parameter(Mandatory = $true)][string]$Layer)
    Assert-AINpcResultLayer $Layer
    $repoRoot = Get-AINpcRepoRoot
    $layerRoot = Join-Path (Join-Path $repoRoot "Saved/TestLogs") $Layer
    if (-not (Test-Path $layerRoot)) {
        return $null
    }
    $latest = Get-ChildItem -Path $layerRoot -Filter "result.json" -Recurse -File |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1
    if ($latest) {
        return $latest.FullName
    }
    return $null
}
