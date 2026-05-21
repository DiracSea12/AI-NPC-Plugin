#pragma once

#include "CoreMinimal.h"

class UAINpcComponent;

namespace AINpcDialogueVisualTestSupport
{
	struct FVisualHarnessPersonaText
	{
		FString PersonaName;
		FString Background;
		FString SpeakingStyle;
	};

	bool LoadRequiredConfigText(const TCHAR* FileName, const TCHAR* Purpose, FString& OutText, FString& OutFailureReason);
	bool LoadPersonaText(const TCHAR* FileName, FVisualHarnessPersonaText& OutPersonaText, FString& OutFailureReason);
	bool ValidateProviderConfiguration(const UAINpcComponent* NpcComponent, FString& OutFailureReason);
	FString DescribeDialogueState(const UAINpcComponent* NpcComponent);
	FString GetDialogueStateText(const UAINpcComponent* NpcComponent);
}
