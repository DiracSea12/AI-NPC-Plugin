#include "Test/AINpcVisualTestRegistry.h"

#include "Dom/JsonObject.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Test/AINpcDataDrivenVisualScenarioTest.h"
#include "AINpcVisualInternalAdapters.h"
#include "Test/AINpcVisualTestExtensionInternal.h"
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
		TEXT("dialogue.start"), TEXT("world.event"), TEXT("wait.until"), TEXT("action.executeLatestIntent"), TEXT("project.action.execute"), TEXT("observe.hold")
	};
	const TCHAR* ProjectFixtureKind = TEXT("existingActor");
	const TCHAR* ExistingActorCapability = TEXT("existingActor.classTag");
	const TCHAR* FixtureActorRef = TEXT("fixture.actor");

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

	bool IsProjectObservationNameShape(const FString& Name)
	{
		TArray<FString> Parts;
		Name.ParseIntoArray(Parts, TEXT("."), false);
		return Parts.Num() >= 3 && Parts[0] == TEXT("project") && !Parts[1].IsEmpty() && !Parts[2].IsEmpty();
	}

	bool IsRecognizedObservationReference(const FString& Name)
	{
		return IsKnownVisualObservationName(Name) || IsProjectObservationNameShape(Name);
	}

	bool IsNativeClassPathShape(const FString& Value)
	{
		if (!Value.StartsWith(TEXT("/Script/")) || Value.Contains(TEXT("'")) || Value.Contains(TEXT("_C")) || Value.Contains(TEXT(":")) || Value.Contains(TEXT("/Game/")))
		{
			return false;
		}
		const FString Remainder = Value.RightChop(8);
		FString ModuleName;
		FString ClassName;
		if (!Remainder.Split(TEXT("."), &ModuleName, &ClassName))
		{
			return false;
		}
		return !ModuleName.IsEmpty() && !ClassName.IsEmpty() && !ClassName.Contains(TEXT("."));
	}

#if WITH_DEV_AUTOMATION_TESTS
	bool ContainsAll(const FString& Diagnostic, const TArray<FString>& Terms)
	{
		for (const FString& Term : Terms)
		{
			if (!Diagnostic.Contains(Term))
			{
				return false;
			}
		}
		return true;
	}
#endif

	bool SupportsNotExistsWindowAbsenceProof(const FString& Name)
	{
		static const TSet<FString> WindowSampledNames = {
			TEXT("waitingStateObserved"),
			TEXT("speakingStateObserved"),
			TEXT("actionTargetReached")
		};
		return WindowSampledNames.Contains(Name);
	}

	TArray<FString> FindRegistryPromptPlaceholders(const FString& Text)
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

	bool ParseAssertionScope(const FJsonObject& JsonObject, const FString& TestId, const FString& Context, FAINpcVisualScenarioAssertion& OutAssertion, FString& OutError)
	{
		FString Scope;
		if (!JsonObject.TryGetStringField(TEXT("scope"), Scope))
		{
			return true;
		}
		if (Scope == TEXT("currentStep"))
		{
			OutAssertion.Scope = EAINpcVisualObservationScope::CurrentStep;
			return true;
		}
		if (Scope == TEXT("scenarioHistory"))
		{
			OutAssertion.Scope = EAINpcVisualObservationScope::ScenarioHistory;
			return true;
		}
		OutError = FString::Printf(TEXT("scenario '%s' %s scope must be currentStep or scenarioHistory"), *ScenarioName(TestId), *Context);
		return false;
	}

	bool ParseAssertion(const TSharedPtr<FJsonObject>& Object, const FString& TestId, const FString& Context, FAINpcVisualScenarioAssertion& OutAssertion, FString& OutError)
	{
		if (!Object.IsValid())
		{
			OutError = FString::Printf(TEXT("scenario '%s' %s assertion must be an object"), *ScenarioName(TestId), *Context);
			return false;
		}

		static const TSet<FString> AssertionFields = { TEXT("all"), TEXT("any"), TEXT("anyOf"), TEXT("exists"), TEXT("equals"), TEXT("notExists") };
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
			if (!IsRecognizedObservationReference(ExistsObservation))
			{
				OutError = FString::Printf(TEXT("scenario '%s' %s exists assertion references unknown observation '%s'"), *ScenarioName(TestId), *Context, *ExistsObservation);
				return false;
			}
			OutAssertion.Operator = TEXT("exists");
			OutAssertion.Observation = MoveTemp(ExistsObservation);
			return true;
		}


		FString NotExistsObservation;
		if (Object->TryGetStringField(TEXT("notExists"), NotExistsObservation))
		{
			if (NotExistsObservation.IsEmpty())
			{
				OutError = FString::Printf(TEXT("scenario '%s' %s notExists assertion has empty observation"), *ScenarioName(TestId), *Context);
				return false;
			}
			if (!IsRecognizedObservationReference(NotExistsObservation))
			{
				OutError = FString::Printf(TEXT("scenario '%s' %s notExists assertion references unknown observation '%s'"), *ScenarioName(TestId), *Context, *NotExistsObservation);
				return false;
			}
			if (!SupportsNotExistsWindowAbsenceProof(NotExistsObservation))
			{
				OutError = FString::Printf(TEXT("scenario '%s' %s notExists assertion cannot prove full-window absence for observation '%s'"), *ScenarioName(TestId), *Context, *NotExistsObservation);
				return false;
			}
			OutAssertion.Operator = TEXT("notExists");
			OutAssertion.Observation = MoveTemp(NotExistsObservation);
			return true;
		}

		const TSharedPtr<FJsonObject>* EqualsObject = nullptr;
		if (Object->TryGetObjectField(TEXT("equals"), EqualsObject) && EqualsObject && EqualsObject->IsValid())
		{
			static const TSet<FString> EqualsFields = { TEXT("observation"), TEXT("value"), TEXT("scope") };
			if (!RejectUnknownFields(**EqualsObject, TestId, EqualsFields, Context + TEXT(".equals"), OutError)) { return false; }
			if (!RequireStringField(**EqualsObject, TestId, TEXT("observation"), OutAssertion.Observation, OutError)) { return false; }
				if (!ParseAssertionScope(**EqualsObject, TestId, Context + TEXT(".equals"), OutAssertion, OutError)) { return false; }
			if (!IsRecognizedObservationReference(OutAssertion.Observation))
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
			else if (Value->Type == EJson::String)
			{
				OutAssertion.EqualsString = Value->AsString();
			}
			else
			{
				OutError = FString::Printf(TEXT("scenario '%s' %s equals value must be boolean or string"), *ScenarioName(TestId), *Context);
				return false;
			}
			return true;
		}

		OutError = FString::Printf(TEXT("scenario '%s' %s assertion must use all, any, anyOf, exists, notExists, or equals"), *ScenarioName(TestId), *Context);
		return false;
	}


	bool IsActionAdapterAttemptFact(const FString& ObservationName)
	{
		return ObservationName == TEXT("actionExecutionAccepted") || ObservationName == TEXT("actionRejectedVisible");
	}

	bool ValidateFinalExpectDoesNotUseActionAdapterFacts(const FAINpcVisualScenarioAssertion& Assertion, const FString& TestId, FString& OutError)
	{
		if (IsActionAdapterAttemptFact(Assertion.Observation))
		{
			OutError = FString::Printf(TEXT("scenario '%s' final expect must not use action adapter facts '%s'; actionExecutionAccepted/actionRejectedVisible are step diagnostics, not final player-visible observations"), *ScenarioName(TestId), *Assertion.Observation);
			return false;
		}
		for (int32 ChildIndex = 0; ChildIndex < Assertion.Children.Num(); ++ChildIndex)
		{
			if (!ValidateFinalExpectDoesNotUseActionAdapterFacts(Assertion.Children[ChildIndex], TestId, OutError))
			{
				return false;
			}
		}
		return true;
	}

	bool ForEachProjectObservation(const FAINpcVisualScenarioAssertion& Assertion, TFunctionRef<bool(const FString&)> Visit)
	{
		if (!Assertion.Observation.IsEmpty() && IsProjectObservationNameShape(Assertion.Observation))
		{
			if (!Visit(Assertion.Observation))
			{
				return false;
			}
		}
		for (const FAINpcVisualScenarioAssertion& Child : Assertion.Children)
		{
			if (!ForEachProjectObservation(Child, Visit))
			{
				return false;
			}
		}
		return true;
	}

	bool ValidateExtensionDeclarations(const FAINpcVisualScenarioConfig& Config, FString& OutError)
	{
		using namespace AINpc::Visual::TestInternal;
		if (Config.Fixture.Kind == ProjectFixtureKind)
		{
			const FVisualAdapterDescriptorValidationResult FixtureDescriptor = FindRegisteredAdapterDescriptor(EAINpcVisualAdapterCategory::FixtureResolver, FName(*Config.Fixture.AdapterId), TEXT("ExtensionDeclaration"), Config.TestId);
			if (!FixtureDescriptor.IsSuccess())
			{
				OutError = FixtureDescriptor.Diagnostic;
				return false;
			}
			if (!FixtureDescriptor.Descriptor.Capabilities.Contains(ExistingActorCapability))
			{
				OutError = FString::Printf(TEXT("stage=ExtensionDeclaration testId=%s category=%s adapter=%s field=fixture.kind capability=%s reason=fixture resolver descriptor lacks existing actor capability"),
					*Config.TestId,
					*LexToString(EAINpcVisualAdapterCategory::FixtureResolver),
					*Config.Fixture.AdapterId,
					ExistingActorCapability);
				return false;
			}
		}

		for (const FAINpcVisualScenarioStep& Step : Config.Steps)
		{
			if (Step.Type != TEXT("project.action.execute"))
			{
				continue;
			}
			const FVisualAdapterDescriptorValidationResult ActionDescriptor = FindRegisteredAdapterDescriptor(EAINpcVisualAdapterCategory::ActionAdapter, FName(*Step.Payload.AdapterId), TEXT("ExtensionDeclaration"), Config.TestId);
			if (!ActionDescriptor.IsSuccess())
			{
				OutError = ActionDescriptor.Diagnostic;
				return false;
			}
		}

		return ForEachProjectObservation(Config.Expect.Assertion, [&OutError, &Config](const FString& ObservationName)
		{
			using namespace AINpc::Visual::TestInternal;
			FAINpcVisualObservationDeclaration Declaration;
			const FVisualAdapterDescriptorValidationResult ProviderDescriptor = FindObservationProviderDeclaration(ObservationName, Declaration, TEXT("ExtensionDeclaration"), Config.TestId);
			if (!ProviderDescriptor.IsSuccess())
			{
				OutError = ProviderDescriptor.Diagnostic;
				return false;
			}
			if (Declaration.Capability.IsEmpty() || !ProviderDescriptor.Descriptor.Capabilities.Contains(Declaration.Capability))
			{
				OutError = FString::Printf(TEXT("stage=ExtensionDeclaration testId=%s category=%s adapter=%s observation=%s capability=%s reason=observation declaration capability is missing from descriptor capabilities"),
					*Config.TestId,
					*LexToString(EAINpcVisualAdapterCategory::ObservationProvider),
					*ProviderDescriptor.Descriptor.AdapterId.ToString(),
					*ObservationName,
					*Declaration.Capability);
				return false;
			}
			if (Declaration.ValueType != EAINpcVisualObservationValueType::Boolean)
			{
				OutError = FString::Printf(TEXT("stage=ExtensionDeclaration testId=%s category=%s adapter=%s observation=%s reason=project observation declaration requires boolean value type"),
					*Config.TestId,
					*LexToString(EAINpcVisualAdapterCategory::ObservationProvider),
					*ProviderDescriptor.Descriptor.AdapterId.ToString(),
					*ObservationName);
				return false;
			}
			if (Declaration.SourceKind != TEXT("observation-provider") || Declaration.SamplingMethod != TEXT("state-read") || !Declaration.bRequiresSourceObjectPath || !Declaration.bRequiresSourceClass)
			{
				OutError = FString::Printf(TEXT("stage=ExtensionDeclaration testId=%s category=%s adapter=%s observation=%s reason=observation declaration lacks required state-read source metadata"),
					*Config.TestId,
					*LexToString(EAINpcVisualAdapterCategory::ObservationProvider),
					*ProviderDescriptor.Descriptor.AdapterId.ToString(),
					*ObservationName);
				return false;
			}
			return true;
		});
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
		for (const FString& Placeholder : FindRegistryPromptPlaceholders(PromptText))
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
			if ((*FixtureObject)->HasField(TEXT("type"))) { OutError = FString::Printf(TEXT("scenario '%s' field 'fixture.type' is rejected; use fixture.adapterId and fixture.kind"), *OutConfig.TestId); return false; }
			if (!RejectUnknownFields(**FixtureObject, OutConfig.TestId, { TEXT("adapterId"), TEXT("kind"), TEXT("actorClass"), TEXT("actorTag") }, TEXT("fixture"), OutError)) { return false; }
			if (!RequireStringField(**FixtureObject, OutConfig.TestId, TEXT("adapterId"), OutConfig.Fixture.AdapterId, OutError)) { return false; }
			if (!RequireStringField(**FixtureObject, OutConfig.TestId, TEXT("kind"), OutConfig.Fixture.Kind, OutError)) { return false; }
			if (OutConfig.Fixture.Kind == ProjectFixtureKind)
			{
				if (!RequireStringField(**FixtureObject, OutConfig.TestId, TEXT("actorClass"), OutConfig.Fixture.ActorClass, OutError)) { return false; }
				if (!RequireStringField(**FixtureObject, OutConfig.TestId, TEXT("actorTag"), OutConfig.Fixture.ActorTag, OutError)) { return false; }
				if (!IsNativeClassPathShape(OutConfig.Fixture.ActorClass)) { OutError = FString::Printf(TEXT("scenario '%s' field 'fixture.actorClass' must be a native class path like /Script/Module.ClassName"), *OutConfig.TestId); return false; }
			}
			else
			{
				if (OutConfig.Fixture.AdapterId != AINpcVisualInternalAdapters::CharacterFixtureAdapterId()) { OutError = FString::Printf(TEXT("scenario '%s' field 'fixture.adapterId' has unsupported value '%s'"), *OutConfig.TestId, *OutConfig.Fixture.AdapterId); return false; }
				if (OutConfig.Fixture.Kind != TEXT("character") && OutConfig.Fixture.Kind != TEXT("characterWithSmartObject")) { OutError = FString::Printf(TEXT("scenario '%s' field 'fixture.kind' has unsupported value '%s'"), *OutConfig.TestId, *OutConfig.Fixture.Kind); return false; }
				if ((*FixtureObject)->HasField(TEXT("actorClass")) || (*FixtureObject)->HasField(TEXT("actorTag"))) { OutError = FString::Printf(TEXT("scenario '%s' fixture actorClass/actorTag only apply to existingActor fixtures"), *OutConfig.TestId); return false; }
			}

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
					if (!RejectUnknownFields(**PayloadObject, OutConfig.TestId, { TEXT("adapterId"), TEXT("eventTag"), TEXT("eventId"), TEXT("targetRef"), TEXT("payload") }, FString::Printf(TEXT("step[%d].payload"), StepIndex), OutError)) { return false; }
					if (!RequireStringField(**PayloadObject, OutConfig.TestId, TEXT("adapterId"), Step.Payload.AdapterId, OutError)) { return false; }
					if (Step.Payload.AdapterId != AINpcVisualInternalAdapters::NpcEventAdapterId()) { OutError = FString::Printf(TEXT("scenario '%s' step[%d].payload.adapterId has unsupported value '%s'"), *OutConfig.TestId, StepIndex, *Step.Payload.AdapterId); return false; }
					const bool bHasEventTag = (*PayloadObject)->HasField(TEXT("eventTag"));
					const bool bHasEventId = (*PayloadObject)->HasField(TEXT("eventId"));
					if (bHasEventTag == bHasEventId) { OutError = FString::Printf(TEXT("scenario '%s' step[%d].payload must declare exactly one of eventTag or eventId"), *OutConfig.TestId, StepIndex); return false; }
					if (bHasEventTag && !RequireStringField(**PayloadObject, OutConfig.TestId, TEXT("eventTag"), Step.Payload.EventTag, OutError)) { return false; }
					if (bHasEventId && !RequireStringField(**PayloadObject, OutConfig.TestId, TEXT("eventId"), Step.Payload.EventId, OutError)) { return false; }
					if ((*PayloadObject)->HasField(TEXT("targetRef")) && !(*PayloadObject)->TryGetStringField(TEXT("targetRef"), Step.Payload.TargetRef)) { OutError = FString::Printf(TEXT("scenario '%s' step[%d].payload.targetRef must be a string"), *OutConfig.TestId, StepIndex); return false; }
					if (!Step.Payload.TargetRef.IsEmpty() && Step.Payload.TargetRef != TEXT("fixture.npc")) { OutError = FString::Printf(TEXT("scenario '%s' step[%d].payload.targetRef has unsupported value '%s'"), *OutConfig.TestId, StepIndex, *Step.Payload.TargetRef); return false; }
					const TSharedPtr<FJsonObject>* AdapterPayloadObject = nullptr;
					if ((*PayloadObject)->HasField(TEXT("payload")) && (!(*PayloadObject)->TryGetObjectField(TEXT("payload"), AdapterPayloadObject) || !AdapterPayloadObject || !AdapterPayloadObject->IsValid() || !(*AdapterPayloadObject)->Values.IsEmpty())) { OutError = FString::Printf(TEXT("scenario '%s' step[%d].payload.payload must be an empty object when present"), *OutConfig.TestId, StepIndex); return false; }
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
					if (!RejectUnknownFields(**PayloadObject, OutConfig.TestId, { TEXT("adapterId"), TEXT("actorRef"), TEXT("allowActionRejection") }, FString::Printf(TEXT("step[%d].payload"), StepIndex), OutError)) { return false; }
					if (!RequireStringField(**PayloadObject, OutConfig.TestId, TEXT("adapterId"), Step.Payload.AdapterId, OutError)) { return false; }
					if (Step.Payload.AdapterId != AINpcVisualInternalAdapters::SmartObjectActionAdapterId()) { OutError = FString::Printf(TEXT("scenario '%s' step[%d].payload.adapterId has unsupported value '%s'"), *OutConfig.TestId, StepIndex, *Step.Payload.AdapterId); return false; }
					if (!RequireStringField(**PayloadObject, OutConfig.TestId, TEXT("actorRef"), Step.Payload.ActorRef, OutError)) { return false; }
					if (Step.Payload.ActorRef != TEXT("fixture.npc")) { OutError = FString::Printf(TEXT("scenario '%s' step[%d].payload.actorRef has unsupported value '%s'"), *OutConfig.TestId, StepIndex, *Step.Payload.ActorRef); return false; }
					if (!(*PayloadObject)->TryGetBoolField(TEXT("allowActionRejection"), Step.Payload.bAllowActionRejection)) { OutError = FString::Printf(TEXT("scenario '%s' step[%d].payload.allowActionRejection must be boolean"), *OutConfig.TestId, StepIndex); return false; }
				}
			else if (Step.Type == TEXT("project.action.execute"))
			{
				if (!RejectUnknownFields(**PayloadObject, OutConfig.TestId, { TEXT("adapterId"), TEXT("actionName"), TEXT("targetRef") }, FString::Printf(TEXT("step[%d].payload"), StepIndex), OutError)) { return false; }
				if (!RequireStringField(**PayloadObject, OutConfig.TestId, TEXT("adapterId"), Step.Payload.AdapterId, OutError)) { return false; }
				if (!RequireStringField(**PayloadObject, OutConfig.TestId, TEXT("actionName"), Step.Payload.ActionName, OutError)) { return false; }
				if (!RequireStringField(**PayloadObject, OutConfig.TestId, TEXT("targetRef"), Step.Payload.TargetRef, OutError)) { return false; }
				if (Step.Payload.TargetRef != FixtureActorRef)
				{
					OutError = FString::Printf(TEXT("scenario '%s' step[%d].payload.targetRef must be fixture.actor"), *OutConfig.TestId, StepIndex);
					return false;
				}
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
		if (!ValidateFinalExpectDoesNotUseActionAdapterFacts(OutConfig.Expect.Assertion, OutConfig.TestId, OutError)) { return false; }
		if (!ValidateExtensionDeclarations(OutConfig, OutError)) { return false; }
		return true;
	}

	TUniquePtr<IAINpcVisualTest> CreateDataDrivenScenarioTest(FAINpcVisualTestContext& Context)
	{
		for (const FAINpcVisualTestDescriptor& Descriptor : FAINpcVisualTestRegistry::GetDescriptors())
		{
			if (Descriptor.TestId == Context.TestId && Descriptor.ScenarioConfig.IsSet())
			{
				return MakeUnique<FAINpcDataDrivenVisualScenarioTest>(Context, Descriptor.ScenarioConfig.GetValue());
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
			Descriptor.FixtureKind = Config.Fixture.Kind == TEXT("character") ? EAINpcVisualTestFixtureKind::NpcOnly : EAINpcVisualTestFixtureKind::NpcWithSmartObject;
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


#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAINpcVisualScenarioParserBoundaryTest,
	"AINpc.Visual.Observation.ScenarioParserBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcVisualScenarioParserBoundaryTest::RunTest(const FString& Parameters)
{
	const FString BaseScenario = TEXT(R"JSON({
		"schemaVersion": 2,
		"testId": "phase28c.parser-boundary",
		"map": "/Game/Maps/AINpcTestMap",
		"timeoutSec": 1,
		"storyIds": ["TEST"],
		"phaseIds": ["phase2.8c"],
		"fixture": { "adapterId": "builtin.characterFixture", "kind": "character" },
		"persona": { "file": "AINpcVisualHarnessPhase27Persona.txt", "delayFillerFile": "AINpcVisualHarnessDelayFiller.txt", "delayFillerThreshold": 0.0 },
		"prompt": { "file": "AINpcVisualHarnessPhase27Prompt.txt", "variables": { "SmartObjectTargetId": "runtime.smartObjectTargetId" } },
		"steps": [
			{ "type": "wait.until", "payload": { "timeoutSec": 1 }, "condition": { "equals": { "observation": "waitingStateObserved", "value": false } } }
		],
		"expect": { "equals": { "observation": "waitingStateObserved", "value": false } }
	})JSON");

	TSharedPtr<FJsonObject> JsonObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BaseScenario);
	TestTrue(TEXT("Boundary scenario JSON parses as an object."), FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid());
	FAINpcVisualScenarioConfig ParsedConfig;
	FString ParseError;
	TestTrue(TEXT("Parser accepts JSON boolean false."), JsonObject.IsValid() && ParseScenarioConfig(*JsonObject, ParsedConfig, ParseError));
	TestEqual(TEXT("Parser stores final expect as equals."), ParsedConfig.Expect.Assertion.Operator, FString(TEXT("equals")));
	TestTrue(TEXT("Parser marks final expect value as boolean."), ParsedConfig.Expect.Assertion.bHasEqualsBool);
	TestFalse(TEXT("Parser preserves JSON boolean false."), ParsedConfig.Expect.Assertion.EqualsBool);

		const FString ScopedScenario = BaseScenario.Replace(
			TEXT("\"observation\": \"waitingStateObserved\", \"value\": false"),
			TEXT("\"observation\": \"waitingStateObserved\", \"value\": false, \"scope\": \"scenarioHistory\""));
		TSharedPtr<FJsonObject> ScopedJsonObject;
		const TSharedRef<TJsonReader<>> ScopedReader = TJsonReaderFactory<>::Create(ScopedScenario);
		TestTrue(TEXT("Scoped boundary scenario JSON parses as an object."), FJsonSerializer::Deserialize(ScopedReader, ScopedJsonObject) && ScopedJsonObject.IsValid());
		FAINpcVisualScenarioConfig ScopedConfig;
		ParseError.Reset();
		TestTrue(TEXT("Parser accepts explicit scenarioHistory equals scope."), ScopedJsonObject.IsValid() && ParseScenarioConfig(*ScopedJsonObject, ScopedConfig, ParseError));
		TestEqual(TEXT("Parser stores explicit scenarioHistory scope."), static_cast<int32>(ScopedConfig.Expect.Assertion.Scope), static_cast<int32>(EAINpcVisualObservationScope::ScenarioHistory));

	const FString InvalidNotExistsScenario = BaseScenario.Replace(
		TEXT("{ \"equals\": { \"observation\": \"waitingStateObserved\", \"value\": false } }"),
		TEXT("{ \"notExists\": \"dialogueResponseObserved\" }"));
	TSharedPtr<FJsonObject> InvalidJsonObject;
	const TSharedRef<TJsonReader<>> InvalidReader = TJsonReaderFactory<>::Create(InvalidNotExistsScenario);
	TestTrue(TEXT("Invalid notExists boundary scenario JSON parses as an object."), FJsonSerializer::Deserialize(InvalidReader, InvalidJsonObject) && InvalidJsonObject.IsValid());
	FAINpcVisualScenarioConfig InvalidConfig;
	ParseError.Reset();
	TestFalse(TEXT("Parser rejects notExists for callback observations without full-window absence coverage."), InvalidJsonObject.IsValid() && ParseScenarioConfig(*InvalidJsonObject, InvalidConfig, ParseError));
	TestTrue(TEXT("Unsupported notExists failure names full-window absence proof."), ParseError.Contains(TEXT("full-window absence")));

	const auto ExpectScenarioFailure = [this](const FString& ScenarioText, const TCHAR* CaseName, const TCHAR* ExpectedErrorFragment)
	{
		TSharedPtr<FJsonObject> CaseJsonObject;
		const TSharedRef<TJsonReader<>> CaseReader = TJsonReaderFactory<>::Create(ScenarioText);
		FString CaseContext(CaseName);
		TestTrue(CaseContext + TEXT(" JSON parses as an object."), FJsonSerializer::Deserialize(CaseReader, CaseJsonObject) && CaseJsonObject.IsValid());
		FAINpcVisualScenarioConfig CaseConfig;
		FString CaseError;
		TestFalse(CaseContext + TEXT(" is rejected."), CaseJsonObject.IsValid() && ParseScenarioConfig(*CaseJsonObject, CaseConfig, CaseError));
		TestTrue(CaseContext + TEXT(" reports expected diagnostic fragment."), CaseError.Contains(ExpectedErrorFragment));
	};

	const auto ExpectParseFailure = [&BaseScenario, &ExpectScenarioFailure](const FString& Replacement, const TCHAR* CaseName, const TCHAR* ExpectedErrorFragment)
	{
		const FString ScenarioText = BaseScenario.Replace(
			TEXT("{ \"equals\": { \"observation\": \"waitingStateObserved\", \"value\": false } }"),
			*Replacement);
		ExpectScenarioFailure(ScenarioText, CaseName, ExpectedErrorFragment);
	};

	ExpectParseFailure(TEXT("{ \"notEquals\": { \"observation\": \"waitingStateObserved\", \"value\": false } }"), TEXT("notEquals operator"), TEXT("unknown field 'notEquals'"));
	ExpectParseFailure(TEXT("{ \"greaterThan\": { \"observation\": \"responseLength\", \"value\": 1 } }"), TEXT("greaterThan operator"), TEXT("unknown field 'greaterThan'"));
	ExpectParseFailure(TEXT("{ \"lessThan\": { \"observation\": \"responseLength\", \"value\": 1 } }"), TEXT("lessThan operator"), TEXT("unknown field 'lessThan'"));
	ExpectParseFailure(TEXT("{ \"equals\": { \"observation\": \"responseLength\", \"value\": 1 } }"), TEXT("numeric equals value"), TEXT("equals value must be boolean or string"));
	ExpectParseFailure(TEXT("{ \"equals\": { \"value\": false } }"), TEXT("equals missing observation"), TEXT("field 'observation' is missing or empty"));
	ExpectParseFailure(TEXT("{ \"equals\": { \"observation\": \"waitingStateObserved\" } }"), TEXT("equals missing value"), TEXT("equals assertion missing value"));
	ExpectParseFailure(TEXT("{ \"equals\": { \"observation\": \"unknownObservation\", \"value\": false } }"), TEXT("equals unknown observation"), TEXT("references unknown observation"));
	ExpectParseFailure(TEXT("{ \"all\": [] }"), TEXT("empty all group"), TEXT("must contain child assertions"));
	ExpectParseFailure(TEXT("{ \"any\": [] }"), TEXT("empty any group"), TEXT("must contain child assertions"));
	ExpectParseFailure(TEXT("{ \"anyOf\": [] }"), TEXT("empty anyOf group"), TEXT("must contain child assertions"));
	ExpectParseFailure(TEXT("{ \"all\": [{ \"exists\": \"unknownObservation\" }] }"), TEXT("nested unknown observation"), TEXT("references unknown observation"));

	ExpectScenarioFailure(BaseScenario.Replace(TEXT("\"adapterId\": \"builtin.characterFixture\""), TEXT("\"adapterId\": \"bad.fixtureAdapter\"")),
		TEXT("bad fixture adapter id"), TEXT("fixture.adapterId"));
	ExpectScenarioFailure(BaseScenario.Replace(TEXT("\"adapterId\": \"builtin.characterFixture\", \"kind\": \"character\""), TEXT("\"adapterId\": \"builtin.characterFixture\", \"kind\": \"character\", \"type\": \"npcWithSmartObject\"")),
		TEXT("legacy fixture type"), TEXT("fixture.type"));

	const FString EventScenario = BaseScenario.Replace(
		TEXT("{ \"type\": \"wait.until\", \"payload\": { \"timeoutSec\": 1 }, \"condition\": { \"equals\": { \"observation\": \"waitingStateObserved\", \"value\": false } } }"),
		TEXT("{ \"type\": \"world.event\", \"payload\": { \"adapterId\": \"builtin.npcEvent\", \"eventTag\": \"AINpc.Test.Event\", \"payload\": {} } }"));
	ExpectScenarioFailure(EventScenario.Replace(TEXT("\"adapterId\": \"builtin.npcEvent\""), TEXT("\"adapterId\": \"bad.eventAdapter\"")),
		TEXT("bad event adapter id"), TEXT("adapterId"));
	ExpectScenarioFailure(EventScenario.Replace(TEXT("\"payload\": {}"), TEXT("\"payload\": { \"legacy\": true }")),
		TEXT("malformed event adapter payload"), TEXT("payload.payload"));
	ExpectScenarioFailure(EventScenario.Replace(TEXT("\"eventTag\": \"AINpc.Test.Event\""), TEXT("\"eventTriggerId\": \"legacy-event\"")),
		TEXT("legacy event payload field"), TEXT("unknown field 'eventTriggerId'"));

	const FString ActionScenario = BaseScenario.Replace(
		TEXT("{ \"type\": \"wait.until\", \"payload\": { \"timeoutSec\": 1 }, \"condition\": { \"equals\": { \"observation\": \"waitingStateObserved\", \"value\": false } } }"),
		TEXT("{ \"type\": \"action.executeLatestIntent\", \"payload\": { \"adapterId\": \"builtin.smartObjectAction\", \"actorRef\": \"fixture.npc\", \"allowActionRejection\": true } }"));
	ExpectScenarioFailure(ActionScenario.Replace(TEXT("\"adapterId\": \"builtin.smartObjectAction\""), TEXT("\"adapterId\": \"bad.actionAdapter\"")),
		TEXT("bad action adapter id"), TEXT("adapterId"));
	ExpectScenarioFailure(ActionScenario.Replace(TEXT("\"allowActionRejection\": true"), TEXT("\"legacyActionField\": true")),
		TEXT("malformed action adapter payload"), TEXT("unknown field 'legacyActionField'"));
	ExpectScenarioFailure(BaseScenario.Replace(TEXT("\"testId\": \"phase28c.parser-boundary\""), TEXT("\"testId\": \"phase28c.parser-boundary\", \"eventTriggerId\": \"legacy\"")),
		TEXT("legacy top-level event field"), TEXT("legacy field 'eventTriggerId'"));
	ExpectScenarioFailure(BaseScenario.Replace(TEXT("\"testId\": \"phase28c.parser-boundary\""), TEXT("\"testId\": \"phase28c.parser-boundary\", \"allowActionRejection\": true")),
		TEXT("legacy top-level action field"), TEXT("legacy field 'allowActionRejection'"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAINpcVisualPhase29BParserDeclarationMatrixTest,
	"AINpc.Visual.Phase29B.ParserDeclarationMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcVisualPhase29BParserDeclarationMatrixTest::RunTest(const FString& Parameters)
{
	const FName Owner(TEXT("Phase29BParserOwner"));
	const FName FixtureId(TEXT("phase29b.parser.fixture"));
	const FName FixtureNoCapId(TEXT("phase29b.parser.fixtureNoCap"));
	const FName ActionId(TEXT("phase29b.parser.action"));
	const FName ObservationId(TEXT("phase29b.parser.observation"));
	const FName BadObservationId(TEXT("phase29b.parser.badObservation"));
	const FName CapabilityNameObservationId(TEXT("phase29b.parser.capabilityNameObservation"));
	const FName WrongCategoryObservationId(TEXT("phase29b.parser.wrongCategoryObservation"));
	const FName EmptyObservationNameId(TEXT("phase29b.parser.emptyObservationName"));
	const FName EmptySourceKindId(TEXT("phase29b.parser.emptySourceKind"));
	const FName EmptySamplingMethodId(TEXT("phase29b.parser.emptySamplingMethod"));
	const FName EmptyObservationCapabilityId(TEXT("phase29b.parser.emptyObservationCapability"));
	const FName BadValueTypeId(TEXT("phase29b.parser.badValueType"));
	const FName BadSourceKindId(TEXT("phase29b.parser.badSourceKind"));
	const FName BadSamplingMethodId(TEXT("phase29b.parser.badSamplingMethod"));
	const FName MissingSourceObjectId(TEXT("phase29b.parser.missingSourceObject"));
	const FName MissingSourceClassId(TEXT("phase29b.parser.missingSourceClass"));
	auto Cleanup = [&]()
	{
		FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::FixtureResolver, FixtureId, Owner);
		FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::FixtureResolver, FixtureNoCapId, Owner);
		FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::ActionAdapter, ActionId, Owner);
		FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::ActionAdapter, WrongCategoryObservationId, Owner);
		FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::ObservationProvider, ObservationId, Owner);
		FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::ObservationProvider, BadObservationId, Owner);
		FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::ObservationProvider, CapabilityNameObservationId, Owner);
		FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::ObservationProvider, EmptyObservationNameId, Owner);
		FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::ObservationProvider, EmptySourceKindId, Owner);
		FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::ObservationProvider, EmptySamplingMethodId, Owner);
		FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::ObservationProvider, EmptyObservationCapabilityId, Owner);
		FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::ObservationProvider, BadValueTypeId, Owner);
		FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::ObservationProvider, BadSourceKindId, Owner);
		FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::ObservationProvider, BadSamplingMethodId, Owner);
		FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::ObservationProvider, MissingSourceObjectId, Owner);
		FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::ObservationProvider, MissingSourceClassId, Owner);
	};
	Cleanup();

	FAINpcVisualAdapterDescriptor FixtureDescriptor;
	FixtureDescriptor.Category = EAINpcVisualAdapterCategory::FixtureResolver;
	FixtureDescriptor.AdapterId = FixtureId;
	FixtureDescriptor.OwnerModuleName = Owner;
	FixtureDescriptor.Capabilities = { TEXT("existingActor.classTag") };
	FixtureDescriptor.CreateFixtureResolver = [](const FAINpcVisualAdapterCreateContext&) { return FAINpcVisualAdapterFactoryResult::Failure(TEXT("parser-only fixture")); };
	FAINpcVisualAdapterDescriptor FixtureNoCapDescriptor = FixtureDescriptor;
	FixtureNoCapDescriptor.AdapterId = FixtureNoCapId;
	FixtureNoCapDescriptor.Capabilities = { TEXT("unrelated.fixture") };
	FAINpcVisualAdapterDescriptor ActionDescriptor;
	ActionDescriptor.Category = EAINpcVisualAdapterCategory::ActionAdapter;
	ActionDescriptor.AdapterId = ActionId;
	ActionDescriptor.OwnerModuleName = Owner;
	ActionDescriptor.Capabilities = { TEXT("projectAction.doorInteract") };
	ActionDescriptor.CreateActionAdapter = [](const FAINpcVisualAdapterCreateContext&) { return FAINpcVisualAdapterFactoryResult::Failure(TEXT("parser-only action")); };
	FAINpcVisualObservationDeclaration Declaration;
	Declaration.ObservationName = TEXT("project.door.isOpen");
	Declaration.ValueType = EAINpcVisualObservationValueType::Boolean;
	Declaration.SourceKind = TEXT("observation-provider");
	Declaration.SamplingMethod = TEXT("state-read");
	Declaration.Capability = TEXT("observation.project.door.isOpen");
	Declaration.bRequiresSourceObjectPath = true;
	Declaration.bRequiresSourceClass = true;
	FAINpcVisualAdapterDescriptor ObservationDescriptor;
	ObservationDescriptor.Category = EAINpcVisualAdapterCategory::ObservationProvider;
	ObservationDescriptor.AdapterId = ObservationId;
	ObservationDescriptor.OwnerModuleName = Owner;
	ObservationDescriptor.Capabilities = { TEXT("observation.project.door.isOpen") };
	ObservationDescriptor.ObservationDeclarations = { Declaration };
	ObservationDescriptor.CreateObservationProvider = [](const FAINpcVisualAdapterCreateContext&) { return FAINpcVisualAdapterFactoryResult::Failure(TEXT("parser-only observation")); };
	TestTrue(TEXT("Phase 2.9B parser fixture descriptor registers."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(FixtureDescriptor).IsSuccess());
	TestTrue(TEXT("Phase 2.9B parser fixture missing-capability descriptor registers."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(FixtureNoCapDescriptor).IsSuccess());
	TestTrue(TEXT("Phase 2.9B parser action descriptor registers."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(ActionDescriptor).IsSuccess());
	TestTrue(TEXT("Phase 2.9B parser observation descriptor registers."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(ObservationDescriptor).IsSuccess());

	const FString BaseScenario = TEXT(R"JSON({
		"schemaVersion": 2,
		"testId": "phase29b.parser",
		"map": "/Game/Maps/AINpcTestMap",
		"timeoutSec": 1,
		"storyIds": ["TEST"],
		"phaseIds": ["phase2.9b"],
		"fixture": { "adapterId": "phase29b.parser.fixture", "kind": "existingActor", "actorClass": "/Script/Engine.Actor", "actorTag": "Phase29BDoor" },
		"persona": { "file": "AINpcVisualHarnessPhase27Persona.txt", "delayFillerFile": "AINpcVisualHarnessDelayFiller.txt", "delayFillerThreshold": 0.0 },
		"prompt": { "file": "AINpcVisualHarnessPhase27Prompt.txt", "variables": { "SmartObjectTargetId": "runtime.smartObjectTargetId" } },
		"steps": [{ "type": "project.action.execute", "payload": { "adapterId": "phase29b.parser.action", "actionName": "Interact", "targetRef": "fixture.actor" } }],
		"expect": { "equals": { "observation": "project.door.isOpen", "value": true } }
	})JSON");

	auto ParseScenarioText = [](const FString& ScenarioText, FAINpcVisualScenarioConfig& OutConfig, FString& OutError)
	{
		TSharedPtr<FJsonObject> JsonObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ScenarioText);
		if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
		{
			OutError = TEXT("scenario text did not parse as a JSON object");
			return false;
		}
		return ParseScenarioConfig(*JsonObject, OutConfig, OutError);
	};
	auto ExpectFailure = [this, &BaseScenario, &ParseScenarioText](const FString& Search, const FString& Replacement, const TCHAR* CaseName, const TArray<FString>& Fragments)
	{
		FAINpcVisualScenarioConfig Config;
		FString Error;
		TestFalse(FString(CaseName) + TEXT(" is rejected."), ParseScenarioText(BaseScenario.Replace(*Search, *Replacement), Config, Error));
		TestTrue(FString(CaseName) + TEXT(" reports expected diagnostic."), ContainsAll(Error, Fragments));
	};

	FAINpcVisualScenarioConfig ValidConfig;
	FString ParseError;
	TestTrue(TEXT("Phase 2.9B project parser accepts the valid shape."), ParseScenarioText(BaseScenario, ValidConfig, ParseError));
	for (const FString& InvalidClass : { TEXT("Blueprint'/Game/Door.Door_C'"), TEXT("Actor"), TEXT("/Game/Door.Door"), TEXT("/Script/Engine.Actor_C") })
	{
		ExpectFailure(TEXT("\"actorClass\": \"/Script/Engine.Actor\""), FString::Printf(TEXT("\"actorClass\": \"%s\""), *InvalidClass), TEXT("P29B-FIXTURE-002 invalid actorClass"), { TEXT("fixture.actorClass") });
	}
	ExpectFailure(TEXT(", \"actorTag\": \"Phase29BDoor\""), TEXT(""), TEXT("P29B-FIXTURE-003 missing actorTag"), { TEXT("actorTag"), TEXT("missing or empty") });
	ExpectFailure(TEXT("\"actorTag\": \"Phase29BDoor\""), TEXT("\"actorTag\": \"\""), TEXT("P29B-FIXTURE-003 empty actorTag"), { TEXT("actorTag"), TEXT("missing or empty") });
	ExpectFailure(TEXT("\"adapterId\": \"phase29b.parser.fixture\", "), TEXT(""), TEXT("P29B-FIXTURE-007 missing fixture adapterId"), { TEXT("adapterId"), TEXT("missing or empty") });
	ExpectFailure(TEXT("\"adapterId\": \"phase29b.parser.fixture\""), TEXT("\"adapterId\": \"\""), TEXT("P29B-FIXTURE-007 empty fixture adapterId"), { TEXT("adapterId"), TEXT("missing or empty") });
	ExpectFailure(TEXT("\"kind\": \"existingActor\", "), TEXT(""), TEXT("P29B-FIXTURE-007 missing fixture kind"), { TEXT("kind"), TEXT("missing or empty") });
	ExpectFailure(TEXT("\"kind\": \"existingActor\""), TEXT("\"kind\": \"actorTagOnly\""), TEXT("P29B-FIXTURE-007 unsupported fixture kind"), { TEXT("fixture.adapterId"), TEXT("unsupported value") });
	ExpectFailure(TEXT(", \"actorClass\": \"/Script/Engine.Actor\""), TEXT(""), TEXT("P29B-FIXTURE-007 missing actorClass"), { TEXT("actorClass"), TEXT("missing or empty") });
	ExpectFailure(TEXT("\"actorTag\": \"Phase29BDoor\""), TEXT("\"actorTag\": \"Phase29BDoor\", \"objectRef\": \"DoorA\""), TEXT("P29B-FIXTURE-008 forbidden objectRef"), { TEXT("unknown field 'objectRef'") });
	ExpectFailure(TEXT("\"actorTag\": \"Phase29BDoor\""), TEXT("\"actorTag\": \"Phase29BDoor\", \"componentTag\": \"Door\""), TEXT("P29B-FIXTURE-008 forbidden componentTag"), { TEXT("unknown field 'componentTag'") });
	ExpectFailure(TEXT("\"actorTag\": \"Phase29BDoor\""), TEXT("\"actorTag\": \"Phase29BDoor\", \"softObjectPath\": \"/Game/Door.Door\""), TEXT("P29B-FIXTURE-008 forbidden softObjectPath"), { TEXT("unknown field 'softObjectPath'") });
	ExpectFailure(TEXT("\"actorTag\": \"Phase29BDoor\""), TEXT("\"actorTag\": \"Phase29BDoor\", \"blueprintGeneratedClass\": \"/Game/Door.Door_C\""), TEXT("P29B-FIXTURE-008 forbidden Blueprint class field"), { TEXT("unknown field 'blueprintGeneratedClass'") });
	ExpectFailure(TEXT("\"actorTag\": \"Phase29BDoor\""), TEXT("\"actorTag\": \"Phase29BDoor\", \"gameplayTag\": \"Door.Main\""), TEXT("P29B-FIXTURE-008 forbidden GameplayTag field"), { TEXT("unknown field 'gameplayTag'") });
	ExpectFailure(TEXT("\"actorTag\": \"Phase29BDoor\""), TEXT("\"actorTag\": \"Phase29BDoor\", \"actorTagOnly\": true"), TEXT("P29B-FIXTURE-008 forbidden actor-tag-only resolver"), { TEXT("unknown field 'actorTagOnly'") });
	ExpectFailure(TEXT("\"actorTag\": \"Phase29BDoor\""), TEXT("\"actorTag\": \"Phase29BDoor\", \"crossMapReference\": \"/Game/Maps/Other.Other:Door\""), TEXT("P29B-FIXTURE-008 forbidden cross-map reference"), { TEXT("unknown field 'crossMapReference'") });
	ExpectFailure(TEXT("\"actorTag\": \"Phase29BDoor\""), TEXT("\"actorTag\": \"Phase29BDoor\", \"resolverStrategies\": [\"classTag\", \"softPath\"]"), TEXT("P29B-FIXTURE-008 forbidden resolver strategy list"), { TEXT("unknown field 'resolverStrategies'") });
	ExpectFailure(TEXT("\"actorTag\": \"Phase29BDoor\""), TEXT("\"actorTag\": \"Phase29BDoor\", \"fallbackResolvers\": [\"actorTag\"]"), TEXT("P29B-FIXTURE-008 forbidden multi-strategy fallback"), { TEXT("unknown field 'fallbackResolvers'") });
	ExpectFailure(TEXT("\"targetRef\": \"fixture.actor\""), TEXT("\"targetRef\": \"fixture.door\""), TEXT("P29B-ACTION-003 invalid targetRef"), { TEXT("targetRef"), TEXT("fixture.actor") });
	ExpectFailure(TEXT("\"targetRef\": \"fixture.actor\""), TEXT("\"targetRef\": \"fixture.actor\", \"allowActionRejection\": true"), TEXT("P29B-ACTION-002 forbidden action field"), { TEXT("unknown field 'allowActionRejection'") });
	ExpectFailure(TEXT("\"targetRef\": \"fixture.actor\""), TEXT("\"targetRef\": \"fixture.actor\", \"actorRef\": \"fixture.npc\""), TEXT("P29B-ACTION-002 forbidden actorRef"), { TEXT("unknown field 'actorRef'") });
	ExpectFailure(TEXT("\"targetRef\": \"fixture.actor\""), TEXT("\"targetRef\": \"fixture.actor\", \"latestIntent\": \"Interact\""), TEXT("P29B-ACTION-002 forbidden latest-intent field"), { TEXT("unknown field 'latestIntent'") });
	ExpectFailure(TEXT("\"targetRef\": \"fixture.actor\""), TEXT("\"targetRef\": \"fixture.actor\", \"smartObjectTargetId\": \"Door\""), TEXT("P29B-ACTION-002 forbidden SmartObject field"), { TEXT("unknown field 'smartObjectTargetId'") });
	ExpectFailure(TEXT("\"targetRef\": \"fixture.actor\""), TEXT("\"targetRef\": \"fixture.actor\", \"unknownActionField\": true"), TEXT("P29B-ACTION-002 forbidden unknown action field"), { TEXT("unknown field 'unknownActionField'") });
	ExpectFailure(TEXT("\"adapterId\": \"phase29b.parser.action\", "), TEXT(""), TEXT("P29B-ACTION-005 missing action adapterId"), { TEXT("adapterId"), TEXT("missing or empty") });
	ExpectFailure(TEXT("\"adapterId\": \"phase29b.parser.action\""), TEXT("\"adapterId\": \"\""), TEXT("P29B-ACTION-005 empty action adapterId"), { TEXT("adapterId"), TEXT("missing or empty") });
	ExpectFailure(TEXT("\"actionName\": \"Interact\", "), TEXT(""), TEXT("P29B-ACTION-005 missing actionName"), { TEXT("actionName"), TEXT("missing or empty") });
	ExpectFailure(TEXT("\"actionName\": \"Interact\""), TEXT("\"actionName\": \"\""), TEXT("P29B-ACTION-005 empty actionName"), { TEXT("actionName"), TEXT("missing or empty") });
	ExpectFailure(TEXT(", \"targetRef\": \"fixture.actor\""), TEXT(""), TEXT("P29B-ACTION-005 missing targetRef"), { TEXT("targetRef"), TEXT("missing or empty") });
	for (const FString& MalformedObservation : { TEXT("project"), TEXT("project."), TEXT("project.door"), TEXT("project..isOpen"), TEXT(".project.door.isOpen"), TEXT("project.door.") })
	{
		ExpectFailure(TEXT("\"observation\": \"project.door.isOpen\""), FString::Printf(TEXT("\"observation\": \"%s\""), *MalformedObservation), TEXT("P29B-OBS-003 malformed project observation"), { TEXT("unknown observation") });
	}
	ExpectFailure(TEXT("\"observation\": \"project.door.isOpen\""), TEXT("\"observation\": \"\""), TEXT("P29B-OBS-003 empty project observation"), { TEXT("observation"), TEXT("missing or empty") });
	ExpectFailure(TEXT("\"adapterId\": \"phase29b.parser.fixture\""), TEXT("\"adapterId\": \"phase29b.missing.fixture\""), TEXT("P29B-FIXTURE-011 unregistered fixture adapter"), { TEXT("stage=ExtensionDeclaration"), TEXT("category=FixtureResolver"), TEXT("adapter=phase29b.missing.fixture") });
	ExpectFailure(TEXT("\"adapterId\": \"phase29b.parser.fixture\""), TEXT("\"adapterId\": \"phase29b.parser.action\""), TEXT("P29B-FIXTURE-011 wrong fixture category"), { TEXT("stage=ExtensionDeclaration"), TEXT("category=FixtureResolver"), TEXT("adapter=phase29b.parser.action") });
	ExpectFailure(TEXT("\"adapterId\": \"phase29b.parser.fixture\""), TEXT("\"adapterId\": \"phase29b.parser.fixtureNoCap\""), TEXT("P29B-FIXTURE-011 missing fixture capability"), { TEXT("stage=ExtensionDeclaration"), TEXT("capability=existingActor.classTag") });
	ExpectFailure(TEXT("\"adapterId\": \"phase29b.parser.action\""), TEXT("\"adapterId\": \"phase29b.parser.fixture\""), TEXT("P29B-ACTION-006 wrong action category"), { TEXT("stage=ExtensionDeclaration"), TEXT("category=ActionAdapter"), TEXT("adapter=phase29b.parser.fixture") });
	ExpectFailure(TEXT("\"observation\": \"project.door.isOpen\""), TEXT("\"observation\": \"project.window.isOpen\""), TEXT("P29B-OBS-002 undeclared observation"), { TEXT("stage=ExtensionDeclaration"), TEXT("observation=project.window.isOpen") });

	FAINpcVisualAdapterDescriptor EmptyObservationNameDescriptor = ObservationDescriptor;
	EmptyObservationNameDescriptor.AdapterId = EmptyObservationNameId;
	EmptyObservationNameDescriptor.ObservationDeclarations[0].ObservationName.Reset();
	TestFalse(TEXT("P29B-OBS-006 rejects empty declaration observation name."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(EmptyObservationNameDescriptor).IsSuccess());
	FAINpcVisualAdapterDescriptor EmptySourceKindDescriptor = ObservationDescriptor;
	EmptySourceKindDescriptor.AdapterId = EmptySourceKindId;
	EmptySourceKindDescriptor.ObservationDeclarations[0].SourceKind.Reset();
	TestFalse(TEXT("P29B-OBS-006 rejects missing declaration source kind."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(EmptySourceKindDescriptor).IsSuccess());
	FAINpcVisualAdapterDescriptor EmptySamplingMethodDescriptor = ObservationDescriptor;
	EmptySamplingMethodDescriptor.AdapterId = EmptySamplingMethodId;
	EmptySamplingMethodDescriptor.ObservationDeclarations[0].SamplingMethod.Reset();
	TestFalse(TEXT("P29B-OBS-006 rejects missing declaration sampling method."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(EmptySamplingMethodDescriptor).IsSuccess());
	FAINpcVisualAdapterDescriptor EmptyObservationCapabilityDescriptor = ObservationDescriptor;
	EmptyObservationCapabilityDescriptor.AdapterId = EmptyObservationCapabilityId;
	EmptyObservationCapabilityDescriptor.ObservationDeclarations[0].Capability.Reset();
	TestFalse(TEXT("P29B-OBS-006 rejects missing declaration capability."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(EmptyObservationCapabilityDescriptor).IsSuccess());
	FAINpcVisualAdapterDescriptor BadValueTypeDescriptor = ObservationDescriptor;
	BadValueTypeDescriptor.AdapterId = BadValueTypeId;
	BadValueTypeDescriptor.ObservationDeclarations[0].ObservationName = TEXT("project.badValueType.isOpen");
	BadValueTypeDescriptor.ObservationDeclarations[0].ValueType = EAINpcVisualObservationValueType::String;
	TestTrue(TEXT("P29B-OBS-006 bad value-type descriptor registers for declaration validation."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(BadValueTypeDescriptor).IsSuccess());
	ExpectFailure(TEXT("\"observation\": \"project.door.isOpen\""), TEXT("\"observation\": \"project.badValueType.isOpen\""), TEXT("P29B-OBS-006 wrong declaration value type"), { TEXT("stage=ExtensionDeclaration"), TEXT("observation=project.badValueType.isOpen"), TEXT("boolean value type") });
	FAINpcVisualAdapterDescriptor BadSourceKindDescriptor = ObservationDescriptor;
	BadSourceKindDescriptor.AdapterId = BadSourceKindId;
	BadSourceKindDescriptor.ObservationDeclarations[0].ObservationName = TEXT("project.badSourceKind.isOpen");
	BadSourceKindDescriptor.ObservationDeclarations[0].SourceKind = TEXT("action-adapter");
	TestTrue(TEXT("P29B-OBS-006 bad source-kind descriptor registers for declaration validation."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(BadSourceKindDescriptor).IsSuccess());
	ExpectFailure(TEXT("\"observation\": \"project.door.isOpen\""), TEXT("\"observation\": \"project.badSourceKind.isOpen\""), TEXT("P29B-OBS-006 wrong declaration source kind"), { TEXT("stage=ExtensionDeclaration"), TEXT("observation=project.badSourceKind.isOpen"), TEXT("state-read source metadata") });
	FAINpcVisualAdapterDescriptor BadSamplingMethodDescriptor = ObservationDescriptor;
	BadSamplingMethodDescriptor.AdapterId = BadSamplingMethodId;
	BadSamplingMethodDescriptor.ObservationDeclarations[0].ObservationName = TEXT("project.badSampling.isOpen");
	BadSamplingMethodDescriptor.ObservationDeclarations[0].SamplingMethod = TEXT("poll");
	TestTrue(TEXT("P29B-OBS-006 bad sampling descriptor registers for declaration validation."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(BadSamplingMethodDescriptor).IsSuccess());
	ExpectFailure(TEXT("\"observation\": \"project.door.isOpen\""), TEXT("\"observation\": \"project.badSampling.isOpen\""), TEXT("P29B-OBS-006 wrong declaration sampling method"), { TEXT("stage=ExtensionDeclaration"), TEXT("observation=project.badSampling.isOpen"), TEXT("state-read source metadata") });
	FAINpcVisualAdapterDescriptor MissingSourceObjectDescriptor = ObservationDescriptor;
	MissingSourceObjectDescriptor.AdapterId = MissingSourceObjectId;
	MissingSourceObjectDescriptor.ObservationDeclarations[0].ObservationName = TEXT("project.missingSourceObject.isOpen");
	MissingSourceObjectDescriptor.ObservationDeclarations[0].bRequiresSourceObjectPath = false;
	TestTrue(TEXT("P29B-OBS-006 missing source-object metadata descriptor registers for declaration validation."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(MissingSourceObjectDescriptor).IsSuccess());
	ExpectFailure(TEXT("\"observation\": \"project.door.isOpen\""), TEXT("\"observation\": \"project.missingSourceObject.isOpen\""), TEXT("P29B-OBS-006 missing declaration source object metadata"), { TEXT("stage=ExtensionDeclaration"), TEXT("observation=project.missingSourceObject.isOpen"), TEXT("state-read source metadata") });
	FAINpcVisualAdapterDescriptor MissingSourceClassDescriptor = ObservationDescriptor;
	MissingSourceClassDescriptor.AdapterId = MissingSourceClassId;
	MissingSourceClassDescriptor.ObservationDeclarations[0].ObservationName = TEXT("project.missingSourceClass.isOpen");
	MissingSourceClassDescriptor.ObservationDeclarations[0].bRequiresSourceClass = false;
	TestTrue(TEXT("P29B-OBS-006 missing source-class metadata descriptor registers for declaration validation."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(MissingSourceClassDescriptor).IsSuccess());
	ExpectFailure(TEXT("\"observation\": \"project.door.isOpen\""), TEXT("\"observation\": \"project.missingSourceClass.isOpen\""), TEXT("P29B-OBS-006 missing declaration source class metadata"), { TEXT("stage=ExtensionDeclaration"), TEXT("observation=project.missingSourceClass.isOpen"), TEXT("state-read source metadata") });

	FAINpcVisualAdapterDescriptor BadObservationDescriptor = ObservationDescriptor;
	BadObservationDescriptor.AdapterId = BadObservationId;
	BadObservationDescriptor.ObservationDeclarations[0].ObservationName = TEXT("project.window.isOpen");
	BadObservationDescriptor.ObservationDeclarations[0].SamplingMethod = TEXT("poll");
	TestTrue(TEXT("Phase 2.9B bad observation descriptor registers for declaration validation."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(BadObservationDescriptor).IsSuccess());
	ExpectFailure(TEXT("\"observation\": \"project.door.isOpen\""), TEXT("\"observation\": \"project.window.isOpen\""), TEXT("P29B-OBS-006 wrong declaration metadata"), { TEXT("stage=ExtensionDeclaration"), TEXT("observation=project.window.isOpen"), TEXT("state-read source metadata") });
	FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::ObservationProvider, ObservationId, Owner);
	FAINpcVisualAdapterDescriptor WrongCategoryObservationDescriptor = ActionDescriptor;
	WrongCategoryObservationDescriptor.AdapterId = WrongCategoryObservationId;
	WrongCategoryObservationDescriptor.ObservationDeclarations = { Declaration };
	TestTrue(TEXT("P29B-OBS-007 wrong-category observation descriptor registers for declaration validation."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(WrongCategoryObservationDescriptor).IsSuccess());
	ExpectFailure(TEXT("\"observation\": \"project.door.isOpen\""), TEXT("\"observation\": \"project.door.isOpen\""), TEXT("P29B-OBS-007 rejects wrong observation provider category"), { TEXT("stage=ExtensionDeclaration"), TEXT("category=ObservationProvider"), TEXT("adapter=phase29b.parser.wrongCategoryObservation"), TEXT("actual=ActionAdapter"), TEXT("observation=project.door.isOpen") });
	FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::ActionAdapter, WrongCategoryObservationId, Owner);
	FAINpcVisualAdapterDescriptor CapabilityNameObservationDescriptor = ObservationDescriptor;
	CapabilityNameObservationDescriptor.AdapterId = CapabilityNameObservationId;
	CapabilityNameObservationDescriptor.ObservationDeclarations[0].ObservationName = TEXT("observation.project.door.isOpen");
	TestTrue(TEXT("P29B-OBS-004 capability-name observation descriptor registers for declaration validation."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(CapabilityNameObservationDescriptor).IsSuccess());
	ExpectFailure(TEXT("\"observation\": \"project.door.isOpen\""), TEXT("\"observation\": \"project.door.isOpen\""), TEXT("P29B-OBS-004 capability is not the observation name"), { TEXT("stage=ExtensionDeclaration"), TEXT("observation=project.door.isOpen") });
	FAINpcVisualAdapterDescriptor BadCapabilityDescriptor = ObservationDescriptor;
	BadCapabilityDescriptor.AdapterId = FName(TEXT("phase29b.parser.badCapability"));
	BadCapabilityDescriptor.ObservationDeclarations[0].Capability = TEXT("observation.project.missing");
	TestFalse(TEXT("P29B-OBS-007 rejects declaration capability missing from descriptor capabilities."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(BadCapabilityDescriptor).IsSuccess());

	Cleanup();
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAINpcVisualPhase28dInternalAdapterLifecycleBoundaryTest,
	"AINpc.Visual.Phase28d.InternalAdapterLifecycleBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcVisualPhase28dInternalAdapterLifecycleBoundaryTest::RunTest(const FString& Parameters)
{
	const TArray<FString> InternalAdapterIds = {
		TEXT("builtin.characterFixture"),
		TEXT("builtin.npcEvent"),
		TEXT("builtin.smartObjectAction")
	};
	TSet<FString> UniqueAdapterIds;
	for (const FString& AdapterId : InternalAdapterIds)
	{
		TestFalse(FString::Printf(TEXT("Internal adapter id '%s' must be unique."), *AdapterId), UniqueAdapterIds.Contains(AdapterId));
		UniqueAdapterIds.Add(AdapterId);
	}
	TestEqual(TEXT("Internal adapter id set has no duplicate ids."), UniqueAdapterIds.Num(), InternalAdapterIds.Num());

	const FString BaseScenario = TEXT(R"JSON({
		"schemaVersion": 2,
		"testId": "phase28d.lifecycle-boundary",
		"map": "/Game/Maps/AINpcTestMap",
		"timeoutSec": 1,
		"storyIds": ["TEST"],
		"phaseIds": ["phase2.8d"],
		"fixture": { "adapterId": "builtin.characterFixture", "kind": "characterWithSmartObject" },
		"persona": { "file": "AINpcVisualHarnessPhase27Persona.txt", "delayFillerFile": "AINpcVisualHarnessDelayFiller.txt", "delayFillerThreshold": 0.0 },
		"prompt": { "file": "AINpcVisualHarnessPhase27Prompt.txt", "variables": { "SmartObjectTargetId": "runtime.smartObjectTargetId" } },
		"steps": [
			{ "type": "world.event", "payload": { "adapterId": "builtin.npcEvent", "eventTag": "AINpc.Test.Gift", "targetRef": "fixture.npc", "payload": {} } },
			{ "type": "action.executeLatestIntent", "payload": { "adapterId": "builtin.smartObjectAction", "actorRef": "fixture.npc", "allowActionRejection": true } }
		],
		"expect": { "exists": "dialogueResponseObserved" }
	})JSON");

	const auto ParseScenarioText = [](const FString& ScenarioText, FAINpcVisualScenarioConfig& OutConfig, FString& OutError)
	{
		TSharedPtr<FJsonObject> JsonObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ScenarioText);
		if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
		{
			OutError = TEXT("scenario text did not parse as a JSON object");
			return false;
		}
		return ParseScenarioConfig(*JsonObject, OutConfig, OutError);
	};

	FAINpcVisualScenarioConfig ValidConfig;
	FString ParseError;
	TestTrue(TEXT("Lifecycle boundary base scenario parses before negative mutations."), ParseScenarioText(BaseScenario, ValidConfig, ParseError));

	const auto ExpectScenarioFailure = [this, &BaseScenario, &ParseScenarioText](const FString& Search, const FString& Replacement, const TCHAR* CaseName, const TCHAR* ExpectedErrorFragment)
	{
		const FString ScenarioText = BaseScenario.Replace(*Search, *Replacement);
		FAINpcVisualScenarioConfig CaseConfig;
		FString CaseError;
		TestFalse(FString(CaseName) + TEXT(" is rejected."), ParseScenarioText(ScenarioText, CaseConfig, CaseError));
		TestTrue(FString(CaseName) + TEXT(" reports expected diagnostic fragment."), CaseError.Contains(ExpectedErrorFragment));
	};

	ExpectScenarioFailure(TEXT("\"fixture\": { \"adapterId\": \"builtin.characterFixture\", \"kind\": \"characterWithSmartObject\" }"), TEXT("\"fixture\": { \"adapterId\": \"builtin.unknownFixture\", \"kind\": \"characterWithSmartObject\" }"), TEXT("bad fixture adapter id"), TEXT("fixture.adapterId"));
	ExpectScenarioFailure(TEXT("\"fixture\": { \"adapterId\": \"builtin.characterFixture\", \"kind\": \"characterWithSmartObject\" }"), TEXT("\"fixture\": { \"adapterId\": \"builtin.characterFixture\", \"kind\": \"npcWithSmartObject\" }"), TEXT("legacy fixture kind"), TEXT("fixture.kind"));
	ExpectScenarioFailure(TEXT("\"fixture\": { \"adapterId\": \"builtin.characterFixture\", \"kind\": \"characterWithSmartObject\" }"), TEXT("\"fixture\": { \"type\": \"npcWithSmartObject\", \"adapterId\": \"builtin.characterFixture\", \"kind\": \"characterWithSmartObject\" }"), TEXT("legacy fixture type"), TEXT("fixture.type"));
	ExpectScenarioFailure(TEXT("\"expect\": { \"exists\": \"dialogueResponseObserved\" }"), TEXT("\"allowActionRejection\": true, \"expect\": { \"exists\": \"dialogueResponseObserved\" }"), TEXT("legacy top-level action field"), TEXT("allowActionRejection"));
	ExpectScenarioFailure(TEXT("\"adapterId\": \"builtin.npcEvent\", \"eventTag\": \"AINpc.Test.Gift\", \"targetRef\": \"fixture.npc\", \"payload\": {}"), TEXT("\"adapterId\": \"builtin.smartObjectAction\", \"eventTag\": \"AINpc.Test.Gift\", \"targetRef\": \"fixture.npc\", \"payload\": {}"), TEXT("world.event using action adapter id"), TEXT("unsupported value"));
	ExpectScenarioFailure(TEXT("\"adapterId\": \"builtin.npcEvent\", \"eventTag\": \"AINpc.Test.Gift\", \"targetRef\": \"fixture.npc\", \"payload\": {}"), TEXT("\"adapterId\": \"builtin.npcEvent\", \"eventTriggerId\": \"legacy\", \"targetRef\": \"fixture.npc\", \"payload\": {}"), TEXT("legacy event trigger field"), TEXT("unknown field 'eventTriggerId'"));
	ExpectScenarioFailure(TEXT("\"adapterId\": \"builtin.npcEvent\", \"eventTag\": \"AINpc.Test.Gift\", \"targetRef\": \"fixture.npc\", \"payload\": {}"), TEXT("\"adapterId\": \"builtin.npcEvent\", \"eventTag\": \"AINpc.Test.Gift\", \"eventId\": \"AINpc.Test.Gift\", \"targetRef\": \"fixture.npc\", \"payload\": {}"), TEXT("world.event duplicate event selectors"), TEXT("exactly one of eventTag or eventId"));
	ExpectScenarioFailure(TEXT("\"adapterId\": \"builtin.npcEvent\", \"eventTag\": \"AINpc.Test.Gift\", \"targetRef\": \"fixture.npc\", \"payload\": {}"), TEXT("\"adapterId\": \"builtin.npcEvent\", \"eventTag\": \"AINpc.Test.Gift\", \"targetRef\": \"fixture.npc\", \"payload\": { \"legacy\": true }"), TEXT("world.event malformed adapter payload"), TEXT("payload.payload must be an empty object"));
	ExpectScenarioFailure(TEXT("\"adapterId\": \"builtin.smartObjectAction\", \"actorRef\": \"fixture.npc\", \"allowActionRejection\": true"), TEXT("\"adapterId\": \"builtin.npcEvent\", \"actorRef\": \"fixture.npc\", \"allowActionRejection\": true"), TEXT("action step using event adapter id"), TEXT("unsupported value"));
	ExpectScenarioFailure(TEXT("\"adapterId\": \"builtin.smartObjectAction\", \"actorRef\": \"fixture.npc\", \"allowActionRejection\": true"), TEXT("\"adapterId\": \"builtin.smartObjectAction\", \"actorRef\": \"fixture.smartObject\", \"allowActionRejection\": true"), TEXT("action step unsupported actor ref"), TEXT("actorRef has unsupported value"));
	ExpectScenarioFailure(TEXT("\"adapterId\": \"builtin.smartObjectAction\", \"actorRef\": \"fixture.npc\", \"allowActionRejection\": true"), TEXT("\"adapterId\": \"builtin.smartObjectAction\", \"actorRef\": \"fixture.npc\", \"allowActionRejection\": {}"), TEXT("action step malformed rejection policy"), TEXT("allowActionRejection must be boolean"));
	return true;
}

#endif
