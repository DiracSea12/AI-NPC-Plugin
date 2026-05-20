$ErrorActionPreference = "Stop"

$checks = @(
    @{ Path = "Plugins/AINpc/Source/AINpcCore/Public/LLM/SSEParser.h"; Pattern = "FSSEParser"; Label = "SSE parser type exists" }
    @{ Path = "Plugins/AINpc/Source/AINpcCore/Private/LLM/SSEParser.cpp"; Pattern = "ProcessChunk"; Label = "SSE parser processes streamed chunks" }
    @{ Path = "Plugins/AINpc/Source/AINpcCore/Private/LLM/SSEParser.cpp"; Pattern = "\[DONE\]"; Label = "SSE parser handles done sentinel" }
    @{ Path = "Plugins/AINpc/Source/AINpcCore/Private/LLM/SSEParser.cpp"; Pattern = "data:"; Label = "SSE parser handles data prefix" }
    @{ Path = "Plugins/AINpc/Source/AINpcCore/Private/LLM/SSEParser.cpp"; Pattern = "event:"; Label = "SSE parser handles event lines" }
)

$missing = @()
foreach ($check in $checks) {
    if (-not (Select-String -Path $check.Path -Pattern $check.Pattern -Quiet)) {
        $missing += $check.Label
    }
}

if ($missing.Count -gt 0) {
    Write-Error ("SSE contract failed: " + ($missing -join "; "))
}

Write-Host "PASS: SSE contract verified"
