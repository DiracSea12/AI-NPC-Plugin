#include "Components/AINpcProviderConfigResolver.h"

#include "Components/AINpcComponent.h"
#include "Data/NpcPersonaDataAsset.h"
#include "HAL/PlatformMisc.h"
#include "LLM/OpenAIProvider.h"
#include "Settings/AINpcSettings.h"

namespace
{
	FString ResolveApiKeyFromEnvironment()
	{
		return FPlatformMisc::GetEnvironmentVariable(TEXT("AINPC_OPENAI_API_KEY")).TrimStartAndEnd();
	}
}

FAINpcProviderBootstrapConfig FAINpcProviderConfigResolver::ResolveBootstrapConfig(const UAINpcComponent& Component)
{
	(void)Component;

	FAINpcProviderBootstrapConfig Config;

	const UAINpcSettings* Settings = GetDefault<UAINpcSettings>();
	Config.ApiKey = Settings ? Settings->GlobalApiKey.TrimStartAndEnd() : FString();
	if (Config.ApiKey.IsEmpty())
	{
		Config.ApiKey = ResolveApiKeyFromEnvironment();
	}

	Config.BaseUrl = Settings ? Settings->GlobalBaseUrl.TrimStartAndEnd() : TEXT("https://api.openai.com/v1");
	Config.Model = Settings ? Settings->GlobalModel.TrimStartAndEnd() : TEXT("gpt-4o-mini");
	return Config;
}

void FAINpcProviderConfigResolver::ApplyRequestConfig(const UAINpcComponent& Component, FLLMRequest& Request)
{
	const UAINpcSettings* Settings = GetDefault<UAINpcSettings>();
	if (Settings)
	{
		Request.ApiKey = Settings->GlobalApiKey.TrimStartAndEnd();
		Request.BaseUrl = Settings->GlobalBaseUrl.TrimStartAndEnd();
		Request.Model = Settings->GlobalModel.TrimStartAndEnd();
		Request.TimeoutSeconds = FMath::Max(0.0f, Settings->RequestTimeoutSeconds);
	}

	if (Component.PersonaDataAsset)
	{
		const FString PersonaApiKey = Component.PersonaDataAsset->ApiKey.TrimStartAndEnd();
		if (!PersonaApiKey.IsEmpty())
		{
			Request.ApiKey = PersonaApiKey;
		}

		const FString PersonaBaseUrl = Component.PersonaDataAsset->BaseUrl.TrimStartAndEnd();
		if (!PersonaBaseUrl.IsEmpty())
		{
			Request.BaseUrl = PersonaBaseUrl;
		}

		const FString PersonaModel = Component.PersonaDataAsset->Model.TrimStartAndEnd();
		if (!PersonaModel.IsEmpty())
		{
			Request.Model = PersonaModel;
		}
	}

	const FString ApiKeyOverride = Component.ApiKeyOverride.TrimStartAndEnd();
	if (!ApiKeyOverride.IsEmpty())
	{
		Request.ApiKey = ApiKeyOverride;
	}

	const FString BaseUrlOverride = Component.BaseUrlOverride.TrimStartAndEnd();
	if (!BaseUrlOverride.IsEmpty())
	{
		Request.BaseUrl = BaseUrlOverride;
	}

	const FString ModelOverride = Component.ModelOverride.TrimStartAndEnd();
	if (!ModelOverride.IsEmpty())
	{
		Request.Model = ModelOverride;
	}

	if (Request.ApiKey.IsEmpty())
	{
		Request.ApiKey = ResolveApiKeyFromEnvironment();
	}
}

TSharedPtr<FOpenAIProvider, ESPMode::ThreadSafe> FAINpcProviderConfigResolver::CreateProvider(const UAINpcComponent& Component)
{
	const FAINpcProviderBootstrapConfig Config = ResolveBootstrapConfig(Component);
	return MakeShared<FOpenAIProvider, ESPMode::ThreadSafe>(Config.ApiKey, Config.Model, Config.BaseUrl);
}
