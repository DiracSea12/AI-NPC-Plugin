$ErrorActionPreference = "Stop"

if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
    throw "Node.js is required for Context7 setup."
}

if ([string]::IsNullOrWhiteSpace($env:CONTEXT7_API_KEY)) {
    Write-Host "CONTEXT7_API_KEY is not set." -ForegroundColor Yellow
    Write-Host "Set it first, then rerun this script." -ForegroundColor Yellow
    Write-Host "Manual fallback: npx ctx7 setup --project --codex --claude --universal --mcp" -ForegroundColor Yellow
    exit 1
}

$commonArgs = @(
    "ctx7",
    "setup",
    "--project",
    "--yes",
    "--api-key",
    $env:CONTEXT7_API_KEY
)

& npx @($commonArgs + @("--codex", "--cli"))
& npx @($commonArgs + @("--claude", "--cli"))
& npx @($commonArgs + @("--universal"))
& npx @($commonArgs + @("--mcp"))
