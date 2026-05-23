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
	const TSet<FString> GTopLevelFields = {
		TEXT("schemaVersion"), TEXT("testId"), TEXT("map"), TEXT("timeoutSec"), TEXT("storyIds"), TEXT("phaseIds"),
		TEXT("fixture"), TEXT("persona"), TEXT("prompt"), TEXT("steps"), TEXT("expect")
	};
	const TSet<FString> GLegacyFields = {
		TEXT("requiresProvider"), TEXT("promptFile"), TEXT("personaFile"), TEXT("delayFillerFile"), TEXT("delayFillerThreshold"),
		TEXT("requireEventTrigger"), TEXT("eventTag"), TEXT("eventTriggerId"), TEXT("requirePartialResponse"),
		TEXT("requireStructuredResponse"), TEXT("requireActionIntent"), TEXT("allowActionRejection"),
		TEXT("requiredObservations"), TEXT("allowedTerminalOutcomes"), TEXT("script")
	};
	const TSet<FString> GSupportedStepTypes = {
		TEXT("dialogue.start"), TEXT("world.event"), TEXT("wait.until"), TEXT("action.executeLatestIntent"), TEXT("observe.hold")
	};

	bool IsKnownVisualObservationName(const FString& Name)
	{
		static const TSet<FString> KnownNames = {
			TEXT("sessionStarted"), TEXT("waitingStateObserved"), TEXT("speakingStateObserved"),
			TEXT("dialogueResponseObserved"), TEXT("partialResponseObserved"), TEXT("structuredResponseObserved"),
			TEXT("actionIntentObserved"), TEXT("eventTriggerBroadcast"), TEXT("eventDelayMaskingStartObserved"),
			TEXT("delayMaskingStartObserved"), TEXT("delayMaskingEndObserved"), TEXT("actionExecutionAccepted"),
			TEXT("actionRejectedVisible"), TEXT("actionTargetReached"), TEXT("actionTargetReachedHoldElapsed"),
			TEXT("responseLength"), TEXT("partialResponseLength"), TEXT("delayFillerLength"),
			TEXT("distanceToActionTarget"), TEXT("lastActionFailure")
		};
		return KnownNames.Contains(Name);
	}

	TArray<FString> FindPromptPlaceholders(const FString& Text)
	{
		TArray<FString> Placeholders;
		int32 SearchFrom = 0;
		while (SearchFrom < Text.Len())
		{
			const int32 OpenIndex = Text.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
			if (OpenIndex == INDEX_NONE) { break; }
			const int32 CloseIndex = Text.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, OpenIndex + 1);
			if (CloseIndex == INDEX_NONE) { break; }
			if (CloseIndex > OpenIndex + 1) { Placeholders.AddUnique(Text.Mid(OpenIndex + 1, CloseIndex - OpenIndex - 1)); }
			SearchFrom = CloseIndex + 1;
		}
		return Placeholders;
	}

	FString ScenarioName(const FString& TestId)
	{
		return TestId.IsEmpty() ? FString(TEXT("<unknown>")) : TestId;
	}

	bool RejectUnknownFields(const FJsonObject& Object, const FString& TestId, const TSet<FString>& AllowedFields, const FString& Context, FString& OutError)
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object.Values)
		{
			if (!AllowedFields.Contains(Field.Key))
			{
				OutError = FString::Printf(TEXT("scenario '%s' %s has unknown field '%s'"), *ScenarioName(TestId), *Context, *Field.Key);
				return false;
			}
		}
		return true;
	}

	bool RequireStringField(const FJsonObject& JsonObject, const FString& TestId, const TCHAR* FieldName, FString& OutValue, FString& OutError)
	{
		if (!JsonObject.TryGetStringField(FieldName, OutValue) || OutValue.IsEmpty())
		{
			OutError = FString::Printf(TEXT("scenario '%s' field '%s' is missing or empty"), *ScenarioName(TestId), FieldName);
			return false;
		}
		return true;
	}

	bool RequireNumberField(const FJsonObject& JsonObject, const FString& TestId, const TCHAR* FieldName, double& OutValue, FString& OutError)
	{
		if (!JsonObject.TryGetNumberField(FieldName, OutValue))
		{
			OutError = FString::Printf(TEXT("scenario '%s' field '%s' is missing or not numeric"), *ScenarioName(TestId), FieldName);
			return false;
		}
		return true;
	}

	bool RequireStringArrayField(const FJsonObject& JsonObject, const FString& TestId, const TCHAR* FieldName, TArray<FString>& OutValues, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!JsonObject.TryGetArrayField(FieldName, Values) || !Values)
		{
			OutError = FString::Printf(TEXT("scenario '%s' field '%s' is missing or not an array"), *ScenarioName(TestId), FieldName);
			return false;
		}
		for (int32 Index = 0; Index < Values->Num(); ++Index)
		{
			FString Value;
			if (!(*Values)[Index].IsValid() || !(*Values)[Index]->TryGetString(Value) || Value.IsEmpty())
			{
				OutError = FString::Printf(TEXT("scenario '%s' field '%s' contains invalid value at index %d"), *ScenarioName(TestId), FieldName, Index);
				return false;
			}
			OutValues.Add(MoveTemp(Value));
		}
		if (OutValues.IsEmpty())
		{
			OutError = FString::Printf(TEXT("scenario '%s' field '%s' must not be empty"), *ScenarioName(TestId), FieldName);
			return false;
		}
		return true;
	}

	bool ParseAssertion(const TSharedPtr<FJsonObject>& Object, const FString& TestId, const FString& Context, FAINpcVisualScenarioAssertion& OutAssertion, FString& OutError)
	{
		if (!Object.IsValid())
		{
			OutError = FString::Printf(TEXT("scenario '%s' %s assertion must be an object"), *ScenarioName(TestId), *Context);
			return false;
		}

		static const TSet<FString> AssertionFields = { TEXT("all"), TEXT("any"), TEXT("anyOf"), TEXT("exists"), TEXT("equals") };
		if (!RejectUnknownFields(*Object, TestId, AssertionFields, Context, OutError)) { return false; }
		int32 OperatorCount = 0;
		for (const FString& AssertionField : AssertionFields)
		{
			if (Object->HasField(AssertionField)) { ++OperatorCount; }
		}
		if (OperatorCount != 1)
		{
			OutError = FString::Printf(TEXT("scenario '%s' %s assertion must use exactly one operator"), *ScenarioName(TestId), *Context);
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
		if (Object->TryGetArrayField(TEXT("all"), Children) || Object->TryGetArrayField(TEXT("any"), Children) || Object->TryGetArrayField(TEXT("anyOf"), Children))
		{
			OutAssertion.Operator = Object->HasField(TEXT("all")) ? TEXT("all") : (Object->HasField(TEXT("any")) ? TEXT("any") : TEXT("anyOf"));
			if (!Children || Children->IsEmpty())
			{
				OutError = FString::Printf(TEXT("scenario '%s' %s assertion '%s' must contain child assertions"), *ScenarioName(TestId), *Context, *OutAssertion.Operator);
				return false;
			}
			for (int32 Index = 0; Index < Children->Num(); ++Index)
			{
				const TSharedPtr<FJsonObject>* ChildObject = nullptr;
				if (!(*Children)[Index].IsValid() || !(*Children)[Index]->TryGetObject(ChildObject) || !ChildObject || !ChildObject->IsValid())
				{
					OutError = FString::Printf(TEXT("scenario '%s' %s assertion child %d must be an object"), *ScenarioName(TestId), *Context, Index);
					return false;
				}
				FAINpcVisualScenarioAssertion Child;
				if (!ParseAssertion(*ChildObject, TestId, FString::Printf(TEXT("%s.%s[%d]"), *Context, *OutAssertion.Operator, Index), Child, OutError)) { return false; }
				OutAssertion.Children.Add(MoveTemp(Child));
			}
			return true;
		}

		FString ExistsObservation;
		if (Object->TryGetStringField(TEXT("exists"), ExistsObservation))
		{
			if (ExistsObservation.IsEmpty())
			{
				OutError = FString::Printf(TEXT("scenario '%s' %s exists assertion has empty observation"), *ScenarioName(TestId), *Context);
				return false;
			}
			if (!IsKnownVisualObservationName(ExistsObservation))
			{
				OutError = FString::Printf(TEXT("scenario '%s' %s exists assertion references unknown observation '%s'"), *ScenarioName(TestId), *Context, *ExistsObservation);
				return false;
			}
			OutAssertion.Operator = TEXT("exists");
			OutAssertion.Observation = MoveTemp(ExistsObservation);
			return true;
		}

		const TSharedPtr<FJsonObject>* EqualsObject = nullptr;
		if (Object->TryGetObjectField(TEXT("equals"), EqualsObject) && EqualsObject && EqualsObject->IsValid())
		{
			static const TSet<FString> EqualsFields = { TEXT("observation"), TEXT("value") };
			if (!RejectUnknownFields(**EqualsObject, TestId, EqualsFields, Context + TEXT(".equals"), OutError)) { return false; }
			if (!RequireStringField(**EqualsObject, TestId, TEXT("observation"), OutAssertion.Observation, OutError)) { return false; }
			if (!IsKnownVisualObservationName(OutAssertion.Observation))
			{
				OutError = FString::Printf(TEXT("scenario '%s' %s equals assertion references unknown observation '%s'"), *ScenarioName(TestId), *Context, *OutAssertion.Observation);
				return false;
			}
			const TSharedPtr<FJsonValue> Value = (*EqualsObject)->TryGetField(TEXT("value"));
			if (!Value.IsValid())
			{
				OutError = FString::Printf(TEXT("scenario '%s' %s equals assertion missing value"), *ScenarioName(TestId), *Context);
				return false;
			}
			OutAssertion.Operator = TEXT("equals");
			if (Value->Type == EJson::Boolean)
			{
				OutAssertion.bHasEqualsBool = true;
				OutAssertion.EqualsBool = Value->AsBool();
			}
			else if (!Value->TryGetString(OutAssertion.EqualsString))
			{
				OutError = FString::Printf(TEXT("scenario '%s' %s equals value must be boolean or string"), *ScenarioName(TestId), *Context);
				return false;
			}
			return true;
		}

		OutError = FString::Printf(TEXT("scenario '%s' %s assertion must use all, any, anyOf, exists, or equals"), *ScenarioName(TestId), *Context);
		return false;
	}

	bool ValidatePromptVariables(const FAINpcVisualScenarioConfig& Config, FString& OutError)
	{
		FString PromptText;
		const FString PromptPath = FPaths::Combine(FPaths::ProjectConfigDir(), Config.Prompt.File);
		if (!FFileHelper::LoadFileToString(PromptText, *PromptPath))
		{
			OutError = FString::Printf(TEXT("scenario '%s' field 'prompt.file' references missing file '%s'"), *Config.TestId, *Config.Prompt.File);
			return false;
		}
		for (const TPair<FString, FString>& Variable : Config.Prompt.Variables)
		{
			if (!PromptText.Contains(FString::Printf(TEXT("{%s}"), *Variable.Key)))
			{
				OutError = FString::Printf(TEXT("scenario '%s' field 'prompt.variables.%s' is unresolved by prompt file '%s'"), *Config.TestId, *Variable.Key, *Config.Prompt.File);
				return false;
			}
		}
		for (const FString& Placeholder : FindPromptPlaceholders(PromptText))
		{
			if (!Config.Prompt.Variables.Contains(Placeholder))
			{
				OutError = FString::Printf(TEXT("scenario '%s' prompt file '%s' contains undeclared placeholder '{%s}'"), *Config.TestId, *Config.Prompt.File, *Placeholder);
				return false;
			}
		}
		return true;
	}

	bool ParseScenarioConfig(const FJsonObject& JsonObject, FAINpcVisualScenarioConfig& OutConfig, FString& OutError)
	{
		JsonObject.TryGetStringField(TEXT("testId"), OutConfig.TestId);
		for (const FString& LegacyField : GLegacyFields)
		{
			if (JsonObject.HasField(LegacyField))
			{
				OutError = FString::Printf(TEXT("scenario '%s' uses rejected legacy field '%s'"), *ScenarioName(OutConfig.TestId), *LegacyField);
				return false;
			}
		}
		if (!RejectUnknownFields(JsonObject, OutConfig.TestId, GTopLevelFields, TEXT("top-level"), OutError)) { return false; }

		double SchemaVersion = 0.0;
		if (!RequireNumberField(JsonObject, OutConfig.TestId, TEXT("schemaVersion"), SchemaVersion, OutError)) { return false; }
		const int32 SchemaVersionInt = static_cast<int32>(SchemaVersion);
		if (SchemaVersion != static_cast<double>(SchemaVersionInt))
		{
			OutError = FString::Printf(TEXT("scenario '%s' field 'schemaVersion' must be integer 2"), *ScenarioName(OutConfig.TestId));
			return false;
		}
		OutConfig.SchemaVersion = SchemaVersionInt;
		if (OutConfig.SchemaVersion != 2) { OutError = FString::Printf(TEXT("scenario '%s' field 'schemaVersion' must be 2"), *ScenarioName(OutConfig.TestId)); return false; }
		if (!RequireStringField(JsonObject, OutConfig.TestId, TEXT("testId"), OutConfig.TestId, OutError)) { return false; }
		if (!RequireStringField(JsonObject, OutConfig.TestId, TEXT("map"), OutConfig.Map, OutError)) { return false; }
		double TimeoutSec = 0.0;
		if (!RequireNumberField(JsonObject, OutConfig.TestId, TEXT("timeoutSec"), TimeoutSec, OutError)) { return false; }
		OutConfig.TimeoutSec = static_cast<int32>(TimeoutSec);
		if (OutConfig.TimeoutSec <= 0) { OutError = FString::Printf(TEXT("scenario '%s' field 'timeoutSec' must be positive"), *OutConfig.TestId); return false; }
		if (!RequireStringArrayField(JsonObject, OutConfig.TestId, TEXT("storyIds"), OutConfig.StoryIds, OutError)) { return false; }
		if (!RequireStringArrayField(JsonObject, OutConfig.TestId, TEXT("phaseIds"), OutConfig.PhaseIds, OutError)) { return false; }

		const TSharedPtr<FJsonObject>* FixtureObject = nullptr;
		if (!JsonObject.TryGetObjectField(TEXT("fixture"), FixtureObject) || !FixtureObject || !FixtureObject->IsValid()) { OutError = FString::Printf(TEXT("scenario '%s' field 'fixture' must be an object"), *OutConfig.TestId); return false; }
		if (!RejectUnknownFields(**FixtureObject, OutConfig.TestId, { TEXT("type") }, TEXT("fixture"), OutError)) { return false; }
		if (!RequireStringField(**FixtureObject, OutConfig.TestId, TEXT("type"), OutConfig.Fixture.Type, OutError)) { return false; }
		if (OutConfig.Fixture.Type != TEXT("npcWithSmartObject")) { OutError = FString::Printf(TEXT("scenario '%s' field 'fixture.type' has unsupported value '%s'"), *OutConfig.TestId, *OutConfig.Fixture.Type); return false; }

		const TSharedPtr<FJsonObject>* PersonaObject = nullptr;
		if (!JsonObject.TryGetObjectField(TEXT("persona"), PersonaObject) || !PersonaObject || !PersonaObject->IsValid()) { OutError = FString::Printf(TEXT("scenario '%s' field 'persona' must be an object"), *OutConfig.TestId); return false; }
		if (!RejectUnknownFields(**PersonaObject, OutConfig.TestId, { TEXT("file"), TEXT("delayFillerFile"), TEXT("delayFillerThreshold") }, TEXT("persona"), OutError)) { return false; }
		if (!RequireStringField(**PersonaObject, OutConfig.TestId, TEXT("file"), OutConfig.Persona.File, OutError)) { return false; }
		if (!RequireStringField(**PersonaObject, OutConfig.TestId, TEXT("delayFillerFile"), OutConfig.Persona.DelayFillerFile, OutError)) { return false; }
		double DelayFillerThreshold = 0.0;
		if (!RequireNumberField(**PersonaObject, OutConfig.TestId, TEXT("delayFillerThreshold"), DelayFillerThreshold, OutError)) { return false; }
		OutConfig.Persona.DelayFillerThreshold = static_cast<float>(DelayFillerThreshold);
		if (OutConfig.Persona.DelayFillerThreshold < 0.0f) { OutError = FString::Printf(TEXT("scenario '%s' field 'persona.delayFillerThreshold' must not be negative"), *OutConfig.TestId); return false; }

		const TSharedPtr<FJsonObject>* PromptObject = nullptr;
		if (!JsonObject.TryGetObjectField(TEXT("prompt"), PromptObject) || !PromptObject || !PromptObject->IsValid()) { OutError = FString::Printf(TEXT("scenario '%s' field 'prompt' must be an object"), *OutConfig.TestId); return false; }
		if (!RejectUnknownFields(**PromptObject, OutConfig.TestId, { TEXT("file"), TEXT("variables") }, TEXT("prompt"), OutError)) { return false; }
		if (!RequireStringField(**PromptObject, OutConfig.TestId, TEXT("file"), OutConfig.Prompt.File, OutError)) { return false; }
		const TSharedPtr<FJsonObject>* VariablesObject = nullptr;
		if (!(*PromptObject)->TryGetObjectField(TEXT("variables"), VariablesObject) || !VariablesObject || !VariablesObject->IsValid()) { OutError = FString::Printf(TEXT("scenario '%s' field 'prompt.variables' must be an object"), *OutConfig.TestId); return false; }
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Variable : (*VariablesObject)->Values)
		{
			FString Value;
			if (!Variable.Value.IsValid() || !Variable.Value->TryGetString(Value) || Value.IsEmpty()) { OutError = FString::Printf(TEXT("scenario '%s' field 'prompt.variables.%s' must be a non-empty string"), *OutConfig.TestId, *Variable.Key); return false; }
			OutConfig.Prompt.Variables.Add(Variable.Key, Value);
		}
		if (!ValidatePromptVariables(OutConfig, OutError)) { return false; }

		const TArray<TSharedPtr<FJsonValue>>* StepValues = nullptr;
		if (!JsonObject.TryGetArrayField(TEXT("steps"), StepValues) || !StepValues || StepValues->IsEmpty()) { OutError = FString::Printf(TEXT("scenario '%s' field 'steps' must be a non-empty array"), *OutConfig.TestId); return false; }
		for (int32 StepIndex = 0; StepIndex < StepValues->Num(); ++StepIndex)
		{
			const TSharedPtr<FJsonObject>* StepObject = nullptr;
			if (!(*StepValues)[StepIndex].IsValid() || !(*StepValues)[StepIndex]->TryGetObject(StepObject) || !StepObject || !StepObject->IsValid()) { OutError = FString::Printf(TEXT("scenario '%s' step[%d] must be an object"), *OutConfig.TestId, StepIndex); return false; }
			if (!RejectUnknownFields(**StepObject, OutConfig.TestId, { TEXT("type"), TEXT("payload"), TEXT("condition") }, FString::Printf(TEXT("step[%d]"), StepIndex), OutError)) { return false; }
			FAINpcVisualScenarioStep Step;
			if (!RequireStringField(**StepObject, OutConfig.TestId, TEXT("type"), Step.Type, OutError)) { return false; }
			if (!GSupportedStepTypes.Contains(Step.Type)) { OutError = FString::Printf(TEXT("scenario '%s' step[%d] has unknown type '%s'"), *OutConfig.TestId, StepIndex, *Step.Type); return false; }
			if (Step.Type != TEXT("wait.until") && (*StepObject)->HasField(TEXT("condition")))
			{
				OutError = FString::Printf(TEXT("scenario '%s' step[%d] field 'condition' is only supported by wait.until"), *OutConfig.TestId, StepIndex);
				return false;
			}
			const TSharedPtr<FJsonObject>* PayloadObject = nullptr;
			if (!(*StepObject)->TryGetObjectField(TEXT("payload"), PayloadObject) || !PayloadObject || !PayloadObject->IsValid()) { OutError = FString::Printf(TEXT("scenario '%s' step[%d] field 'payload' must be an object"), *OutConfig.TestId, StepIndex); return false; }

			if (Step.Type == TEXT("dialogue.start"))
			{
				if (!RejectUnknownFields(**PayloadObject, OutConfig.TestId, { TEXT("promptRef") }, FString::Printf(TEXT("step[%d].payload"), StepIndex), OutError)) { return false; }
				if (!RequireStringField(**PayloadObject, OutConfig.TestId, TEXT("promptRef"), Step.Payload.PromptRef, OutError)) { return false; }
				if (Step.Payload.PromptRef != TEXT("prompt")) { OutError = FString::Printf(TEXT("scenario '%s' step[%d].payload.promptRef must be 'prompt'"), *OutConfig.TestId, StepIndex); return false; }
			}
			else if (Step.Type == TEXT("world.event"))
			{
				if (!RejectUnknownFields(**PayloadObject, OutConfig.TestId, { TEXT("eventTag") }, FString::Printf(TEXT("step[%d].payload"), StepIndex), OutError)) { return false; }
				if (!RequireStringField(**PayloadObject, OutConfig.TestId, TEXT("eventTag"), Step.Payload.EventTag, OutError)) { return false; }
			}
			else if (Step.Type == TEXT("wait.until"))
			{
				if (!RejectUnknownFields(**PayloadObject, OutConfig.TestId, { TEXT("timeoutSec") }, FString::Printf(TEXT("step[%d].payload"), StepIndex), OutError)) { return false; }
				double StepTimeout = 0.0;
				if (!RequireNumberField(**PayloadObject, OutConfig.TestId, TEXT("timeoutSec"), StepTimeout, OutError)) { return false; }
				Step.Payload.TimeoutSec = static_cast<float>(StepTimeout);
				if (Step.Payload.TimeoutSec <= 0.0f) { OutError = FString::Printf(TEXT("scenario '%s' step[%d].payload.timeoutSec must be positive"), *OutConfig.TestId, StepIndex); return false; }
				const TSharedPtr<FJsonObject>* ConditionObject = nullptr;
				if (!(*StepObject)->TryGetObjectField(TEXT("condition"), ConditionObject) || !ConditionObject || !ConditionObject->IsValid()) { OutError = FString::Printf(TEXT("scenario '%s' step[%d] wait.until requires condition"), *OutConfig.TestId, StepIndex); return false; }
				if (!ParseAssertion(*ConditionObject, OutConfig.TestId, FString::Printf(TEXT("step[%d].condition"), StepIndex), Step.Condition, OutError)) { return false; }
			}
			else if (Step.Type == TEXT("action.executeLatestIntent"))
			{
				if (!RejectUnknownFields(**PayloadObject, OutConfig.TestId, { TEXT("allowActionRejection") }, FString::Printf(TEXT("step[%d].payload"), StepIndex), OutError)) { return false; }
				if (!(*PayloadObject)->TryGetBoolField(TEXT("allowActionRejection"), Step.Payload.bAllowActionRejection)) { OutError = FString::Printf(TEXT("scenario '%s' step[%d].payload.allowActionRejection must be boolean"), *OutConfig.TestId, StepIndex); return false; }
			}
			else if (Step.Type == TEXT("observe.hold"))
			{
				if (!RejectUnknownFields(**PayloadObject, OutConfig.TestId, { TEXT("observation"), TEXT("durationSec") }, FString::Printf(TEXT("step[%d].payload"), StepIndex), OutError)) { return false; }
				if (!RequireStringField(**PayloadObject, OutConfig.TestId, TEXT("observation"), Step.Payload.Observation, OutError)) { return false; }
				double Duration = 0.0;
				if (!RequireNumberField(**PayloadObject, OutConfig.TestId, TEXT("durationSec"), Duration, OutError)) { return false; }
				Step.Payload.DurationSec = static_cast<float>(Duration);
				if (Step.Payload.DurationSec <= 0.0f) { OutError = FString::Printf(TEXT("scenario '%s' step[%d].payload.durationSec must be positive"), *OutConfig.TestId, StepIndex); return false; }
				if (!IsKnownVisualObservationName(Step.Payload.Observation)) { OutError = FString::Printf(TEXT("scenario '%s' step[%d].payload.observation references unknown observation '%s'"), *OutConfig.TestId, StepIndex, *Step.Payload.Observation); return false; }
			}
			OutConfig.Steps.Add(MoveTemp(Step));
		}

		const TSharedPtr<FJsonObject>* ExpectObject = nullptr;
		if (!JsonObject.TryGetObjectField(TEXT("expect"), ExpectObject) || !ExpectObject || !ExpectObject->IsValid()) { OutError = FString::Printf(TEXT("scenario '%s' field 'expect' must be an object"), *OutConfig.TestId); return false; }
		if (!ParseAssertion(*ExpectObject, OutConfig.TestId, TEXT("expect"), OutConfig.Expect.Assertion, OutError)) { return false; }
		return true;
	}

	TUniquePtr<IAINpcVisualTest> CreateDataDrivenScenarioTest(FAINpcVisualTestContext& Context)
	{
		if (!Context.Fixture.Npc || !Context.Fixture.SmartObject) { return nullptr; }
		for (const FAINpcVisualTestDescriptor& Descriptor : FAINpcVisualTestRegistry::GetDescriptors())
		{
			if (Descriptor.TestId == Context.TestId && Descriptor.ScenarioConfig.IsSet())
			{
				return MakeUnique<FAINpcDataDrivenVisualScenarioTest>(*Context.Fixture.Npc, *Context.Fixture.SmartObject, Descriptor.ScenarioConfig.GetValue());
			}
		}
		return nullptr;
	}

	TArray<FAINpcVisualTestDescriptor> BuildDescriptors()
	{
		TArray<FAINpcVisualTestDescriptor> Descriptors;
		const FString ConfigPath = FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("AINpcVisualScenarios.json"));
		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *ConfigPath)) { UE_LOG(LogTemp, Error, TEXT("AINpcVisualTestRegistry: failed to load %s"), *ConfigPath); return Descriptors; }
		TArray<TSharedPtr<FJsonValue>> RootArray;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, RootArray)) { UE_LOG(LogTemp, Error, TEXT("AINpcVisualTestRegistry: failed to parse %s"), *ConfigPath); return Descriptors; }
		TSet<FString> TestIds;
		for (int32 EntryIndex = 0; EntryIndex < RootArray.Num(); ++EntryIndex)
		{
			const TSharedPtr<FJsonObject>* JsonObject = nullptr;
			if (!RootArray[EntryIndex].IsValid() || !RootArray[EntryIndex]->TryGetObject(JsonObject) || !JsonObject || !JsonObject->IsValid()) { UE_LOG(LogTemp, Error, TEXT("AINpcVisualTestRegistry: entry %d in %s is not a JSON object"), EntryIndex, *ConfigPath); continue; }
			FAINpcVisualScenarioConfig Config;
			FString ParseError;
			if (!ParseScenarioConfig(**JsonObject, Config, ParseError)) { UE_LOG(LogTemp, Error, TEXT("AINpcVisualTestRegistry: invalid scenario entry %d in %s: %s"), EntryIndex, *ConfigPath, *ParseError); continue; }
			if (TestIds.Contains(Config.TestId)) { UE_LOG(LogTemp, Error, TEXT("AINpcVisualTestRegistry: duplicate scenario testId '%s' in %s"), *Config.TestId, *ConfigPath); continue; }
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
		if (Descriptor.TestId == TestId) { return &Descriptor; }
	}
	return nullptr;
}

FString FAINpcVisualTestRegistry::GetRegisteredTestIds()
{
	TArray<FString> TestIds;
	for (const FAINpcVisualTestDescriptor& Descriptor : GetDescriptors()) { TestIds.Add(Descriptor.TestId); }
	return FString::Join(TestIds, TEXT(", "));
}
