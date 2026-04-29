#pragma once

#include "CoreMinimal.h"

class AINPCMEMORY_API FPlayerMentionDetector
{
public:
	static bool ContainsPlayerMention(const FString& Text, const FString& PlayerName = TEXT("Player"));
	static float GetPlayerMentionBoost() { return 3.0f; }
};
