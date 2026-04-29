$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$targets = @(
    "Binaries",
    "DerivedDataCache",
    "Intermediate",
    "Saved",
    "Temp",
    "Plugins/AINpc/Binaries",
    "Plugins/AINpc/Intermediate",
    "Plugins/AINpc/Saved",
    "Plugins/AINpc/Plugins"
)

foreach ($target in $targets) {
    $fullPath = Join-Path $repoRoot $target
    if (Test-Path $fullPath) {
        Remove-Item -LiteralPath $fullPath -Recurse -Force
        Write-Host "Removed $fullPath"
    }
}
