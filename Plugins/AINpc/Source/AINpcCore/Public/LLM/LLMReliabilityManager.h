#pragma once

#include "CoreMinimal.h"
#include "LLM/LLMProviderTypes.h"

class UAINpcSettings;
class UNpcPersonaDataAsset;

class AINPCCORE_API FLLMReliabilityManager
{
public:
	static bool IsRetryableFailure(const FLLMResponse& Response);
	static int32 GetMaxRetryAttempts(const UAINpcSettings* Settings);
	static float GetRetryBackoffBaseSeconds(const UAINpcSettings* Settings);
	static float GetRetryDelaySeconds(const UAINpcSettings* Settings, int32 RetryAttemptIndex);
	static FString ResolveFallbackResponseText(const UNpcPersonaDataAsset* PersonaDataAsset, const UAINpcSettings* Settings);
};
