$ErrorActionPreference = "Stop"

$checks = @(
    @{ Path = "Plugins/AINpc/Source/AINpcCore/Public/LLM/LLMProviderTypes.h"; Pattern = "FString BaseUrl"; Label = "FLLMRequest carries BaseUrl" }
    @{ Path = "Plugins/AINpc/Source/AINpcCore/Public/LLM/OpenAIProvider.h"; Pattern = "FString InBaseUrl"; Label = "OpenAI provider constructor accepts BaseUrl" }
    @{ Path = "Plugins/AINpc/Source/AINpcCore/Private/LLM/OpenAIProvider.cpp"; Pattern = "FString FOpenAIProvider::ResolveBaseUrl"; Label = "OpenAI provider resolves BaseUrl" }
)

$missing = @()
foreach ($check in $checks) {
    if (-not (Select-String -Path $check.Path -Pattern $check.Pattern -Quiet)) {
        $missing += $check.Label
    }
}

if ($missing.Count -gt 0) {
    Write-Error ("BaseURL contract failed: " + ($missing -join "; "))
}

Write-Host "PASS: BaseURL contract verified"
