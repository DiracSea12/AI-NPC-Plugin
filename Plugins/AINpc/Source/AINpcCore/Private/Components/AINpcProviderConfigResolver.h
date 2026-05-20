#pragma once

#include "CoreMinimal.h"
#include "LLM/LLMProviderTypes.h"

class ILLMProvider;
class UAINpcComponent;

struct FAINpcProviderBootstrapConfig
{
	FString ProviderType = TEXT("openai");
	FString ApiKey;
	FString BaseUrl;
	FString Model;
};

struct FAINpcRequestDefaults
{
	FString BaseUrl;
	FString Model;
	FString ApiKey;
};

class FAINpcProviderConfigResolver
{
public:
	static FAINpcProviderBootstrapConfig ResolveBootstrapConfig(const UAINpcComponent& Component);
	static FAINpcRequestDefaults ResolveRequestDefaults(const UAINpcComponent& Component);
	static void ApplyRequestConfig(const UAINpcComponent& Component, FLLMRequest& Request);
	static TSharedPtr<ILLMProvider, ESPMode::ThreadSafe> CreateProvider(const UAINpcComponent& Component);
};
