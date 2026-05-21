#pragma once

#include "CoreMinimal.h"
#include "LLM/LLMProviderTypes.h"

class ILLMProvider;
class UAINpcComponent;

struct FAINpcProviderBootstrapConfig
{
	bool bHasProviderSourceConfig = false;
	FString ProviderSourceError;
	FString ProviderType;
	FString ApiKey;
	FString BaseUrl;
	FString Model;
	FString EffortLevel;
	FString Protocol;
};

struct FAINpcRequestDefaults
{
	FString BaseUrl;
	FString Model;
	FString ApiKey;
	FString EffortLevel;
};

class FAINpcProviderConfigResolver
{
public:
	static FAINpcProviderBootstrapConfig ResolveBootstrapConfig(const UAINpcComponent& Component);
	static FAINpcRequestDefaults ResolveRequestDefaults(const UAINpcComponent& Component);
	static void ApplyRequestConfig(const UAINpcComponent& Component, FLLMRequest& Request);
	static TSharedPtr<ILLMProvider, ESPMode::ThreadSafe> CreateProvider(const UAINpcComponent& Component);

#if defined(WITH_DEV_AUTOMATION_TESTS) && WITH_DEV_AUTOMATION_TESTS
	static bool TryResolveBootstrapConfigFromJsonTextForTest(const FString& JsonText, FAINpcProviderBootstrapConfig& OutConfig);
	static void ApplyRequestConfigForConfigForTest(const UAINpcComponent& Component, const FAINpcProviderBootstrapConfig& BootstrapConfig, FLLMRequest& Request);
	static TSharedPtr<ILLMProvider, ESPMode::ThreadSafe> CreateProviderForConfigForTest(const FAINpcProviderBootstrapConfig& Config);
#endif
};
