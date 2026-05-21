#pragma once

#include "CoreMinimal.h"
#include "Components/AINpcComponent.h"

class FAINpcComponentStateHandler
{
public:
	static bool TryGetLatestActionIntent(const UAINpcComponent& Component, FNpcAction& OutActionIntent);
	static bool HasBeenInDialogueStateLongerThan(const UAINpcComponent& Component, float DurationSeconds);
	static bool SupportsStateTreeAutoController(const UAINpcComponent& Component);
	static void EnsureControllerBinding(UAINpcComponent& Component);
	static void SetDialogueState(UAINpcComponent& Component, ENpcDialogueState NewState);
};
