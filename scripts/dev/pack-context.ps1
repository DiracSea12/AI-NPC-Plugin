param(
    [switch]$Compress
)

$ErrorActionPreference = "Stop"

if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
    throw "Node.js is required to run Repomix via npx."
}

$artifactDir = Join-Path (Get-Location) ".artifacts/ai"
New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null

$outputPath = Join-Path $artifactDir "ainpc-repo.xml"
$args = @(
    "repomix",
    "--config",
    "repomix.config.json",
    "--output",
    $outputPath,
    "--quiet"
)

if ($Compress) {
    $args += "--compress"
}

& npx @args

if (-not (Test-Path $outputPath)) {
    throw "Repomix reported success but no output file was found at $outputPath"
}

Write-Host ("Repomix bundle written to {0}" -f $outputPath)
