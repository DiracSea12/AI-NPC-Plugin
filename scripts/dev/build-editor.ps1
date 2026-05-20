param(
    [string]$Configuration = "Development"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ubt = "G:\UE5\UnrealEngine\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe"
$uproject = Join-Path $repoRoot "VerifierHost.uproject"

if (-not (Test-Path $ubt)) {
    throw "UnrealBuildTool not found at $ubt"
}

if (-not (Test-Path $uproject)) {
    throw "Project file not found at $uproject"
}

& $ubt VerifierHostEditor Win64 $Configuration "-Project=$uproject" -WaitMutex -FromMsBuild
if ($LASTEXITCODE -ne 0) {
    throw "UnrealBuildTool failed with exit code $LASTEXITCODE"
}
