#include "Test/AINpcDialogueVisualTestSupport.h"

#include "Components/AINpcComponent.h"
#include "Components/AINpcProviderConfigResolver.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace AINpcDialogueVisualTestSupport
{
	namespace
	{
		bool TryReadPersonaField(const FString& Line, const TCHAR* FieldName, FString& OutValue)
		{
			FString Left;
			FString Right;
			if (!Line.Split(TEXT("="), &Left, &Right, ESearchCase::CaseSensitive))
			{
				return false;
			}

			Left.TrimStartAndEndInline();
			if (Left != FieldName)
			{
				return false;
			}

			Right.TrimStartAndEndInline();
			OutValue = MoveTemp(Right);
			return true;
		}
	}

	bool LoadRequiredConfigText(const TCHAR* FileName, const TCHAR* Purpose, FString& OutText, FString& OutFailureReason)
	{
		const FString ConfigPath = FPaths::Combine(FPaths::ProjectConfigDir(), FileName);
		FString LoadedText;
		if (!FFileHelper::LoadFileToString(LoadedText, *ConfigPath))
		{
			OutFailureReason = FString::Printf(TEXT("Failed to load visual harness %s text from %s."), Purpose, *ConfigPath);
			return false;
		}

		LoadedText.TrimStartAndEndInline();
		if (LoadedText.IsEmpty())
		{
			OutFailureReason = FString::Printf(TEXT("Visual harness %s text was empty: %s."), Purpose, *ConfigPath);
			return false;
		}

		OutText = MoveTemp(LoadedText);
		return true;
	}

	bool LoadPersonaText(const TCHAR* FileName, FVisualHarnessPersonaText& OutPersonaText, FString& OutFailureReason)
	{
		FString PersonaConfigText;
		if (!LoadRequiredConfigText(FileName, TEXT("persona"), PersonaConfigText, OutFailureReason))
		{
			return false;
		}

		OutPersonaText = FVisualHarnessPersonaText();
		TArray<FString> Lines;
		PersonaConfigText.ParseIntoArrayLines(Lines, false);
		for (const FString& RawLine : Lines)
		{
			FString Line = RawLine;
			Line.TrimStartAndEndInline();
			if (Line.IsEmpty())
			{
				continue;
			}

			FString Value;
			if (TryReadPersonaField(Line, TEXT("PersonaName"), Value))
			{
				OutPersonaText.PersonaName = MoveTemp(Value);
			}
			else if (TryReadPersonaField(Line, TEXT("Background"), Value))
			{
				OutPersonaText.Background = MoveTemp(Value);
			}
			else if (TryReadPersonaField(Line, TEXT("SpeakingStyle"), Value))
			{
				OutPersonaText.SpeakingStyle = MoveTemp(Value);
			}
		}

		if (OutPersonaText.PersonaName.IsEmpty() || OutPersonaText.Background.IsEmpty() || OutPersonaText.SpeakingStyle.IsEmpty())
		{
			OutFailureReason = FString::Printf(TEXT("Visual harness persona config is missing PersonaName, Background, or SpeakingStyle: %s."), FileName);
			return false;
		}

		return true;
	}

	bool ValidateProviderConfiguration(const UAINpcComponent* NpcComponent, FString& OutFailureReason)
	{
		if (!NpcComponent)
		{
			OutFailureReason = TEXT("Provider configuration cannot be validated because NPC component is null.");
			return false;
		}

		const FAINpcProviderBootstrapConfig Config = FAINpcProviderConfigResolver::ResolveBootstrapConfig(*NpcComponent);
		const FString Provider = Config.ProviderType.TrimStartAndEnd().ToLower();
		const FString BaseUrl = Config.BaseUrl.TrimStartAndEnd();
		const FString Model = Config.Model.TrimStartAndEnd();
		const FString ApiKey = Config.ApiKey.TrimStartAndEnd();
		if (Provider.IsEmpty() || (Provider != TEXT("openai") && Provider != TEXT("anthropic")))
		{
			OutFailureReason = FString::Printf(TEXT("Invalid real provider config for visual harness: provider='%s'."), *Config.ProviderType);
			return false;
		}
		if (BaseUrl.IsEmpty() || Model.IsEmpty() || ApiKey.IsEmpty())
		{
			OutFailureReason = FString::Printf(
				TEXT("Incomplete real provider config for visual harness. Provider=%s BaseUrl=%s Model=%s ApiKey=%s."),
				*Provider,
				BaseUrl.IsEmpty() ? TEXT("missing") : TEXT("present"),
				Model.IsEmpty() ? TEXT("missing") : TEXT("present"),
				ApiKey.IsEmpty() ? TEXT("missing") : TEXT("present"));
			return false;
		}

		return true;
	}

	FString DescribeDialogueState(const UAINpcComponent* NpcComponent)
	{
		if (!NpcComponent)
		{
			return TEXT("DialogueState=<missing component>");
		}

		return FString::Printf(
			TEXT("DialogueState=%s RequestInFlight=%s SessionActive=%s Queued=%s"),
			*GetDialogueStateText(NpcComponent),
			NpcComponent->IsRequestInFlight() ? TEXT("true") : TEXT("false"),
			NpcComponent->IsDialogueActive() ? TEXT("true") : TEXT("false"),
			NpcComponent->IsDialogueRequestQueued() ? TEXT("true") : TEXT("false"));
	}

	FString GetDialogueStateText(const UAINpcComponent* NpcComponent)
	{
		if (!NpcComponent)
		{
			return TEXT("<missing component>");
		}

		const UEnum* const DialogueStateEnum = StaticEnum<ENpcDialogueState>();
		return DialogueStateEnum ? DialogueStateEnum->GetValueAsString(NpcComponent->GetDialogueState()) : TEXT("Unknown");
	}
}
