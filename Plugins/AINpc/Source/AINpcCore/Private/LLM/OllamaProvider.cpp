#include "LLM/OllamaProvider.h"

FOllamaProvider::FOllamaProvider(FString InDefaultModel, FString InBaseUrl)
	: FOpenAIProvider(FString(), MoveTemp(InDefaultModel), MoveTemp(InBaseUrl))
{
}

FLLMProviderCapabilities FOllamaProvider::GetCapabilities() const
{
	FLLMProviderCapabilities Capabilities;
	Capabilities.bSupportsStreaming = false;
	Capabilities.bSupportsFunctionCalling = true;
	Capabilities.bSupportsJsonMode = true;
	return Capabilities;
}
