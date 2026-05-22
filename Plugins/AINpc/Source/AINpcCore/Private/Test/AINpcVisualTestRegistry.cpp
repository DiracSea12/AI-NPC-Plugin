#include "Test/AINpcVisualTestRegistry.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Test/AINpcDataDrivenVisualScenarioTest.h"
#include "Test/AINpcTestCharacter.h"
#include "Test/AINpcTestSmartObjectActor.h"

namespace
{
	TUniquePtr<IAINpcVisualTest> CreateDataDrivenScenarioTest(FAINpcVisualTestContext& Context)
	{
		if (!Context.Fixture.Npc || !Context.Fixture.SmartObject)
		{
			return nullptr;
		}

		for (const FAINpcVisualTestDescriptor& Descriptor : FAINpcVisualTestRegistry::GetDescriptors())
		{
			if (Descriptor.TestId == Context.TestId && Descriptor.ScenarioConfig.IsSet())
			{
				return MakeUnique<FAINpcDataDrivenVisualScenarioTest>(
					*Context.Fixture.Npc,
					*Context.Fixture.SmartObject,
					Descriptor.ScenarioConfig.GetValue());
			}
		}

		return nullptr;
	}

	bool RequireStringField(const FJsonObject& JsonObject, const TCHAR* FieldName, FString& OutValue, FString& OutError)
	{
		if (!JsonObject.TryGetStringField(FieldName, OutValue) || OutValue.IsEmpty())
		{
			OutError = FString::Printf(TEXT("missing or empty required string field '%s'"), FieldName);
			return false;
		}
		return true;
	}

	bool RequireBoolField(const FJsonObject& JsonObject, const TCHAR* FieldName, bool& OutValue, FString& OutError)
	{
		if (!JsonObject.TryGetBoolField(FieldName, OutValue))
		{
			OutError = FString::Printf(TEXT("missing required bool field '%s'"), FieldName);
			return false;
		}
		return true;
	}

	bool RequireNumberField(const FJsonObject& JsonObject, const TCHAR* FieldName, double& OutValue, FString& OutError)
	{
		if (!JsonObject.TryGetNumberField(FieldName, OutValue))
		{
			OutError = FString::Printf(TEXT("missing required number field '%s'"), FieldName);
			return false;
		}
		return true;
	}

	bool RequireStringArrayField(const FJsonObject& JsonObject, const TCHAR* FieldName, TArray<FString>& OutValues, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!JsonObject.TryGetArrayField(FieldName, Values) || !Values)
		{
			OutError = FString::Printf(TEXT("missing required string array field '%s'"), FieldName);
			return false;
		}

		for (int32 Index = 0; Index < Values->Num(); ++Index)
		{
			FString Value;
			if (!(*Values)[Index].IsValid() || !(*Values)[Index]->TryGetString(Value) || Value.IsEmpty())
			{
				OutError = FString::Printf(TEXT("field '%s' contains a non-string or empty value at index %d"), FieldName, Index);
				return false;
			}
			OutValues.Add(MoveTemp(Value));
		}

		if (OutValues.IsEmpty())
		{
			OutError = FString::Printf(TEXT("required string array field '%s' is empty"), FieldName);
			return false;
		}

		return true;
	}

	bool TryParseScenarioConfig(const FJsonObject& JsonObject, FAINpcVisualScenarioConfig& OutConfig, FString& OutError)
	{
		if (!RequireStringField(JsonObject, TEXT("testId"), OutConfig.TestId, OutError)) { return false; }
		if (!RequireStringField(JsonObject, TEXT("map"), OutConfig.Map, OutError)) { return false; }

		double TimeoutSec = 0.0;
		if (!RequireNumberField(JsonObject, TEXT("timeoutSec"), TimeoutSec, OutError)) { return false; }
		OutConfig.TimeoutSec = static_cast<int32>(TimeoutSec);
		if (OutConfig.TimeoutSec <= 0)
		{
			OutError = FString::Printf(TEXT("scenario '%s' has non-positive timeoutSec"), *OutConfig.TestId);
			return false;
		}

		if (!RequireBoolField(JsonObject, TEXT("requiresProvider"), OutConfig.bRequiresProvider, OutError)) { return false; }
		if (!RequireStringField(JsonObject, TEXT("promptFile"), OutConfig.PromptFile, OutError)) { return false; }
		if (!RequireStringField(JsonObject, TEXT("personaFile"), OutConfig.PersonaFile, OutError)) { return false; }
		if (!RequireStringField(JsonObject, TEXT("delayFillerFile"), OutConfig.DelayFillerFile, OutError)) { return false; }

		double DelayFillerThreshold = 0.0;
		if (!RequireNumberField(JsonObject, TEXT("delayFillerThreshold"), DelayFillerThreshold, OutError)) { return false; }
		OutConfig.DelayFillerThreshold = static_cast<float>(DelayFillerThreshold);
		if (OutConfig.DelayFillerThreshold < 0.0f)
		{
			OutError = FString::Printf(TEXT("scenario '%s' has negative delayFillerThreshold"), *OutConfig.TestId);
			return false;
		}

		if (!RequireBoolField(JsonObject, TEXT("requireEventTrigger"), OutConfig.bRequireEventTrigger, OutError)) { return false; }
		JsonObject.TryGetStringField(TEXT("eventTag"), OutConfig.EventTag);
		JsonObject.TryGetStringField(TEXT("eventTriggerId"), OutConfig.EventTriggerId);
		if (OutConfig.bRequireEventTrigger && (OutConfig.EventTag.IsEmpty() || OutConfig.EventTriggerId.IsEmpty()))
		{
			OutError = FString::Printf(TEXT("scenario '%s' requires eventTag and eventTriggerId when requireEventTrigger is true"), *OutConfig.TestId);
			return false;
		}

		if (!RequireBoolField(JsonObject, TEXT("requirePartialResponse"), OutConfig.bRequirePartialResponse, OutError)) { return false; }
		if (!RequireBoolField(JsonObject, TEXT("requireStructuredResponse"), OutConfig.bRequireStructuredResponse, OutError)) { return false; }
		if (!RequireBoolField(JsonObject, TEXT("requireActionIntent"), OutConfig.bRequireActionIntent, OutError)) { return false; }
		if (!RequireBoolField(JsonObject, TEXT("allowActionRejection"), OutConfig.bAllowActionRejection, OutError)) { return false; }
		if (!RequireStringArrayField(JsonObject, TEXT("storyIds"), OutConfig.StoryIds, OutError)) { return false; }
		if (!RequireStringArrayField(JsonObject, TEXT("phaseIds"), OutConfig.PhaseIds, OutError)) { return false; }
		if (!RequireStringArrayField(JsonObject, TEXT("requiredObservations"), OutConfig.RequiredObservations, OutError)) { return false; }
		if (!RequireStringArrayField(JsonObject, TEXT("allowedTerminalOutcomes"), OutConfig.AllowedTerminalOutcomes, OutError)) { return false; }

		return true;
	}

	TArray<FAINpcVisualTestDescriptor> BuildDescriptors()
	{
		TArray<FAINpcVisualTestDescriptor> Descriptors;

		const FString ConfigPath = FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("AINpcVisualScenarios.json"));
		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *ConfigPath))
		{
			UE_LOG(LogTemp, Error, TEXT("AINpcVisualTestRegistry: failed to load %s"), *ConfigPath);
			return Descriptors;
		}

		TArray<TSharedPtr<FJsonValue>> RootArray;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, RootArray))
		{
			UE_LOG(LogTemp, Error, TEXT("AINpcVisualTestRegistry: failed to parse %s"), *ConfigPath);
			return Descriptors;
		}

		TSet<FString> TestIds;
		bool bHadInvalidEntry = false;
		for (int32 EntryIndex = 0; EntryIndex < RootArray.Num(); ++EntryIndex)
		{
			const TSharedPtr<FJsonValue>& Entry = RootArray[EntryIndex];
			const TSharedPtr<FJsonObject>* JsonObject = nullptr;
			if (!Entry.IsValid() || !Entry->TryGetObject(JsonObject) || !JsonObject || !JsonObject->IsValid())
			{
				UE_LOG(LogTemp, Error, TEXT("AINpcVisualTestRegistry: entry %d in %s is not a JSON object"), EntryIndex, *ConfigPath);
				bHadInvalidEntry = true;
				continue;
			}

			FAINpcVisualScenarioConfig Config;
			FString ParseError;
			if (!TryParseScenarioConfig(**JsonObject, Config, ParseError))
			{
				UE_LOG(LogTemp, Error, TEXT("AINpcVisualTestRegistry: invalid scenario entry %d in %s: %s"), EntryIndex, *ConfigPath, *ParseError);
				bHadInvalidEntry = true;
				continue;
			}

			if (TestIds.Contains(Config.TestId))
			{
				UE_LOG(LogTemp, Error, TEXT("AINpcVisualTestRegistry: duplicate scenario testId '%s' in %s"), *Config.TestId, *ConfigPath);
				bHadInvalidEntry = true;
				continue;
			}
			TestIds.Add(Config.TestId);

			FAINpcVisualTestDescriptor Descriptor;
			Descriptor.TestId = Config.TestId;
			Descriptor.StoryIds = Config.StoryIds;
			Descriptor.PhaseIds = Config.PhaseIds;
			Descriptor.FixtureKind = EAINpcVisualTestFixtureKind::NpcWithSmartObject;
			Descriptor.CreateTest = &CreateDataDrivenScenarioTest;
			Descriptor.ScenarioConfig = MoveTemp(Config);
			Descriptors.Add(MoveTemp(Descriptor));
		}

		if (bHadInvalidEntry)
		{
			UE_LOG(LogTemp, Error, TEXT("AINpcVisualTestRegistry: rejected one or more invalid entries from %s; registered valid ids: %s"), *ConfigPath, *FString::JoinBy(Descriptors, TEXT(", "), [](const FAINpcVisualTestDescriptor& Descriptor) { return Descriptor.TestId; }));
		}

		return Descriptors;
	}
}

const TArray<FAINpcVisualTestDescriptor>& FAINpcVisualTestRegistry::GetDescriptors()
{
	static const TArray<FAINpcVisualTestDescriptor> Descriptors = BuildDescriptors();
	return Descriptors;
}

const FAINpcVisualTestDescriptor* FAINpcVisualTestRegistry::Find(const FString& TestId)
{
	for (const FAINpcVisualTestDescriptor& Descriptor : GetDescriptors())
	{
		if (Descriptor.TestId == TestId)
		{
			return &Descriptor;
		}
	}

	return nullptr;
}

FString FAINpcVisualTestRegistry::GetRegisteredTestIds()
{
	TArray<FString> TestIds;
	for (const FAINpcVisualTestDescriptor& Descriptor : GetDescriptors())
	{
		TestIds.Add(Descriptor.TestId);
	}

	return FString::Join(TestIds, TEXT(", "));
}
