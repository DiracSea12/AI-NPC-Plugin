param(
    [int]$TimeoutSec = 45,
    [string]$ProxyUrl = "http://127.0.0.1:7897",
    [string]$RunId
)

$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "TestResult.ps1")

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent (Split-Path -Parent $scriptRoot)
$configPath = Join-Path $repoRoot "Config/AINpcLocalProvider.json"
$context = New-AINpcTestRunContext -Layer "provider-connectivity" -RunId $RunId
$startTime = (Get-Date).ToUniversalTime()
$status = "PASS"
$failures = @()
$endpoint = $null
$httpStatus = $null
$latencyMs = $null
$provider = $null
$baseUrl = $null
$model = $null
$effortLevel = $null
$apiKey = $null

function Join-ProviderEndpoint([string]$BaseUrl, [string]$Suffix) {
    $base = $BaseUrl.Trim().TrimEnd('/')
    if ($base.EndsWith($Suffix, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $base
    }
    return "$base$Suffix"
}

$commandArgs = @("-File", $MyInvocation.MyCommand.Path, "-TimeoutSec", ([string]$TimeoutSec), "-ProxyUrl", $ProxyUrl)
if (-not [string]::IsNullOrWhiteSpace($RunId)) { $commandArgs += @("-RunId", $RunId) }
$command = New-AINpcCommandInfo -Executable "pwsh" -Arguments $commandArgs -WorkingDirectory $repoRoot

try {
    if (-not (Test-Path $configPath)) {
        $status = "BLOCKED"
        throw "Provider config not found: $configPath"
    }

    $config = Get-Content -Path $configPath -Raw | ConvertFrom-Json
    $provider = ([string]$config.provider).Trim().ToLowerInvariant()
    $apiKey = ([string]$config.apiKey).Trim()
    $baseUrl = ([string]$config.baseUrl).Trim()
    $model = ([string]$config.model).Trim()
    $effortLevel = ([string]$config.effortLevel).Trim()

    if ([string]::IsNullOrWhiteSpace($provider)) { $status = "BLOCKED"; throw "Config field 'provider' is required in $configPath" }
    if ([string]::IsNullOrWhiteSpace($apiKey)) { $status = "BLOCKED"; throw "Config field 'apiKey' is required in $configPath" }
    if ([string]::IsNullOrWhiteSpace($baseUrl)) { $status = "BLOCKED"; throw "Config field 'baseUrl' is required in $configPath" }
    if ([string]::IsNullOrWhiteSpace($model)) { $status = "BLOCKED"; throw "Config field 'model' is required in $configPath" }

    Write-Host "Provider connectivity diagnostic"
    Write-Host "Config: $configPath"
    Write-Host "Provider: $provider"
    Write-Host "BaseUrl: $baseUrl"
    Write-Host "Model: $model"
    Write-Host "EffortLevel: $effortLevel"
    Write-Host "ApiKey: $(ConvertTo-AINpcMaskedSecret $apiKey)"

    $headers = @{
        "Content-Type" = "application/json"
    }

    if ($provider -eq "anthropic") {
        $endpoint = Join-ProviderEndpoint $baseUrl "/v1/messages"
        $headers["x-api-key"] = $apiKey
        $headers["anthropic-version"] = "2023-06-01"
        $bodyObject = [ordered]@{
            model = $model
            max_tokens = 32
            messages = @(
                @{ role = "user"; content = '请只回复 JSON：{"ok":true}' }
            )
        }
        if (-not [string]::IsNullOrWhiteSpace($effortLevel)) {
            $bodyObject.effortLevel = $effortLevel
        }
    }
    elseif ($provider -eq "openai") {
        $endpoint = Join-ProviderEndpoint $baseUrl "/chat/completions"
        $headers["Authorization"] = "Bearer $apiKey"
        $bodyObject = [ordered]@{
            model = $model
            max_tokens = 32
            messages = @(
                @{ role = "user"; content = '请只回复 JSON：{"ok":true}' }
            )
        }
        if (-not [string]::IsNullOrWhiteSpace($effortLevel)) {
            $bodyObject.effortLevel = $effortLevel
        }
    }
    else {
        $status = "BLOCKED"
        throw "Unsupported provider '$provider'. Expected 'anthropic' or 'openai'."
    }

    $body = $bodyObject | ConvertTo-Json -Depth 10 -Compress
    Write-Host "Endpoint: $endpoint"
    Write-Host "TimeoutSec: $TimeoutSec"
    Write-Host "Proxy: $ProxyUrl"

    $requestArgs = @{
        Uri = $endpoint
        Method = "Post"
        Headers = $headers
        Body = $body
        TimeoutSec = $TimeoutSec
        ErrorAction = "Stop"
    }
    if (-not [string]::IsNullOrWhiteSpace($ProxyUrl)) {
        $requestArgs.Proxy = $ProxyUrl
    }

    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        $response = Invoke-WebRequest @requestArgs
        $stopwatch.Stop()
        $latencyMs = [int]$stopwatch.ElapsedMilliseconds
        $httpStatus = [int]$response.StatusCode
        Write-Host "HTTP status: $httpStatus"
        Write-Host "DurationMs: $latencyMs"

        $json = $response.Content | ConvertFrom-Json
        if (-not $json) {
            $status = "FAIL"
            throw "Response JSON parsed to null."
        }

        Write-Host "PASS: provider accepted the diagnostic request and returned JSON"
    }
    catch {
        $stopwatch.Stop()
        $latencyMs = [int]$stopwatch.ElapsedMilliseconds
        if ($_.Exception.Response -and $_.Exception.Response.StatusCode) {
            $httpStatus = [int]$_.Exception.Response.StatusCode
            $status = "FAIL"
        }
        elseif ($status -eq "PASS") {
            $status = "BLOCKED"
        }
        throw
    }
}
catch {
    if ($status -eq "PASS") {
        $status = "FAIL"
    }
    $message = ConvertTo-AINpcRedactedErrorSummary -ErrorRecord $_ -Secrets @($apiKey)
    Write-Host "FAIL: provider connectivity diagnostic failed"
    Write-Host "Error: $message"
    $failures += [ordered]@{
        id = "provider-connectivity"
        message = $message
        artifact = $null
    }
}
finally {
    $endTime = (Get-Date).ToUniversalTime()
    $diagnostics = [ordered]@{
        provider = $provider
        baseUrl = $baseUrl
        model = $model
        effortLevel = $effortLevel
        endpoint = $endpoint
        httpStatus = $httpStatus
        latencyMs = $latencyMs
        apiKey = ConvertTo-AINpcMaskedSecret $apiKey
        proxy = $(if ([string]::IsNullOrWhiteSpace($ProxyUrl)) { "none" } else { "configured" })
        timeoutSec = $TimeoutSec
        errorSummary = $(if ($failures.Count -gt 0) { $failures[0].message } else { "" })
    }

    Write-AINpcTestResult -Context $context -Status $status -StartTimeUtc $startTime -EndTimeUtc $endTime -Command $command -Artifacts ([ordered]@{
        config = ConvertTo-AINpcRepoRelativePath $configPath
        result = ConvertTo-AINpcRepoRelativePath $context.ResultPath
    }) -Diagnostics $diagnostics -Observations ([ordered]@{
        responseJsonParsed = ($status -eq "PASS")
    }) -Failures $failures | Out-Null
}

if ($status -eq "PASS") {
    exit 0
}
exit 1
