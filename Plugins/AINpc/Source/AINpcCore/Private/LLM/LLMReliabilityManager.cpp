#include "LLM/LLMReliabilityManager.h"

#include "Data/NpcPersonaDataAsset.h"
#include "Settings/AINpcSettings.h"

bool FLLMReliabilityManager::IsRetryableFailure(const FLLMResponse& Response)
{
	if (Response.bSuccess)
	{
		return false;
	}

	if (Response.ErrorMessage.Contains(TEXT("exhausting retries"), ESearchCase::IgnoreCase) ||
		Response.ErrorMessage.Contains(TEXT("retries exhausted"), ESearchCase::IgnoreCase))
	{
		return false;
	}

	if (Response.HttpStatusCode == 408 || Response.HttpStatusCode == 425 || Response.HttpStatusCode == 429)
	{
		return true;
	}

	if (Response.HttpStatusCode >= 500 && Response.HttpStatusCode <= 599)
	{
		return true;
	}

	return Response.ErrorMessage.Contains(TEXT("request timed out"), ESearchCase::IgnoreCase) ||
		Response.ErrorMessage.Contains(TEXT("connection timed out"), ESearchCase::IgnoreCase);
}

int32 FLLMReliabilityManager::GetMaxRetryAttempts(const UAINpcSettings* Settings)
{
	return FMath::Max(0, Settings ? Settings->MaxRequestRetries : 0);
}

float FLLMReliabilityManager::GetRetryBackoffBaseSeconds(const UAINpcSettings* Settings)
{
	return FMath::Max(0.0f, Settings ? Settings->RetryBackoffBaseSeconds : 0.0f);
}

float FLLMReliabilityManager::GetRetryDelaySeconds(const UAINpcSettings* Settings, const int32 RetryAttemptIndex)
{
	const float BaseSeconds = GetRetryBackoffBaseSeconds(Settings);
	if (BaseSeconds <= 0.0f)
	{
		return 0.0f;
	}

	const int32 SafeAttemptIndex = FMath::Max(0, RetryAttemptIndex);
	const float Delay = BaseSeconds * FMath::Pow(2.0f, static_cast<float>(SafeAttemptIndex));
	const float MaxDelay = Settings ? Settings->MaxRetryDelaySeconds : 16.0f;
	return FMath::Min(Delay, MaxDelay);
}

FString FLLMReliabilityManager::ResolveFallbackResponseText(const UNpcPersonaDataAsset* PersonaDataAsset, const UAINpcSettings* Settings)
{
	if (PersonaDataAsset && !PersonaDataAsset->FailureFallbackResponse.IsEmptyOrWhitespace())
	{
		return PersonaDataAsset->FailureFallbackResponse.ToString();
	}

	if (Settings && !Settings->FallbackResponseTemplate.IsEmpty())
	{
		return Settings->FallbackResponseTemplate;
	}

	return FString();
}
