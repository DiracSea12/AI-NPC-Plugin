$ErrorActionPreference = "Stop"

function Test-CommandExists {
    param([string]$Name)
    return $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

$projectUniversalSkills = Test-Path (Join-Path (Get-Location) ".agents/skills")
$projectClaudeRule = Test-Path (Join-Path (Get-Location) ".claude/rules/context7.md")
$projectMcpConfig = Test-Path (Join-Path (Get-Location) ".mcp.json")
$projectContext7Ready = $projectUniversalSkills -or $projectClaudeRule -or $projectMcpConfig

$checks = @(
    [pscustomobject]@{ Name = "Node.js"; Available = (Test-CommandExists "node"); Hint = "Required for ctx7/repomix via npx." }
    [pscustomobject]@{ Name = "ctx7"; Available = ((Test-CommandExists "ctx7") -or $projectContext7Ready); Hint = "Current docs lookup. Project-local setup also counts." }
    [pscustomobject]@{ Name = "repomix"; Available = (Test-CommandExists "repomix"); Hint = "Optional. `npx repomix` also works." }
    [pscustomobject]@{ Name = "docker"; Available = (Test-CommandExists "docker"); Hint = "Optional. Needed later for Langfuse local deployment." }
)

foreach ($check in $checks) {
    $state = if ($check.Available) { "OK" } else { "MISSING" }
    Write-Host ("[{0}] {1} - {2}" -f $state, $check.Name, $check.Hint)
}

$context7KeyPresent = -not [string]::IsNullOrWhiteSpace($env:CONTEXT7_API_KEY)
Write-Host ("[INFO] CONTEXT7_API_KEY present: {0}" -f $context7KeyPresent)

$projectSkillDir = Join-Path (Get-Location) ".agents/skills"
$context7SkillPresent = Test-Path $projectSkillDir
Write-Host ("[INFO] Project-local universal skills directory present: {0}" -f $context7SkillPresent)
Write-Host ("[INFO] Claude Context7 rule present: {0}" -f $projectClaudeRule)
Write-Host ("[INFO] Project MCP config present: {0}" -f $projectMcpConfig)

if ((Test-CommandExists "node") -and -not $context7SkillPresent) {
    if ($context7KeyPresent) {
        Write-Host "[NEXT] Run: pwsh ./scripts/dev/setup-context7.ps1"
    } else {
        Write-Host "[NEXT] Set CONTEXT7_API_KEY, then run: pwsh ./scripts/dev/setup-context7.ps1"
    }
}

Write-Host "[NEXT] Large-context handoff: pwsh ./scripts/dev/pack-context.ps1"
Write-Host "[NEXT] Fast verification: pwsh ./scripts/dev/test-fast.ps1"
