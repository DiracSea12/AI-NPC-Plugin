#include "Test/AINpcDataDrivenVisualScenarioTest.h"
#include "Test/AINpcVisualObservationStore.h"

#include "Misc/AutomationTest.h"

namespace
{
	FAINpcRecordedVisualObservation MakeBoolObservation(const FString& Name, const int32 StepIndex, const double TimestampSeconds, const FString& SourceKind = TEXT("callback"), const FString& SourceId = TEXT("source"))
	{
		FAINpcRecordedVisualObservation Observation;
		Observation.Record.Name = Name;
		Observation.Record.ValueType = EAINpcVisualObservationValueType::Boolean;
		Observation.Record.BoolValue = true;
		Observation.Record.StepIndex = StepIndex;
		Observation.Record.TimestampSeconds = TimestampSeconds;
		Observation.Record.SourceKind = SourceKind;
		Observation.Record.SourceIdentity = SourceId;
		Observation.Record.AdapterOrProviderId = SourceId;
		return Observation;
	}

	FAINpcRecordedVisualObservation MakeBoolObservationValue(const FString& Name, const bool bValue, const int32 StepIndex, const double TimestampSeconds, const FString& SourceKind = TEXT("callback"), const FString& SourceId = TEXT("source"))
	{
		FAINpcRecordedVisualObservation Observation = MakeBoolObservation(Name, StepIndex, TimestampSeconds, SourceKind, SourceId);
		Observation.Record.BoolValue = bValue;
		return Observation;
	}

	FAINpcRecordedVisualObservation MakeStringObservation(const FString& Name, const FString& Value, const int32 StepIndex, const double TimestampSeconds, const FString& SourceKind = TEXT("callback"), const FString& SourceId = TEXT("source"))
	{
		FAINpcRecordedVisualObservation Observation;
		Observation.Record.Name = Name;
		Observation.Record.ValueType = EAINpcVisualObservationValueType::String;
		Observation.Record.StringValue = Value;
		Observation.Record.StepIndex = StepIndex;
		Observation.Record.TimestampSeconds = TimestampSeconds;
		Observation.Record.SourceKind = SourceKind;
		Observation.Record.SourceIdentity = SourceId;
		Observation.Record.AdapterOrProviderId = SourceId;
		return Observation;
	}

	FAINpcRecordedVisualObservation MakeIntegerObservation(const FString& Name, const int32 Value, const int32 StepIndex, const double TimestampSeconds, const FString& SourceKind = TEXT("callback"), const FString& SourceId = TEXT("source"))
	{
		FAINpcRecordedVisualObservation Observation;
		Observation.Record.Name = Name;
		Observation.Record.ValueType = EAINpcVisualObservationValueType::Integer;
		Observation.Record.IntegerValue = Value;
		Observation.Record.StepIndex = StepIndex;
		Observation.Record.TimestampSeconds = TimestampSeconds;
		Observation.Record.SourceKind = SourceKind;
		Observation.Record.SourceIdentity = SourceId;
		Observation.Record.AdapterOrProviderId = SourceId;
		return Observation;
	}

	FAINpcVisualObservationSourceInfo MakeSource(const FString& SourceKind = TEXT("callback"), const FString& SourceId = TEXT("source"))
	{
		FAINpcVisualObservationSourceInfo Source;
		Source.SourceKind = SourceKind;
		Source.SourceIdentity = SourceId;
		Source.AdapterOrProviderId = SourceId;
		return Source;
	}

	FAINpcVisualObservationReadinessRecord MakeReadiness(const FString& Name, const int32 StepIndex, const double CoverageStartSeconds, const double CoverageEndSeconds)
	{
		return FAINpcVisualObservationStore::MakeReadinessRecord(Name, MakeSource(), StepIndex, CoverageEndSeconds, CoverageStartSeconds, CoverageEndSeconds);
	}

	FAINpcVisualObservationReadinessRecord MakeReadinessFromSource(const FString& Name, const FString& SourceKind, const FString& SourceId, const int32 StepIndex, const double CoverageStartSeconds, const double CoverageEndSeconds)
	{
		return FAINpcVisualObservationStore::MakeReadinessRecord(Name, MakeSource(SourceKind, SourceId), StepIndex, CoverageEndSeconds, CoverageStartSeconds, CoverageEndSeconds);
	}

	FAINpcVisualObservationWindow MakeStepWindow(const int32 StepIndex, const double StartSeconds, const double EndSeconds)
	{
		FAINpcVisualObservationWindow Window;
		Window.StepIndex = StepIndex;
		Window.StartSeconds = StartSeconds;
		Window.EndSeconds = EndSeconds;
		return Window;
	}

	FAINpcVisualObservationWindow MakeHistoryWindow(const double StartSeconds, const double EndSeconds)
	{
		FAINpcVisualObservationWindow Window;
		Window.StepIndex = INDEX_NONE;
		Window.StartSeconds = StartSeconds;
		Window.EndSeconds = EndSeconds;
		Window.bScenarioHistory = true;
		return Window;
	}

	FAINpcVisualObservationStore MakeStore(TArray<FAINpcRecordedVisualObservation>& Records)
	{
		FAINpcVisualObservationStore Store;
		FString Failure;
		for (FAINpcRecordedVisualObservation& Record : Records)
		{
			Store.RecordObservation(MoveTemp(Record.Record), false, Failure);
		}
		return Store;
	}

	void MarkReadyForWindow(FAINpcVisualObservationStore& Store, const FString& Name, const FString& SourceKind, const FString& SourceId, const int32 StepIndex, const double CoverageStartSeconds, const double CoverageEndSeconds)
	{
		Store.MarkSourceReadyForWindow(Name, MakeSource(SourceKind, SourceId), MakeStepWindow(StepIndex, CoverageStartSeconds, CoverageEndSeconds));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAINpcVisualObservationWindowBehaviorTest,
	"AINpc.Visual.Observation.WindowBehavior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcVisualObservationWindowBehaviorTest::RunTest(const FString& Parameters)
{
	TArray<FAINpcRecordedVisualObservation> Records;
	Records.Add(MakeBoolObservation(TEXT("dialogueResponseObserved"), 0, 1.0));
	Records.Add(MakeBoolObservation(TEXT("dialogueResponseObserved"), 1, 11.0));
	FAINpcVisualObservationStore Store;
	FString StoreFailure;
	for (FAINpcRecordedVisualObservation& Record : Records) { Store.RecordObservation(MoveTemp(Record.Record), false, StoreFailure); }

	FAINpcRecordedVisualObservation Found;
	FAINpcVisualAssertionFailureDetail Failure;
	TestTrue(TEXT("Current-step lookup must skip stale records and find the in-window match."),
		Store.TryGetLatestObservationInWindow(TEXT("dialogueResponseObserved"), MakeStepWindow(1, 10.0, 12.0), Found, &Failure));
	TestEqual(TEXT("Current-step lookup returns the in-window step."), Found.Record.StepIndex, 1);

	TestTrue(TEXT("Scenario-history lookup accepts completed-step records."),
		Store.TryGetLatestObservationInWindow(TEXT("dialogueResponseObserved"), MakeHistoryWindow(0.0, 12.0), Found, &Failure));
	TestEqual(TEXT("Scenario-history lookup returns latest record across steps."), Found.Record.StepIndex, 1);

	TestFalse(TEXT("Step-scoped lookup fails when only a stale step exists."),
		Store.TryGetLatestObservationInWindow(TEXT("dialogueResponseObserved"), MakeStepWindow(2, 20.0, 22.0), Found, &Failure));
	TestEqual(TEXT("Stale step failure is diagnosed as stale."), Failure.Category, FString(TEXT("stale")));

	FAINpcVisualScenarioAssertion HistoryScopedAssertion;
	HistoryScopedAssertion.Operator = TEXT("equals");
	HistoryScopedAssertion.Scope = EAINpcVisualObservationScope::ScenarioHistory;
	HistoryScopedAssertion.Observation = TEXT("dialogueResponseObserved");
	HistoryScopedAssertion.bHasEqualsBool = true;
	HistoryScopedAssertion.EqualsBool = true;
	TestTrue(TEXT("Explicit scenarioHistory assertion scope may read an earlier-step observation."),
		Store.EvaluateEqualsAssertion(HistoryScopedAssertion, MakeStepWindow(2, 20.0, 22.0), &Failure));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAINpcVisualReadinessWindowBehaviorTest,
	"AINpc.Visual.Observation.ReadinessWindowBehavior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcVisualReadinessWindowBehaviorTest::RunTest(const FString& Parameters)
{
	TArray<FAINpcVisualObservationReadinessRecord> Records;
	FAINpcVisualObservationStore Store;
	FAINpcVisualAssertionFailureDetail Failure;

	TestFalse(TEXT("notExists readiness fails when no source sampled the checked window."),
		Store.HasReadinessCoverageInWindow(TEXT("partialResponseObserved"), MakeStepWindow(1, 10.0, 12.0), &Failure));
	TestEqual(TEXT("Missing readiness uses readiness-window-coverage category."), Failure.Category, FString(TEXT("readiness-window-coverage")));

	MarkReadyForWindow(Store, TEXT("partialResponseObserved"), TEXT("callback"), TEXT("source"), 0, 1.0, 2.0);
	TestFalse(TEXT("notExists readiness fails when coverage exists only in another step."),
		Store.HasReadinessCoverageInWindow(TEXT("partialResponseObserved"), MakeStepWindow(1, 10.0, 12.0), &Failure));
	TestEqual(TEXT("Stale readiness still uses readiness-window-coverage category."), Failure.Category, FString(TEXT("readiness-window-coverage")));

	Store.MarkSourceReady(TEXT("partialResponseObserved"), MakeSource(), 1, 11.0);
	TestFalse(TEXT("notExists readiness fails when point readiness sits inside but does not cover the whole window."),
		Store.HasReadinessCoverageInWindow(TEXT("partialResponseObserved"), MakeStepWindow(1, 10.0, 12.0), &Failure));
	TestEqual(TEXT("Point readiness is diagnosed as missing window coverage."), Failure.Category, FString(TEXT("readiness-window-coverage")));

	FAINpcVisualObservationStore StaleSameStepStore;
	MarkReadyForWindow(StaleSameStepStore, TEXT("partialResponseObserved"), TEXT("callback"), TEXT("source"), 1, 8.0, 9.0);
	TestFalse(TEXT("notExists readiness fails when same-step coverage is stale and does not reach the checked window."),
		StaleSameStepStore.HasReadinessCoverageInWindow(TEXT("partialResponseObserved"), MakeStepWindow(1, 10.0, 12.0), &Failure));

	MarkReadyForWindow(Store, TEXT("partialResponseObserved"), TEXT("callback"), TEXT("source"), 1, 10.0, 12.0);
	TestTrue(TEXT("notExists readiness passes only when readiness coverage spans the checked window."),
		Store.HasReadinessCoverageInWindow(TEXT("partialResponseObserved"), MakeStepWindow(1, 10.0, 12.0), &Failure));

	FAINpcVisualObservationStore RuntimeStyleStore;
	MarkReadyForWindow(RuntimeStyleStore, TEXT("actionTargetReached"), TEXT("actor"), TEXT("builtin.characterDriver"), 2, 20.0, 22.0);
	TestTrue(TEXT("Runtime-style polled observation readiness can cover a whole checked window."),
		RuntimeStyleStore.HasReadinessCoverageInWindow(TEXT("actionTargetReached"), MakeStepWindow(2, 20.0, 22.0), &Failure));

	FAINpcVisualScenarioAssertion RuntimeNotExists;
	RuntimeNotExists.Operator = TEXT("notExists");
	RuntimeNotExists.Observation = TEXT("actionTargetReached");
	TestTrue(TEXT("Runtime-style full-window coverage lets notExists pass when the polled observation is absent."),
		RuntimeStyleStore.EvaluateNotExistsAssertion(RuntimeNotExists, MakeStepWindow(2, 20.0, 22.0), &Failure));
	FString RuntimeStoreFailure;
	RuntimeStyleStore.RecordObservation(MakeBoolObservation(TEXT("actionTargetReached"), 2, 21.0).Record, false, RuntimeStoreFailure);
	TestFalse(TEXT("The same runtime-style full-window coverage does not hide an actual observation presence."),
		RuntimeStyleStore.EvaluateNotExistsAssertion(RuntimeNotExists, MakeStepWindow(2, 20.0, 22.0), &Failure));
	TestEqual(TEXT("Present observation makes notExists fail as unexpected-presence."), Failure.Category, FString(TEXT("unexpected-presence")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAINpcVisualFinalSourceBehaviorTest,
	"AINpc.Visual.Observation.FinalSourceBehavior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcVisualFinalSourceBehaviorTest::RunTest(const FString& Parameters)
{
	FAINpcVisualObservationSourceInfo CallbackSource;
	CallbackSource.SourceKind = TEXT("callback");
	CallbackSource.SourceIdentity = TEXT("OnDialogueResponse");
	CallbackSource.AdapterOrProviderId = TEXT("provider.primary.dialogue");
	TestTrue(TEXT("Runtime callback source may satisfy final player-visible success."),
		FAINpcVisualObservationStore::IsFinalSuccessSourceAllowed(CallbackSource));

	FAINpcVisualObservationSourceInfo RuntimeProviderSource;
	RuntimeProviderSource.SourceKind = TEXT("observation-provider");
	RuntimeProviderSource.SourceIdentity = TEXT("project.quest.StateObservationProvider");
	RuntimeProviderSource.SourceObjectPath = TEXT("/Game/Test/QuestStateProvider.QuestStateProvider");
	RuntimeProviderSource.SourceClass = TEXT("UQuestStateObservationProvider");
	RuntimeProviderSource.SamplingMethod = TEXT("state-read");
	RuntimeProviderSource.AdapterOrProviderId = TEXT("project.quest.state");
	TestTrue(TEXT("Declared observation provider with concrete runtime identity and state metadata may satisfy final player-visible success."),
		FAINpcVisualObservationStore::IsFinalSuccessSourceAllowed(RuntimeProviderSource));

	FAINpcVisualObservationSourceInfo ProviderWithoutRuntimeState = RuntimeProviderSource;
	ProviderWithoutRuntimeState.SourceObjectPath.Reset();
	ProviderWithoutRuntimeState.SourceClass.Reset();
	TestFalse(TEXT("Declared observation provider without object/class runtime state metadata must not satisfy final player-visible success."),
		FAINpcVisualObservationStore::IsFinalSuccessSourceAllowed(ProviderWithoutRuntimeState));

	FAINpcVisualObservationSourceInfo ActionSource = RuntimeProviderSource;
	ActionSource.SourceIdentity = TEXT("builtin.smartObjectAction");
	ActionSource.SamplingMethod = TEXT("action-attempt");
	ActionSource.AdapterOrProviderId = TEXT("builtin.smartObjectAction");
	TestFalse(TEXT("Action/provider attempt facts must not satisfy final player-visible success."),
		FAINpcVisualObservationStore::IsFinalSuccessSourceAllowed(ActionSource));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAINpcVisualEqualsFalseBehaviorTest,
	"AINpc.Visual.Observation.EqualsFalseBehavior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcVisualEqualsFalseBehaviorTest::RunTest(const FString& Parameters)
{
	FAINpcVisualScenarioAssertion Assertion;
	Assertion.Operator = TEXT("equals");
	Assertion.Observation = TEXT("featureFlagVisible");
	Assertion.bHasEqualsBool = true;
	Assertion.EqualsBool = false;

	TArray<FAINpcRecordedVisualObservation> FalseRecords;
	FalseRecords.Add(MakeBoolObservationValue(TEXT("featureFlagVisible"), false, 1, 11.0));
	FAINpcVisualAssertionFailureDetail Failure;
	TestTrue(TEXT("equals false must pass when the recorded boolean is false."),
		MakeStore(FalseRecords).EvaluateEqualsAssertion(Assertion, MakeStepWindow(1, 10.0, 12.0), &Failure));

	TArray<FAINpcRecordedVisualObservation> TrueRecords;
	TrueRecords.Add(MakeBoolObservationValue(TEXT("featureFlagVisible"), true, 1, 11.0));
	TestFalse(TEXT("equals false must fail when the recorded boolean is true."),
		MakeStore(TrueRecords).EvaluateEqualsAssertion(Assertion, MakeStepWindow(1, 10.0, 12.0), &Failure));
	TestEqual(TEXT("equals false mismatch is diagnosed as value-mismatch."), Failure.Category, FString(TEXT("value-mismatch")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAINpcVisualDegradedAndAdapterFinalGateBehaviorTest,
	"AINpc.Visual.Observation.DegradedAndAdapterFinalGateBehavior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcVisualDegradedAndAdapterFinalGateBehaviorTest::RunTest(const FString& Parameters)
{
	FAINpcVisualObservationSourceInfo FallbackSource;
	FallbackSource.SourceKind = TEXT("fallback");
	FallbackSource.SourceIdentity = TEXT("OnDegraded");
	FallbackSource.AdapterOrProviderId = TEXT("provider.fallback");
	TestFalse(TEXT("Fallback/degraded source kind must not satisfy final player-visible source gate."),
		FAINpcVisualObservationStore::IsFinalSuccessSourceAllowed(FallbackSource));

	FAINpcVisualObservationSourceInfo RuntimeProviderSource;
	RuntimeProviderSource.SourceKind = TEXT("observation-provider");
	RuntimeProviderSource.SourceIdentity = TEXT("project.quest.StateObservationProvider");
	RuntimeProviderSource.SourceObjectPath = TEXT("/Game/Test/QuestStateProvider.QuestStateProvider");
	RuntimeProviderSource.SourceClass = TEXT("UQuestStateObservationProvider");
	RuntimeProviderSource.AdapterOrProviderId = TEXT("project.quest.state");

	FAINpcVisualObservationSourceInfo AdapterAcceptanceSource = RuntimeProviderSource;
	AdapterAcceptanceSource.SamplingMethod = TEXT("adapter-acceptance");
	TestFalse(TEXT("Adapter acceptance facts must not satisfy final player-visible source gate."),
		FAINpcVisualObservationStore::IsFinalSuccessSourceAllowed(AdapterAcceptanceSource));

	FAINpcVisualObservationSourceInfo ActionSource = RuntimeProviderSource;
	ActionSource.SourceIdentity = TEXT("builtin.smartObjectAction");
	ActionSource.SamplingMethod = TEXT("action-execution");
	ActionSource.AdapterOrProviderId = TEXT("builtin.smartObjectAction");
	TestFalse(TEXT("SmartObject action adapter facts must not satisfy final player-visible source gate."),
		FAINpcVisualObservationStore::IsFinalSuccessSourceAllowed(ActionSource));

	FAINpcVisualObservationSourceInfo FailurePathSource = RuntimeProviderSource;
	FailurePathSource.SamplingMethod = TEXT("failure-path");
	FailurePathSource.AdapterOrProviderId = TEXT("provider.failure");
	TestFalse(TEXT("Failure-path provider facts must not satisfy final player-visible source gate."),
		FAINpcVisualObservationStore::IsFinalSuccessSourceAllowed(FailurePathSource));

	FAINpcVisualObservationSourceInfo DegradedProviderSource = RuntimeProviderSource;
	DegradedProviderSource.SamplingMethod = TEXT("degraded");
	DegradedProviderSource.AdapterOrProviderId = TEXT("provider.degraded");
	TestFalse(TEXT("Degraded provider facts must not satisfy final player-visible source gate."),
		FAINpcVisualObservationStore::IsFinalSuccessSourceAllowed(DegradedProviderSource));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAINpcVisualObservationIsolationAndTypeFailureTest,
	"AINpc.Visual.Observation.IsolationAndTypeFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcVisualObservationIsolationAndTypeFailureTest::RunTest(const FString& Parameters)
{
	TArray<FAINpcRecordedVisualObservation> PreviousRunLookupRecords;
	PreviousRunLookupRecords.Add(MakeBoolObservationValue(TEXT("dialogueResponseObserved"), true, 0, 1.0, TEXT("callback"), TEXT("run.previous")));

	FAINpcRecordedVisualObservation Found;
	FAINpcVisualAssertionFailureDetail Failure;
	TestFalse(TEXT("A previous run/step observation cannot satisfy the current run window."),
		MakeStore(PreviousRunLookupRecords).TryGetLatestObservationInWindow(TEXT("dialogueResponseObserved"), MakeStepWindow(0, 10.0, 12.0), Found, &Failure));
	TestEqual(TEXT("Cross-run stale observation is diagnosed as stale."), Failure.Category, FString(TEXT("stale")));

	TArray<FAINpcRecordedVisualObservation> PreviousRunIsolationRecords;
	PreviousRunIsolationRecords.Add(MakeBoolObservationValue(TEXT("dialogueResponseObserved"), true, 0, 1.0, TEXT("callback"), TEXT("run.previous")));
	FAINpcVisualObservationStore PreviousRunStore = MakeStore(PreviousRunIsolationRecords);
	FAINpcVisualObservationStore CurrentRunStore;
	TestFalse(TEXT("A fresh per-run observation store starts without previous scenario records."),
		CurrentRunStore.HasObservationInWindow(TEXT("dialogueResponseObserved"), MakeHistoryWindow(0.0, 12.0)));
	TestTrue(TEXT("The previous run store still proves the isolation test would catch shared state."),
		PreviousRunStore.HasObservationInWindow(TEXT("dialogueResponseObserved"), MakeHistoryWindow(0.0, 12.0)));

	FAINpcVisualScenarioAssertion BooleanAssertion;
	BooleanAssertion.Operator = TEXT("equals");
	BooleanAssertion.Observation = TEXT("dialogueResponseObserved");
	BooleanAssertion.bHasEqualsBool = true;
	BooleanAssertion.EqualsBool = true;
	TArray<FAINpcRecordedVisualObservation> StringRecords;
	StringRecords.Add(MakeStringObservation(TEXT("dialogueResponseObserved"), TEXT("true"), 1, 11.0));
	TestFalse(TEXT("Boolean equals rejects string-typed observation values."),
		MakeStore(StringRecords).EvaluateEqualsAssertion(BooleanAssertion, MakeStepWindow(1, 10.0, 12.0), &Failure));
	TestEqual(TEXT("Boolean/string equals mismatch is diagnosed as type-mismatch."), Failure.Category, FString(TEXT("type-mismatch")));
	TestEqual(TEXT("Type mismatch diagnostic preserves observation identity."), Failure.ObservationName, FString(TEXT("dialogueResponseObserved")));
	TestFalse(TEXT("Type mismatch diagnostic does not leak raw string payload."), Failure.Message.Contains(TEXT("true")));

	FAINpcVisualScenarioAssertion StringAssertion;
	StringAssertion.Operator = TEXT("equals");
	StringAssertion.Observation = TEXT("lastActionFailure");
	StringAssertion.EqualsString = TEXT("expected failure text");
	TArray<FAINpcRecordedVisualObservation> IntegerRecords;
	IntegerRecords.Add(MakeIntegerObservation(TEXT("lastActionFailure"), 42, 1, 11.0));
	TestFalse(TEXT("String equals rejects integer-typed observation values."),
		MakeStore(IntegerRecords).EvaluateEqualsAssertion(StringAssertion, MakeStepWindow(1, 10.0, 12.0), &Failure));
	TestEqual(TEXT("Integer/string equals mismatch is diagnosed as type-mismatch."), Failure.Category, FString(TEXT("type-mismatch")));

	TArray<FAINpcRecordedVisualObservation> MismatchedStringRecords;
	MismatchedStringRecords.Add(MakeStringObservation(TEXT("lastActionFailure"), TEXT("secret-token-should-not-appear"), 1, 11.0));
	TestFalse(TEXT("String equals mismatch fails without echoing sensitive values."),
		MakeStore(MismatchedStringRecords).EvaluateEqualsAssertion(StringAssertion, MakeStepWindow(1, 10.0, 12.0), &Failure));
	TestEqual(TEXT("String mismatch is diagnosed as value-mismatch."), Failure.Category, FString(TEXT("value-mismatch")));
	TestTrue(TEXT("String mismatch diagnostic reports lengths."), Failure.Message.Contains(TEXT("length")));
	TestFalse(TEXT("String mismatch diagnostic redacts actual value."), Failure.Message.Contains(TEXT("secret-token-should-not-appear")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAINpcVisualNotExistsUnsupportedReadinessSourceTest,
	"AINpc.Visual.Observation.NotExistsUnsupportedReadinessSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcVisualNotExistsUnsupportedReadinessSourceTest::RunTest(const FString& Parameters)
{
	FAINpcVisualScenarioAssertion Assertion;
	Assertion.Operator = TEXT("notExists");
	Assertion.Observation = TEXT("actionTargetReached");

	FAINpcVisualObservationStore WrongNameStore;
	MarkReadyForWindow(WrongNameStore, TEXT("waitingStateObserved"), TEXT("actor"), TEXT("builtin.characterDriver"), 2, 20.0, 22.0);
	FAINpcVisualAssertionFailureDetail Failure;
	TestFalse(TEXT("Readiness for another observation cannot satisfy notExists."),
		WrongNameStore.EvaluateNotExistsAssertion(Assertion, MakeStepWindow(2, 20.0, 22.0), &Failure));
	TestEqual(TEXT("Wrong-name readiness leaves absence proof missing."), Failure.Category, FString(TEXT("readiness-window-coverage")));

	FAINpcVisualObservationStore UnsupportedSourceStore;
	MarkReadyForWindow(UnsupportedSourceStore, TEXT("actionTargetReached"), TEXT("observation-provider"), TEXT("unsupported.provider"), 2, 20.0, 22.0);
	TestFalse(TEXT("Unsupported readiness source cannot produce notExists success."),
		UnsupportedSourceStore.EvaluateNotExistsAssertion(Assertion, MakeStepWindow(2, 20.0, 22.0), &Failure));
	TestEqual(TEXT("Unsupported readiness source is diagnosed distinctly."), Failure.Category, FString(TEXT("readiness-source")));
	TestEqual(TEXT("Unsupported source diagnostic preserves source kind."), Failure.SourceKind, FString(TEXT("observation-provider")));
	return true;
}
