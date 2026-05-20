#include "AINpcProviderConfigResolver.h"

#include "Components/AINpcComponent.h"
#include "Data/NpcPersonaDataAsset.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformMisc.h"
#include "LLM/AnthropicProvider.h"
#include "LLM/ILLMProvider.h"
#include "LLM/OpenAIProvider.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Settings/AINpcSettings.h"

namespace
{
	const TCHAR* LocalProviderConfigFileName = TEXT("AINpcLocalProvider.json");
	const TCHAR* DefaultOpenAIBaseUrl = TEXT("https://api.openai.com/v1");
	const TCHAR* DefaultAnthropicBaseUrl = TEXT("https://api.anthropic.com/v1");

	FString ResolveApiKeyFromEnvironment()
	{
		return FPlatformMisc::GetEnvironmentVariable(TEXT("AINPC_OPENAI_API_KEY")).TrimStartAndEnd();
	}

	FString NormalizeProviderType(const FString& ProviderType)
	{
		const FString Normalized = ProviderType.TrimStartAndEnd().ToLower();
		if (Normalized == TEXT("anthropic"))
		{
			return TEXT("anthropic");
		}

		return TEXT("openai");
	}

	FString ResolveDefaultBaseUrlForProvider(const FString& ProviderType)
	{
		return NormalizeProviderType(ProviderType) == TEXT("anthropic")
			? FString(DefaultAnthropicBaseUrl)
			: FString(DefaultOpenAIBaseUrl);
	}

	FString ResolveAnthropicModelFromEnvObject(const FJsonObject& EnvObject)
	{
		static const TCHAR* CandidateKeys[] =
		{
			TEXT("ANTHROPIC_MODEL"),
			TEXT("ANTHROPIC_DEFAULT_SONNET_MODEL"),
			TEXT("ANTHROPIC_DEFAULT_OPUS_MODEL"),
			TEXT("ANTHROPIC_DEFAULT_HAIKU_MODEL"),
			TEXT("CLAUDE_CODE_SUBAGENT_MODEL")
		};

		for (const TCHAR* CandidateKey : CandidateKeys)
		{
			FString Value;
			if (EnvObject.TryGetStringField(CandidateKey, Value))
			{
				Value = Value.TrimStartAndEnd();
				if (!Value.IsEmpty())
				{
					return Value;
				}
			}
		}

		return FString();
	}

	bool TryParseRepoLocalProviderConfig(FAINpcProviderBootstrapConfig& OutConfig)
	{
		const FString ConfigPath = FPaths::Combine(FPaths::ProjectConfigDir(), LocalProviderConfigFileName);
		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *ConfigPath))
		{
			return false;
		}

		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
		{
			return false;
		}

		if (const TSharedPtr<FJsonObject>* EnvObject = nullptr; RootObject->TryGetObjectField(TEXT("env"), EnvObject) && EnvObject && EnvObject->IsValid())
		{
			FString Token;
			if ((*EnvObject)->TryGetStringField(TEXT("ANTHROPIC_AUTH_TOKEN"), Token))
			{
				OutConfig.ApiKey = Token.TrimStartAndEnd();
			}

			FString BaseUrl;
			if ((*EnvObject)->TryGetStringField(TEXT("ANTHROPIC_BASE_URL"), BaseUrl))
			{
				OutConfig.BaseUrl = BaseUrl.TrimStartAndEnd();
			}

			OutConfig.Model = ResolveAnthropicModelFromEnvObject(**EnvObject);
			OutConfig.ProviderType = TEXT("anthropic");
			return true;
		}

		FString ProviderType;
		if (RootObject->TryGetStringField(TEXT("provider"), ProviderType))
		{
			OutConfig.ProviderType = NormalizeProviderType(ProviderType);
		}

		FString ApiKey;
		if (RootObject->TryGetStringField(TEXT("apiKey"), ApiKey))
		{
			OutConfig.ApiKey = ApiKey.TrimStartAndEnd();
		}

		FString BaseUrl;
		if (RootObject->TryGetStringField(TEXT("baseUrl"), BaseUrl))
		{
			OutConfig.BaseUrl = BaseUrl.TrimStartAndEnd();
		}

		FString Model;
		if (RootObject->TryGetStringField(TEXT("model"), Model))
		{
			OutConfig.Model = Model.TrimStartAndEnd();
		}

		return true;
	}

	void ApplyBootstrapOverride(const FAINpcProviderBootstrapConfig& OverrideConfig, FAINpcProviderBootstrapConfig& InOutConfig)
	{
		if (!OverrideConfig.ProviderType.IsEmpty())
		{
			InOutConfig.ProviderType = NormalizeProviderType(OverrideConfig.ProviderType);
		}

		if (!OverrideConfig.ApiKey.IsEmpty())
		{
			InOutConfig.ApiKey = OverrideConfig.ApiKey;
		}

		if (!OverrideConfig.BaseUrl.IsEmpty())
		{
			InOutConfig.BaseUrl = OverrideConfig.BaseUrl;
		}

		if (!OverrideConfig.Model.IsEmpty())
		{
			InOutConfig.Model = OverrideConfig.Model;
		}
	}
}

FAINpcProviderBootstrapConfig FAINpcProviderConfigResolver::ResolveBootstrapConfig(const UAINpcComponent& Component)
{
	(void)Component;

	FAINpcProviderBootstrapConfig Config;
	Config.ProviderType = TEXT("openai");
	Config.BaseUrl = DefaultOpenAIBaseUrl;

	FAINpcProviderBootstrapConfig LocalConfig;
	if (TryParseRepoLocalProviderConfig(LocalConfig))
	{
		ApplyBootstrapOverride(LocalConfig, Config);
	}

	const UAINpcSettings* Settings = GetDefault<UAINpcSettings>();
	const FString SettingsApiKey = Settings ? Settings->GlobalApiKey.TrimStartAndEnd() : FString();
	if (!SettingsApiKey.IsEmpty())
	{
		Config.ApiKey = SettingsApiKey;
	}

	const FString SettingsBaseUrl = Settings ? Settings->GlobalBaseUrl.TrimStartAndEnd() : FString();
	if (!SettingsBaseUrl.IsEmpty())
	{
		Config.BaseUrl = SettingsBaseUrl;
	}

	const FString SettingsModel = Settings ? Settings->GlobalModel.TrimStartAndEnd() : FString();
	if (!SettingsModel.IsEmpty())
	{
		Config.Model = SettingsModel;
	}

	if (TryParseRepoLocalProviderConfig(LocalConfig))
	{
		ApplyBootstrapOverride(LocalConfig, Config);
	}

	if (Config.BaseUrl.IsEmpty())
	{
		Config.BaseUrl = ResolveDefaultBaseUrlForProvider(Config.ProviderType);
	}

	if (Config.ApiKey.IsEmpty())
	{
		Config.ApiKey = ResolveApiKeyFromEnvironment();
	}

	return Config;
}

FAINpcRequestDefaults FAINpcProviderConfigResolver::ResolveRequestDefaults(const UAINpcComponent& Component)
{
	FLLMRequest Request;
	ApplyRequestConfig(Component, Request);

	FAINpcRequestDefaults Defaults;
	Defaults.ApiKey = Request.ApiKey;
	Defaults.BaseUrl = Request.BaseUrl;
	Defaults.Model = Request.Model;
	return Defaults;
}

void FAINpcProviderConfigResolver::ApplyRequestConfig(const UAINpcComponent& Component, FLLMRequest& Request)
{
	const FAINpcProviderBootstrapConfig BootstrapConfig = ResolveBootstrapConfig(Component);
	Request.ApiKey = BootstrapConfig.ApiKey;
	Request.BaseUrl = BootstrapConfig.BaseUrl;
	Request.Model = BootstrapConfig.Model;

	const UAINpcSettings* Settings = GetDefault<UAINpcSettings>();
	if (Settings)
	{
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

	if (Request.BaseUrl.IsEmpty())
	{
		Request.BaseUrl = ResolveDefaultBaseUrlForProvider(BootstrapConfig.ProviderType);
	}

}

TSharedPtr<ILLMProvider, ESPMode::ThreadSafe> FAINpcProviderConfigResolver::CreateProvider(const UAINpcComponent& Component)
{
	const FAINpcProviderBootstrapConfig Config = ResolveBootstrapConfig(Component);
	if (NormalizeProviderType(Config.ProviderType) == TEXT("anthropic"))
	{
		return MakeShared<FAnthropicProvider, ESPMode::ThreadSafe>(Config.ApiKey, Config.Model, Config.BaseUrl);
	}

	return MakeShared<FOpenAIProvider, ESPMode::ThreadSafe>(Config.ApiKey, Config.Model, Config.BaseUrl);
}
