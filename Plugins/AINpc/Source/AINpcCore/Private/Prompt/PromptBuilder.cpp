#include "Prompt/PromptBuilder.h"
#include "Data/NpcPersonaDataAsset.h"

FString FPromptBuilder::BuildSystemPrompt(
	const UNpcPersonaDataAsset* PersonaDataAsset,
	const FPromptBuilderConfig& Config)
{
	FString SystemPrompt;

	if (PersonaDataAsset)
	{
		SystemPrompt += FString::Printf(TEXT("You are %s.\n"), *PersonaDataAsset->PersonaName);
		SystemPrompt += FString::Printf(TEXT("Background: %s\n"), *PersonaDataAsset->Background);
		SystemPrompt += FString::Printf(TEXT("Speaking Style: %s\n"), *PersonaDataAsset->SpeakingStyle);
	}

	if (!Config.AvailableSmartObjectTargets.IsEmpty())
	{
		FString TargetList;
		for (int32 i = 0; i < Config.AvailableSmartObjectTargets.Num(); ++i)
		{
			if (i > 0)
			{
				TargetList += TEXT(", ");
			}
			TargetList += Config.AvailableSmartObjectTargets[i];
		}
		SystemPrompt += FString::Printf(TEXT("\nAvailable SmartObjects near NPC (legal action targets): [%s]\n"), *TargetList);
		SystemPrompt += TEXT("If no listed target applies, set \"actions\" to an empty array.\n");
	}

	SystemPrompt += TEXT("\nYou must respond in valid JSON format with the following structure:\n");
	SystemPrompt += TEXT("{\n");
	SystemPrompt += TEXT("  \"dialogue\": \"your response text\",\n");
	SystemPrompt += TEXT("  \"actions\": [{\"action_type\": \"Action.Type\", \"target\": \"target_name\"}],\n");
	SystemPrompt += TEXT("  \"emotion_delta\": {\"valence\": 0.0, \"arousal\": 0.0, \"dominance\": 0.0},\n");
	SystemPrompt += TEXT("  \"relationship_delta\": {\"affinity\": 0.0, \"trust\": 0.0, \"familiarity\": 0.0}\n");
	SystemPrompt += TEXT("}\n");
	SystemPrompt += TEXT("IMPORTANT: All 4 fields are required. Use empty array [] for no actions. Do not use markdown code blocks. Do not add extra fields.\n");

	return SystemPrompt;
}

int32 FPromptBuilder::GetMaxSentenceCount(ENpcSpeakingLength SpeakingLength)
{
	switch (SpeakingLength)
	{
	case ENpcSpeakingLength::VeryShort: return 1;
	case ENpcSpeakingLength::Short: return 2;
	case ENpcSpeakingLength::Medium: return 4;
	case ENpcSpeakingLength::Long: return 6;
	default: return 3;
	}
}
