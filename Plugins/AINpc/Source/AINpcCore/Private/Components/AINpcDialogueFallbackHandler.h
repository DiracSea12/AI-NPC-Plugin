#pragma once

#include "CoreMinimal.h"
#include "LLM/LLMProviderTypes.h"

class UAINpcComponent;

class FAINpcDialogueFallbackHandler
{
public:
	static bool TryHandleFailure(UAINpcComponent& Component, const FLLMResponse& Response);

private:
	static FString ResolveFallbackResponseText(const UAINpcComponent& Component);
	static FString ResolveFailureReason(const FLLMResponse& Response);
};
