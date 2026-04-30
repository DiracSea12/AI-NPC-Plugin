#pragma once

#include "CoreMinimal.h"
#include "LLM/LLMProviderTypes.h"

class UAINpcComponent;

class FAINpcDialogueRequestBuilder
{
public:
	static FString BuildSystemPrompt(const UAINpcComponent& Component);
	static FLLMRequest BuildRequest(const UAINpcComponent& Component);
	static void ConfigureStreamingRequest(UAINpcComponent& Component, FLLMRequest& Request);
};
