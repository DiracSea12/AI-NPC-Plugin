#pragma once

#include "LLM/OpenAIProvider.h"

class AINPCCORE_API FOllamaProvider : public FOpenAIProvider
{
public:
	explicit FOllamaProvider(
		FString InDefaultModel = FString(),
		FString InBaseUrl = TEXT("http://localhost:11434/v1"));

	virtual FLLMProviderCapabilities GetCapabilities() const override;
};
