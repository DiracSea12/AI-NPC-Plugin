param(
    [int]$TimeoutSec = 45,
    [string]$ProxyUrl = "http://127.0.0.1:7897"
)

$ErrorActionPreference = "Stop"
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
& (Join-Path $scriptRoot "test-provider.ps1") -TimeoutSec $TimeoutSec -ProxyUrl $ProxyUrl
exit $LASTEXITCODE
