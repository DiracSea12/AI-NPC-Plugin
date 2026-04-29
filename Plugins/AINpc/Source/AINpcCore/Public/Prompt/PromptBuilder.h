#pragma once

#include "CoreMinimal.h"

class UNpcPersonaDataAsset;
enum class ENpcSpeakingLength : uint8;

struct AINPCCORE_API FPromptBuilderConfig
{
	int32 MaxPromptTokens = 512;
	TArray<FString> AvailableSmartObjectTargets;
	TArray<FString> RelevantMemories;
};

class AINPCCORE_API FPromptBuilder
{
public:
	static FString BuildSystemPrompt(
		const UNpcPersonaDataAsset* PersonaDataAsset,
		const FPromptBuilderConfig& Config = FPromptBuilderConfig());

	static int32 GetMaxSentenceCount(ENpcSpeakingLength SpeakingLength);
};
