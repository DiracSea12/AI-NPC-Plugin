#include "Prompt/PromptBuilder.h"
#include "Data/NpcPersonaDataAsset.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
const TCHAR* PromptTemplateFileName = TEXT("AINpcPromptTemplate.txt");
const TCHAR* PromptFragmentsFileName = TEXT("AINpcPromptFragments.txt");
const TCHAR* PersonaNamePlaceholder = TEXT("{{PERSONA_NAME}}");
const TCHAR* PersonaRolePlaceholder = TEXT("{{PERSONA_ROLE}}");
const TCHAR* PersonaBackgroundPlaceholder = TEXT("{{PERSONA_BACKGROUND}}");
const TCHAR* PersonaVoicePlaceholder = TEXT("{{PERSONA_VOICE}}");
const TCHAR* SentenceGuidancePlaceholder = TEXT("{{SENTENCE_GUIDANCE}}");
const TCHAR* SmartObjectTargetListPlaceholder = TEXT("{{SMART_OBJECT_TARGET_LIST_BLOCK}}");
const TCHAR* SmartObjectTargetRequirementPlaceholder = TEXT("{{SMART_OBJECT_TARGET_REQUIREMENT_BLOCK}}");
const TCHAR* SmartObjectTargetOptionalPlaceholder = TEXT("{{SMART_OBJECT_TARGET_OPTIONAL_BLOCK}}");
const TCHAR* SmartObjectTargetListValuePlaceholder = TEXT("{{SMART_OBJECT_TARGET_LIST}}");
const TCHAR* MaxSentenceCountPlaceholder = TEXT("{{MAX_SENTENCE_COUNT}}");

FString LoadConfigTextFile(const TCHAR* FileName)
{
	const FString FilePath = FPaths::Combine(FPaths::ProjectConfigDir(), FileName);
	FString FileText;
	if (!FFileHelper::LoadFileToString(FileText, *FilePath))
	{
		return FString();
	}

	return FileText;
}

bool ExtractPromptFragment(const FString& PromptFragments, const TCHAR* FragmentName, FString& OutFragment)
{
	const FString StartMarker = FString::Printf(TEXT("<%s>"), FragmentName);
	const FString EndMarker = FString::Printf(TEXT("</%s>"), FragmentName);
	const int32 StartMarkerIndex = PromptFragments.Find(*StartMarker, ESearchCase::CaseSensitive);
	if (StartMarkerIndex == INDEX_NONE)
	{
		return false;
	}

	const int32 FragmentStartIndex = StartMarkerIndex + StartMarker.Len();
	const int32 EndMarkerIndex = PromptFragments.Find(*EndMarker, ESearchCase::CaseSensitive, ESearchDir::FromStart, FragmentStartIndex);
	if (EndMarkerIndex == INDEX_NONE)
	{
		return false;
	}

	OutFragment = PromptFragments.Mid(FragmentStartIndex, EndMarkerIndex - FragmentStartIndex).TrimStartAndEnd();
	return true;
}

FString GetPromptFragment(const FString& PromptFragments, const TCHAR* FragmentName)
{
	FString Fragment;
	ExtractPromptFragment(PromptFragments, FragmentName, Fragment);
	return Fragment;
}

FString BuildSentenceGuidance(const UNpcPersonaDataAsset* PersonaDataAsset, const FString& PromptFragments)
{
	const ENpcSpeakingLength SpeakingLength = PersonaDataAsset
		? PersonaDataAsset->SpeakingLength
		: ENpcSpeakingLength::Medium;

	const int32 MaxSentenceCount = FPromptBuilder::GetMaxSentenceCount(SpeakingLength);
	FString SentenceGuidance = GetPromptFragment(PromptFragments, TEXT("SentenceGuidanceFormat"));
	SentenceGuidance = SentenceGuidance.Replace(MaxSentenceCountPlaceholder, *FString::FromInt(MaxSentenceCount), ESearchCase::CaseSensitive);
	return SentenceGuidance;
}

FString LoadPromptTemplate()
{
	return LoadConfigTextFile(PromptTemplateFileName);
}

FString LoadPromptFragments()
{
	return LoadConfigTextFile(PromptFragmentsFileName);
}

FString BuildSmartObjectTargetList(const TArray<FString>& SmartObjectTargets)
{
	FString TargetList;
	for (int32 Index = 0; Index < SmartObjectTargets.Num(); ++Index)
	{
		if (Index > 0)
		{
			TargetList += TEXT(", ");
		}

		TargetList += SmartObjectTargets[Index];
	}

	return TargetList;
}

void ReplacePlaceholder(FString& PromptTemplate, const TCHAR* Placeholder, const FString& Replacement)
{
	PromptTemplate = PromptTemplate.Replace(Placeholder, *Replacement, ESearchCase::CaseSensitive);
}

FString ResolvePromptFieldOrDefault(const FString& FieldValue, const FString& DefaultValue)
{
	const FString TrimmedValue = FieldValue.TrimStartAndEnd();
	return TrimmedValue.IsEmpty() ? DefaultValue : TrimmedValue;
}
}

FString FPromptBuilder::BuildSystemPrompt(
	const UNpcPersonaDataAsset* PersonaDataAsset,
	const FPromptBuilderConfig& Config)
{
	FString PromptTemplate = LoadPromptTemplate();
	if (PromptTemplate.IsEmpty())
	{
		return FString();
	}

	const FString PromptFragments = LoadPromptFragments();
	if (PromptFragments.IsEmpty())
	{
		return FString();
	}

	const FString PersonaName = ResolvePromptFieldOrDefault(
		PersonaDataAsset ? PersonaDataAsset->PersonaName : FString(),
		GetPromptFragment(PromptFragments, TEXT("DefaultPersonaName")));
	const FString PersonaRole = GetPromptFragment(PromptFragments, TEXT("DefaultPersonaRole"));
	const FString PersonaBackground = ResolvePromptFieldOrDefault(
		PersonaDataAsset ? PersonaDataAsset->Background : FString(),
		GetPromptFragment(PromptFragments, TEXT("DefaultPersonaBackground")));
	const FString PersonaVoice = ResolvePromptFieldOrDefault(
		PersonaDataAsset ? PersonaDataAsset->SpeakingStyle : FString(),
		GetPromptFragment(PromptFragments, TEXT("DefaultPersonaVoice")));

	ReplacePlaceholder(PromptTemplate, PersonaNamePlaceholder, PersonaName);
	ReplacePlaceholder(PromptTemplate, PersonaRolePlaceholder, PersonaRole);
	ReplacePlaceholder(PromptTemplate, PersonaBackgroundPlaceholder, PersonaBackground);
	ReplacePlaceholder(PromptTemplate, PersonaVoicePlaceholder, PersonaVoice);
	ReplacePlaceholder(PromptTemplate, SentenceGuidancePlaceholder, BuildSentenceGuidance(PersonaDataAsset, PromptFragments));

	if (Config.AvailableSmartObjectTargets.IsEmpty())
	{
		ReplacePlaceholder(PromptTemplate, SmartObjectTargetListPlaceholder, FString());
		ReplacePlaceholder(PromptTemplate, SmartObjectTargetRequirementPlaceholder, FString());
		ReplacePlaceholder(PromptTemplate, SmartObjectTargetOptionalPlaceholder, FString());
		return PromptTemplate;
	}

	const FString TargetList = BuildSmartObjectTargetList(Config.AvailableSmartObjectTargets);
	FString TargetListBlock = GetPromptFragment(PromptFragments, TEXT("SmartObjectTargetListFormat"));
	TargetListBlock = TargetListBlock.Replace(SmartObjectTargetListValuePlaceholder, *TargetList, ESearchCase::CaseSensitive);
	ReplacePlaceholder(
		PromptTemplate,
		SmartObjectTargetListPlaceholder,
		TargetListBlock);
	ReplacePlaceholder(
		PromptTemplate,
		SmartObjectTargetRequirementPlaceholder,
		GetPromptFragment(PromptFragments, TEXT("SmartObjectTargetRequirementBlock")));
	ReplacePlaceholder(
		PromptTemplate,
		SmartObjectTargetOptionalPlaceholder,
		GetPromptFragment(PromptFragments, TEXT("SmartObjectTargetOptionalBlock")));

	return PromptTemplate;
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
