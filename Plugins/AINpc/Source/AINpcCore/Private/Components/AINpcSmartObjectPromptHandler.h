#pragma once

#include "CoreMinimal.h"

class UAINpcComponent;

class FAINpcSmartObjectPromptHandler
{
public:
	static FString BuildSystemPrompt(const UAINpcComponent& Component);
	static TArray<FString> GetAvailableTargets(const UAINpcComponent& Component);
	static void SetTargetsOverrideForTest(const TArray<FString>& InTargets);
	static void ClearTargetsOverrideForTest();
};
