#include "Memory/PlayerMentionDetector.h"

bool FPlayerMentionDetector::ContainsPlayerMention(const FString& Text, const FString& PlayerName)
{
	return Text.Contains(PlayerName, ESearchCase::IgnoreCase) ||
	       Text.Contains(TEXT("player"), ESearchCase::IgnoreCase) ||
	       Text.Contains(TEXT("you"), ESearchCase::IgnoreCase);
}
