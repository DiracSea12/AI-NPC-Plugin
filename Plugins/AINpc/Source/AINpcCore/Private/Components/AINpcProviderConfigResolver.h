#pragma once

#include "CoreMinimal.h"
#include "LLM/LLMProviderTypes.h"

class FOpenAIProvider;
class UAINpcComponent;

struct FAINpcProviderBootstrapConfig
{
	FString ApiKey;
	FString BaseUrl;
	FString Model;
};

class FAINpcProviderConfigResolver
{
public:
	static FAINpcProviderBootstrapConfig ResolveBootstrapConfig(const UAINpcComponent& Component);
	static void ApplyRequestConfig(const UAINpcComponent& Component, FLLMRequest& Request);
	static TSharedPtr<FOpenAIProvider, ESPMode::ThreadSafe> CreateProvider(const UAINpcComponent& Component);
};
