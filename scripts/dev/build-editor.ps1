param(
    [string]$Configuration = "Development",
    [string]$RunId
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "TestResult.ps1")

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ubt = "G:\UE5\UnrealEngine\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe"
$uproject = Join-Path $repoRoot "VerifierHost.uproject"
$context = New-AINpcTestRunContext -Layer "build" -RunId $RunId
$startTime = (Get-Date).ToUniversalTime()
$status = "PASS"
$failures = @()
$diagnostics = [ordered]@{
    configuration = $Configuration
    target = "VerifierHostEditor"
    platform = "Win64"
    exitCode = $null
}
$commandArgs = @(
    "VerifierHostEditor",
    "Win64",
    $Configuration,
    "-Project=$uproject",
    "-WaitMutex",
    "-FromMsBuild"
)
$command = New-AINpcCommandInfo -Executable $ubt -Arguments $commandArgs -WorkingDirectory $repoRoot

try {
    if (-not (Test-Path $ubt)) {
        throw "UnrealBuildTool not found at $ubt"
    }

    if (-not (Test-Path $uproject)) {
        throw "Project file not found at $uproject"
    }

    & $ubt VerifierHostEditor Win64 $Configuration "-Project=$uproject" -WaitMutex -FromMsBuild
    $diagnostics.exitCode = $LASTEXITCODE
    if ($LASTEXITCODE -ne 0) {
        throw "UnrealBuildTool failed with exit code $LASTEXITCODE"
    }
}
catch {
    $status = "FAIL"
    $failures += [ordered]@{
        id = "build-editor"
        message = ConvertTo-AINpcRedactedErrorSummary -ErrorRecord $_
        artifact = $null
    }
}
finally {
    $endTime = (Get-Date).ToUniversalTime()
    Write-AINpcTestResult -Context $context -Status $status -StartTimeUtc $startTime -EndTimeUtc $endTime -Command $command -Artifacts ([ordered]@{
        uproject = ConvertTo-AINpcRepoRelativePath $uproject
        unrealBuildTool = $ubt
        result = ConvertTo-AINpcRepoRelativePath $context.ResultPath
    }) -Diagnostics $diagnostics -Observations ([ordered]@{}) -Failures $failures | Out-Null
}

if ($status -ne "PASS") {
    exit 1
}
