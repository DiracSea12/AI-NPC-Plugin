#pragma once

#include "LLM/OpenAIProvider.h"

class AINPCCORE_API FOllamaProvider : public FOpenAIProvider
{
public:
	explicit FOllamaProvider(
		FString InDefaultModel = FString(),
		FString InBaseUrl = FString());

	virtual FLLMProviderCapabilities GetCapabilities() const override;
};
