#include "AINpcProviderConfigResolver.h"

#include "Components/AINpcComponent.h"
#include "Dom/JsonObject.h"
#include "LLM/AnthropicProvider.h"
#include "LLM/ILLMProvider.h"
#include "LLM/LLMProviderChain.h"
#include "LLM/LocalProvider.h"
#include "LLM/OllamaProvider.h"
#include "LLM/OpenAIProvider.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Settings/AINpcSettings.h"

namespace
{
	const TCHAR* LocalProviderConfigFileName = TEXT("AINpcLocalProvider.json");

	FString NormalizeProviderType(const FString& ProviderType)
	{
		const FString Normalized = ProviderType.TrimStartAndEnd().ToLower();
		if (Normalized == TEXT("anthropic") || Normalized == TEXT("custom") ||
			Normalized == TEXT("local") || Normalized == TEXT("ollama") ||
			Normalized == TEXT("openai"))
		{
			return Normalized;
		}

		return FString();
	}

	FString NormalizeProviderProtocol(const FString& Protocol)
	{
		return Protocol.TrimStartAndEnd().ToLower();
	}

	bool IsLocalDeploymentProvider(const FString& ProviderType)
	{
		const FString NormalizedProviderType = NormalizeProviderType(ProviderType);
		return NormalizedProviderType == TEXT("local") || NormalizedProviderType == TEXT("ollama");
	}

	bool IsSupportedProviderType(const FString& ProviderType)
	{
		return !NormalizeProviderType(ProviderType).IsEmpty();
	}

	bool TryReadRequiredProviderString(
		const FJsonObject& JsonObject,
		const TCHAR* FieldName,
		FString& OutValue,
		FString& OutError)
	{
		FString RawValue;
		if (!JsonObject.TryGetStringField(FieldName, RawValue))
		{
			OutError = FString::Printf(TEXT("Provider JSON is missing required field '%s'."), FieldName);
			return false;
		}

		OutValue = RawValue.TrimStartAndEnd();
		if (OutValue.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Provider JSON field '%s' must not be empty."), FieldName);
			return false;
		}

		return true;
	}

	bool TryReadOptionalProviderString(const FJsonObject& JsonObject, const TCHAR* FieldName, FString& OutValue)
	{
		FString RawValue;
		if (!JsonObject.TryGetStringField(FieldName, RawValue))
		{
			return false;
		}

		OutValue = RawValue.TrimStartAndEnd();
		return true;
	}

	bool ValidateProviderSourceFields(
		const FString& Context,
		const FString& ProviderType,
		const FString& Protocol,
		const FString& ApiKey,
		const FString& BaseUrl,
		const FString& Model,
		FString& OutError)
	{
		if (!IsSupportedProviderType(ProviderType))
		{
			OutError = FString::Printf(TEXT("%s provider type is missing or unsupported."), *Context);
			return false;
		}

		if (NormalizeProviderType(ProviderType) == TEXT("custom") && NormalizeProviderProtocol(Protocol) != TEXT("openai-compatible"))
		{
			OutError = FString::Printf(TEXT("%s custom provider requires protocol 'openai-compatible'."), *Context);
			return false;
		}

		if (Model.IsEmpty())
		{
			OutError = FString::Printf(TEXT("%s provider JSON field 'model' must not be empty."), *Context);
			return false;
		}

		if (BaseUrl.IsEmpty())
		{
			OutError = FString::Printf(TEXT("%s provider JSON field 'baseUrl' must not be empty."), *Context);
			return false;
		}

		if (!IsLocalDeploymentProvider(ProviderType) && ApiKey.IsEmpty())
		{
			OutError = FString::Printf(TEXT("%s provider JSON field 'apiKey' must not be empty."), *Context);
			return false;
		}

		return true;
	}

	bool TryParseProviderSourceObject(
		const FJsonObject& JsonObject,
		const FString& Context,
		FAINpcProviderBootstrapConfig& OutConfig)
	{
		FString Error;
		FString ProviderType;
		if (!TryReadRequiredProviderString(JsonObject, TEXT("provider"), ProviderType, Error))
		{
			OutConfig.ProviderSourceError = FString::Printf(TEXT("%s %s"), *Context, *Error);
			return false;
		}

		ProviderType = NormalizeProviderType(ProviderType);
		FString ApiKey;
		TryReadOptionalProviderString(JsonObject, TEXT("apiKey"), ApiKey);

		FString BaseUrl;
		TryReadOptionalProviderString(JsonObject, TEXT("baseUrl"), BaseUrl);

		FString Model;
		TryReadOptionalProviderString(JsonObject, TEXT("model"), Model);

		FString EffortLevel;
		TryReadOptionalProviderString(JsonObject, TEXT("effortLevel"), EffortLevel);
		EffortLevel = EffortLevel.ToLower();

		FString Protocol;
		TryReadOptionalProviderString(JsonObject, TEXT("protocol"), Protocol);
		Protocol = NormalizeProviderProtocol(Protocol);

		if (!ValidateProviderSourceFields(Context, ProviderType, Protocol, ApiKey, BaseUrl, Model, Error))
		{
			OutConfig.ProviderSourceError = Error;
			return false;
		}

		OutConfig.bHasProviderSourceConfig = true;
		OutConfig.ProviderType = ProviderType;
		OutConfig.ApiKey = ApiKey;
		OutConfig.BaseUrl = BaseUrl;
		OutConfig.Model = Model;
		OutConfig.EffortLevel = EffortLevel;
		OutConfig.Protocol = Protocol;
		return true;
	}

	bool TryParseProviderConfigObject(const FJsonObject& RootObject, FAINpcProviderBootstrapConfig& OutConfig)
	{
		OutConfig = FAINpcProviderBootstrapConfig();
		if (RootObject.HasField(TEXT("fallback")))
		{
			OutConfig.ProviderSourceError = TEXT("Provider JSON field 'fallback' is not supported.");
			return false;
		}

		return TryParseProviderSourceObject(RootObject, TEXT("Primary"), OutConfig);
	}

	bool TryParseProviderConfigJson(const FString& JsonText, FAINpcProviderBootstrapConfig& OutConfig)
	{
		OutConfig = FAINpcProviderBootstrapConfig();
		if (JsonText.TrimStartAndEnd().IsEmpty())
		{
			OutConfig.ProviderSourceError = TEXT("Provider JSON config is missing.");
			return false;
		}

		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
		{
			OutConfig.ProviderSourceError = TEXT("Provider JSON config could not be parsed.");
			return false;
		}

		return TryParseProviderConfigObject(*RootObject, OutConfig);
	}

	bool TryParseRepoLocalProviderConfig(FAINpcProviderBootstrapConfig& OutConfig)
	{
		OutConfig = FAINpcProviderBootstrapConfig();

		const FString ConfigPath = FPaths::Combine(FPaths::ProjectConfigDir(), LocalProviderConfigFileName);
		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *ConfigPath))
		{
			OutConfig.ProviderSourceError = TEXT("Provider JSON config file is missing.");
			return false;
		}

		return TryParseProviderConfigJson(JsonText, OutConfig);
	}

	void ApplyRequestConfigFromBootstrap(
		const UAINpcComponent& Component,
		const FAINpcProviderBootstrapConfig& BootstrapConfig,
		FLLMRequest& Request)
	{
		(void)Component;

		if (BootstrapConfig.bHasProviderSourceConfig)
		{
			Request.ApiKey = BootstrapConfig.ApiKey;
			Request.BaseUrl = BootstrapConfig.BaseUrl;
			Request.Model = BootstrapConfig.Model;
			Request.EffortLevel = BootstrapConfig.EffortLevel;
		}

		const UAINpcSettings* Settings = GetDefault<UAINpcSettings>();
		if (Settings)
		{
			Request.TimeoutSeconds = FMath::Max(0.0f, Settings->RequestTimeoutSeconds);
		}
	}

	TSharedPtr<ILLMProvider> CreateProviderFromConfig(const FAINpcProviderBootstrapConfig& Config)
	{
		if (!Config.bHasProviderSourceConfig)
		{
			return nullptr;
		}

		FAINpcProviderBootstrapConfig ResolvedConfig = Config;
		ResolvedConfig.ProviderType = NormalizeProviderType(ResolvedConfig.ProviderType);
		ResolvedConfig.Protocol = NormalizeProviderProtocol(ResolvedConfig.Protocol);

		FString Error;
		if (!ValidateProviderSourceFields(
			TEXT("Primary"),
			ResolvedConfig.ProviderType,
			ResolvedConfig.Protocol,
			ResolvedConfig.ApiKey,
			ResolvedConfig.BaseUrl,
			ResolvedConfig.Model,
			Error))
		{
			return nullptr;
		}

		const FString ProviderType = ResolvedConfig.ProviderType;
		const FString Protocol = ResolvedConfig.Protocol;
		if (ProviderType == TEXT("anthropic"))
		{
			return MakeShared<FAnthropicProvider>(ResolvedConfig.ApiKey, ResolvedConfig.Model, ResolvedConfig.BaseUrl);
		}

		if (ProviderType == TEXT("local"))
		{
			return MakeShared<FLocalProvider>(ResolvedConfig.Model, ResolvedConfig.BaseUrl);
		}

		if (ProviderType == TEXT("ollama"))
		{
			return MakeShared<FOllamaProvider>(ResolvedConfig.Model, ResolvedConfig.BaseUrl);
		}

		if (ProviderType == TEXT("openai") || ProviderType == TEXT("custom"))
		{
			return MakeShared<FOpenAIProvider>(ResolvedConfig.ApiKey, ResolvedConfig.Model, ResolvedConfig.BaseUrl);
		}

		return nullptr;
	}

}

FAINpcProviderBootstrapConfig FAINpcProviderConfigResolver::ResolveBootstrapConfig(const UAINpcComponent& Component)
{
	(void)Component;

	FAINpcProviderBootstrapConfig Config;
	TryParseRepoLocalProviderConfig(Config);
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
	Defaults.EffortLevel = Request.EffortLevel;
	return Defaults;
}

void FAINpcProviderConfigResolver::ApplyRequestConfig(const UAINpcComponent& Component, FLLMRequest& Request)
{
	const FAINpcProviderBootstrapConfig BootstrapConfig = ResolveBootstrapConfig(Component);
	ApplyRequestConfigFromBootstrap(Component, BootstrapConfig, Request);
}

TSharedPtr<ILLMProvider, ESPMode::ThreadSafe> FAINpcProviderConfigResolver::CreateProvider(const UAINpcComponent& Component)
{
	const FAINpcProviderBootstrapConfig Config = ResolveBootstrapConfig(Component);
	TSharedPtr<ILLMProvider, ESPMode::ThreadSafe> PrimaryProvider = CreateProviderFromConfig(Config);
	if (!PrimaryProvider.IsValid())
	{
		return nullptr;
	}

	const UAINpcSettings* Settings = GetDefault<UAINpcSettings>();
	TArray<FString> FallbackResponses;
	if (Settings && !Settings->FallbackResponseTemplate.TrimStartAndEnd().IsEmpty())
	{
		FallbackResponses.Add(Settings->FallbackResponseTemplate);
	}

	const int32 MaxRetries = Settings ? FMath::Max(0, Settings->MaxRequestRetries) : 0;
	const float BaseRetryDelaySeconds = Settings ? FMath::Max(0.0f, Settings->RetryBackoffBaseSeconds) : 0.0f;
	TSharedPtr<FLLMProviderChain, ESPMode::ThreadSafe> ProviderChain =
		MakeShared<FLLMProviderChain, ESPMode::ThreadSafe>(PrimaryProvider, nullptr, MoveTemp(FallbackResponses), MaxRetries, BaseRetryDelaySeconds);
	ProviderChain->SetNpcActor(Component.GetOwner());
	return ProviderChain;
}

#if defined(WITH_DEV_AUTOMATION_TESTS) && WITH_DEV_AUTOMATION_TESTS
bool FAINpcProviderConfigResolver::TryResolveBootstrapConfigFromJsonTextForTest(const FString& JsonText, FAINpcProviderBootstrapConfig& OutConfig)
{
	return TryParseProviderConfigJson(JsonText, OutConfig);
}

void FAINpcProviderConfigResolver::ApplyRequestConfigForConfigForTest(
	const UAINpcComponent& Component,
	const FAINpcProviderBootstrapConfig& BootstrapConfig,
	FLLMRequest& Request)
{
	ApplyRequestConfigFromBootstrap(Component, BootstrapConfig, Request);
}

TSharedPtr<ILLMProvider, ESPMode::ThreadSafe> FAINpcProviderConfigResolver::CreateProviderForConfigForTest(const FAINpcProviderBootstrapConfig& Config)
{
	return CreateProviderFromConfig(Config);
}

#endif
