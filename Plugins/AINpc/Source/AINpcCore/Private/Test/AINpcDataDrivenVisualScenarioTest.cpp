#include "Test/AINpcDataDrivenVisualScenarioTest.h"
#include "AINpcVisualInternalAdapters.h"
#include "AINpcVisualObservationStore.h"

#include "Animation/AnimMontage.h"
#include "Components/AINpcComponent.h"
#include "Data/NpcPersonaDataAsset.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Events/NpcEventPayloadBlueprintLibrary.h"
#include "Events/NpcEventSubsystem.h"
#include "GameplayTagContainer.h"
#include "SmartObjectBridge/SmartObjectBridgeContext.h"
#include "Test/AINpcDialogueVisualTestSupport.h"
#include "Test/AINpcSmartObjectActionTestSupport.h"
#include "Test/AINpcTestCharacter.h"
#include "Test/AINpcTestSmartObjectActor.h"
#include "Test/AINpcVisualTestExtensionInternal.h"
#include "TimerManager.h"

namespace
{
	const float InitialDialogueDelaySeconds = 3.0f;
	const TCHAR* SmartObjectTargetIdVariable = TEXT("SmartObjectTargetId");
	const TCHAR* FixtureNpcRef = TEXT("fixture.npc");
	const TCHAR* FixtureActorRef = TEXT("fixture.actor");
	const TCHAR* ProjectFixtureKind = TEXT("existingActor");
	const TCHAR* ExistingActorCapability = TEXT("existingActor.classTag");
	const float SmartObjectSearchRadius = 1200.0f;
	const int32 SmartObjectClaimPriority = 2;

	struct FAINpcBuiltInCharacterDriver
	{
		AAINpcTestCharacter& Character;

		explicit FAINpcBuiltInCharacterDriver(AAINpcTestCharacter& InCharacter)
			: Character(InCharacter)
		{
		}

		UWorld* GetWorld() const { return Character.GetWorld(); }
		AActor* GetActor() const { return &Character; }
		UObject* CreatePersonaOuter() const { return &Character; }
		UAINpcComponent* GetNpcComponent() const { return Character.NpcComponent; }
		void SetVisibleStateText(const FString& Text) const { Character.SetVisibleStateText(Text); }
		void SetVisibleDialogueText(const FString& Text) const { Character.SetVisibleDialogueText(Text); }
		void BeginVisualActionMove(const FTransform& Transform, const FString& TargetId) const { Character.BeginVisualActionMove(Transform, TargetId); }
		bool HasReachedVisualActionTarget() const { return Character.HasReachedVisualActionTarget(); }
		double GetVisualActionTargetDistance() const { return Character.GetVisualActionTargetDistance(); }
	};

	bool IsKnownBooleanObservation(const FString& Name)
	{
		static const TSet<FString> KnownBooleanObservations = {
			TEXT("sessionStarted"),
			TEXT("waitingStateObserved"),
			TEXT("speakingStateObserved"),
			TEXT("dialogueResponseObserved"),
			TEXT("partialResponseObserved"),
			TEXT("structuredResponseObserved"),
			TEXT("actionIntentObserved"),
			TEXT("eventTriggerBroadcast"),
			TEXT("eventDelayMaskingStartObserved"),
			TEXT("delayMaskingStartObserved"),
			TEXT("delayMaskingEndObserved"),
			TEXT("actionExecutionAccepted"),
			TEXT("actionRejectedVisible"),
			TEXT("actionTargetReached"),
			TEXT("actionTargetReachedHoldElapsed")
		};
		return KnownBooleanObservations.Contains(Name);
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
			if (CloseIndex > OpenIndex + 1)
			{
				Placeholders.AddUnique(Text.Mid(OpenIndex + 1, CloseIndex - OpenIndex - 1));
			}
			SearchFrom = CloseIndex + 1;
		}
		return Placeholders;
	}

	TArray<FString> FindUnknownPromptPlaceholders(const FString& Text, const TMap<FString, FString>& Variables)
	{
		TArray<FString> Unknown;
		for (const FString& Placeholder : FindPromptPlaceholders(Text))
		{
			if (!Variables.Contains(Placeholder))
			{
				Unknown.Add(FString::Printf(TEXT("{%s}"), *Placeholder));
			}
		}
		return Unknown;
	}

	FString DescribeUnresolvedPromptPlaceholders(const TArray<FString>& Placeholders)
	{
		return Placeholders.IsEmpty() ? TEXT("<none>") : FString::Join(Placeholders, TEXT(", "));
	}
}

struct FAINpcInternalDialogueObservationProvider
{
	UAINpcComponent* NpcComponent = nullptr;
	TFunction<void(const FString&, bool, const FAINpcVisualObservationSourceInfo&, bool)> RecordBool;
	TFunction<void(const FString&, int32, const FAINpcVisualObservationSourceInfo&)> RecordInteger;
	TFunction<void(const FString&, const FAINpcVisualObservationSourceInfo&)> MarkReady;
	TFunction<bool(const FString&)> HasBool;
	TFunction<void(const FString&)> SetVisibleDialogueText;
	TFunction<void()> UpdateDialogueStateEvidence;
	TFunction<bool()> HasLatestActionIntent;
	TFunction<void(const FString&)> Fail;
	TFunction<void(const FString&, const FColor&, float)> ShowStatus;
	TFunction<FAINpcVisualObservationSourceInfo(const TCHAR*)> MakeDialogueSource;
	FDelegateHandle SessionStartedHandle;
	FDelegateHandle ResponseHandle;
	FDelegateHandle PartialResponseHandle;
	FDelegateHandle ErrorHandle;
	FDelegateHandle SessionEndedHandle;
	FDelegateHandle DegradedHandle;
	FDelegateHandle DelayMaskingStartHandle;
	FDelegateHandle DelayMaskingEndHandle;
	FString LastNpcResponseText;
	FString LastPartialResponseText;
	FString LastDelayFillerText;

	bool Start(UAINpcComponent& InNpcComponent, FString& OutFailureReason)
	{
		if (NpcComponent)
		{
			OutFailureReason = TEXT("Dialogue observation provider is already bound.");
			return false;
		}
		NpcComponent = &InNpcComponent;
		SessionStartedHandle = NpcComponent->OnDialogueSessionStartedNative().AddRaw(this, &FAINpcInternalDialogueObservationProvider::OnSessionStarted);
		ResponseHandle = NpcComponent->OnDialogueResponseNative().AddRaw(this, &FAINpcInternalDialogueObservationProvider::OnResponse);
		PartialResponseHandle = NpcComponent->OnDialoguePartialResponseNative().AddRaw(this, &FAINpcInternalDialogueObservationProvider::OnPartialResponse);
		ErrorHandle = NpcComponent->OnDialogueErrorNative().AddRaw(this, &FAINpcInternalDialogueObservationProvider::OnError);
		SessionEndedHandle = NpcComponent->OnDialogueSessionEndedNative().AddRaw(this, &FAINpcInternalDialogueObservationProvider::OnSessionEnded);
		DegradedHandle = NpcComponent->OnDialogueDegradedNative().AddRaw(this, &FAINpcInternalDialogueObservationProvider::OnDegraded);
		DelayMaskingStartHandle = NpcComponent->OnDelayMaskingStartNative().AddRaw(this, &FAINpcInternalDialogueObservationProvider::OnDelayMaskingStart);
		DelayMaskingEndHandle = NpcComponent->OnDelayMaskingEndNative().AddRaw(this, &FAINpcInternalDialogueObservationProvider::OnDelayMaskingEnd);
		return true;
	}

	void Stop()
	{
		if (!NpcComponent) { return; }
		NpcComponent->OnDialogueSessionStartedNative().Remove(SessionStartedHandle);
		NpcComponent->OnDialogueResponseNative().Remove(ResponseHandle);
		NpcComponent->OnDialoguePartialResponseNative().Remove(PartialResponseHandle);
		NpcComponent->OnDialogueErrorNative().Remove(ErrorHandle);
		NpcComponent->OnDialogueSessionEndedNative().Remove(SessionEndedHandle);
		NpcComponent->OnDialogueDegradedNative().Remove(DegradedHandle);
		NpcComponent->OnDelayMaskingStartNative().Remove(DelayMaskingStartHandle);
		NpcComponent->OnDelayMaskingEndNative().Remove(DelayMaskingEndHandle);
		NpcComponent = nullptr;
	}

	void OnSessionStarted();
	void OnResponse(const FString& Text);
	void OnPartialResponse(const FString& Text);
	void OnError(const FString& ErrorMessage);
	void OnSessionEnded();
	void OnDegraded(const FString& FallbackResponse, const FString& DegradedFailureReason);
	void OnDelayMaskingStart(UAnimMontage* Montage, const FText& FillerText);
	void OnDelayMaskingEnd();
};

struct FAINpcVisualScenarioRuntimeView
{
	UWorld* World = nullptr;
	FString TestId;
	FString RunId;
	AAINpcTestSmartObjectActor* SmartObject = nullptr;
	TUniquePtr<FAINpcBuiltInCharacterDriver> CharacterDriver;
	TUniquePtr<FAINpcInternalDialogueObservationProvider> DialogueObservationProvider;
	TSharedPtr<AINpc::Visual::TestInternal::FAdapterRunView> AdapterRunView;
	TWeakObjectPtr<AActor> ProjectFixtureActor;

	UWorld* GetWorld() const { return CharacterDriver ? CharacterDriver->GetWorld() : World; }
	double GetTimeSeconds(const double FallbackSeconds) const { const UWorld* RunWorld = GetWorld(); return RunWorld ? RunWorld->GetTimeSeconds() : FallbackSeconds; }
	UAINpcComponent* GetNpcComponent() const { return CharacterDriver ? CharacterDriver->GetNpcComponent() : nullptr; }
	AActor* GetNpcActor() const { return CharacterDriver ? CharacterDriver->GetActor() : nullptr; }
	AActor* GetProjectFixtureActor() const { return ProjectFixtureActor.Get(); }
	UObject* CreatePersonaOuter() const { return CharacterDriver ? CharacterDriver->CreatePersonaOuter() : GetWorld(); }
	bool HasSmartObjectFixture() const { return SmartObject != nullptr; }
	void SetSmartObjectInteractionState(const bool bEnabled) const { if (SmartObject) { SmartObject->SetInteractionState(bEnabled); } }
	TArray<FString> GetAvailableSmartObjectTargetsForExecution() const { return GetNpcComponent() ? GetNpcComponent()->GetAvailableSmartObjectTargetsForExecution() : TArray<FString>(); }
	bool TryGetLatestActionIntent(FNpcAction& OutAction) const { return GetNpcComponent() && GetNpcComponent()->TryGetLatestActionIntent(OutAction); }
	void AddEventSubscriptionTag(const FGameplayTag& RouteTag) const { if (UAINpcComponent* NpcComponent = GetNpcComponent()) { NpcComponent->EventSubscriptionTags.AddTag(RouteTag); } }
	void SetPersonaData(UNpcPersonaDataAsset* Persona) const { if (UAINpcComponent* NpcComponent = GetNpcComponent()) { NpcComponent->SetPersonaData(Persona); } }
	void SetVisibleStateText(const FString& Text) const { if (CharacterDriver) { CharacterDriver->SetVisibleStateText(Text); } }
	void SetVisibleDialogueText(const FString& Text) const { if (CharacterDriver) { CharacterDriver->SetVisibleDialogueText(Text); } }
	void BeginVisualActionMove(const FTransform& Transform, const FString& TargetId) const { if (CharacterDriver) { CharacterDriver->BeginVisualActionMove(Transform, TargetId); } }
	bool HasReachedVisualActionTarget() const { return CharacterDriver && CharacterDriver->HasReachedVisualActionTarget(); }
	double GetVisualActionTargetDistance() const { return CharacterDriver ? CharacterDriver->GetVisualActionTargetDistance() : 0.0; }
};

struct FAINpcDataDrivenVisualScenarioTest::FImplementation
{
	FImplementation(const FAINpcVisualTestContext& Context, FAINpcVisualScenarioConfig InConfig);
	~FImplementation();

	bool Start(FString& OutFailureReason);
	void Poll();
	bool IsComplete() const;
	bool HasFailed() const;
	const FString& GetFailureReason() const;
	FString BuildSummary() const;
	FAINpcVisualTestObservations BuildObservations() const;
	TArray<FAINpcVisualTestStepDiagnostic> BuildStepDiagnostics() const;

private:
	bool RequiresBuiltInNpcRuntime() const;
	bool StartDialogueObservation(FString& OutFailureReason);
	bool StartProjectAdapters(FString& OutFailureReason);
	bool ResolveProjectFixture(FString& OutFailureReason);
	void StartNextStep();
	void PollActiveStep();
	bool RunStep(const FAINpcVisualScenarioStep& Step, FString& OutFailureReason);
	bool RunDialogueStart(const FAINpcVisualScenarioStep& Step, FString& OutFailureReason);
	bool RunWorldEvent(const FAINpcVisualScenarioStep& Step, FString& OutFailureReason);
	bool RunActionExecuteLatestIntent(const FAINpcVisualScenarioStep& Step, FString& OutFailureReason);
	bool RunProjectActionExecute(const FAINpcVisualScenarioStep& Step, FString& OutFailureReason);
	bool RunObserveHold(const FAINpcVisualScenarioStep& Step, FString& OutFailureReason);
	bool EvaluateAssertion(const FAINpcVisualScenarioAssertion& Assertion, const FAINpcVisualObservationWindow& Window, FAINpcVisualAssertionFailureDetail* OutFailure) const;
	bool EvaluateFinalAssertion(FAINpcVisualAssertionFailureDetail* OutFailure);
	bool SampleProjectObservationForFinalAssertion(const FAINpcVisualScenarioAssertion& Assertion, FAINpcVisualAssertionFailureDetail* OutFailure);
	void RecordWindowReadinessForAssertion(const FAINpcVisualScenarioAssertion& Assertion, const FAINpcVisualObservationWindow& Window);
	bool IsWindowSampledObservation(const FString& Name) const;
	FAINpcVisualObservationSourceInfo MakeWindowSampledSource(const FString& Name) const;
	FAINpcVisualObservationWindow GetCurrentStepWindow() const;
	FAINpcVisualObservationWindow GetScenarioHistoryWindow() const;
	FString BuildPrompt(FString& OutFailureReason) const;
	void HandleTimeout();
	void UpdateDialogueStateEvidence();
	bool ConfigurePersona(FString& OutFailureReason);
	void Fail(const FString& Reason);
	void AddStartupDiagnostic(const FString& Stage, const FString& Reason, const FString& AdapterCategory = FString(), const FString& AdapterId = FString(), const FString& ActorClass = FString(), const FString& ActorTag = FString(), const FString& TargetRef = FString(), const FString& FieldName = FString(), const FString& Capability = FString(), const FString& ObservationName = FString(), const FString& OwnerModuleName = FString());
	void FailWithAssertion(const FString& Prefix, const FAINpcVisualAssertionFailureDetail& Failure);
	void ShowStatus(const FString& Message, const FColor& Color, float DurationSeconds) const;
	void RecordBoolObservation(const FString& Name, bool bValue, const FAINpcVisualObservationSourceInfo& SourceInfo, bool bRequiresAllowedFinalSource = false);
	void RecordIntegerObservation(const FString& Name, int32 Value, const FAINpcVisualObservationSourceInfo& SourceInfo);
	void RecordNumberObservation(const FString& Name, double Value, const FAINpcVisualObservationSourceInfo& SourceInfo);
	void RecordStringObservation(const FString& Name, const FString& Value, const FAINpcVisualObservationSourceInfo& SourceInfo);
	bool RecordObservation(FAINpcVisualObservationRecord Record, bool bRequiresAllowedFinalSource);
	void MarkObservationSourceReady(const FString& Name, const FAINpcVisualObservationSourceInfo& SourceInfo);
	void MarkObservationSourceReadyForWindow(const FString& Name, const FAINpcVisualObservationSourceInfo& SourceInfo, const FAINpcVisualObservationWindow& Window);
	void RefreshActionTargetObservation();
	void CompleteCurrentStep();
	void FinalizeActiveStepDiagnostic(const FString& Status, const FString& Reason, const FString& FailureCategory = FString(), const FString& ObservationName = FString(), const FString& SourceKind = FString(), const FString& SourceId = FString());
	FAINpcVisualObservationSourceInfo MakeDialogueSource(const TCHAR* CallbackName) const;
	FAINpcVisualObservationSourceInfo MakeDialogueStateSource(const TCHAR* SamplingName) const;
	FAINpcVisualObservationSourceInfo MakeEventSource() const;
	FAINpcVisualObservationSourceInfo MakeActionSource() const;
	FAINpcVisualObservationSourceInfo MakeCharacterSource(const TCHAR* SamplingName) const;

	TUniquePtr<FAINpcVisualScenarioRuntimeView> Runtime;
	FAINpcVisualScenarioConfig Config;
	TObjectPtr<UNpcPersonaDataAsset> VisualHarnessPersona;
	FTimerHandle StepTimerHandle;
	FTimerHandle TimeoutTimerHandle;
	TUniquePtr<FAINpcVisualObservationStore> Observations;
	TArray<FAINpcVisualTestStepDiagnostic> StepDiagnostics;
	int32 ActiveStepIndex = INDEX_NONE;
	double ActiveStepStartSeconds = 0.0;
	double CurrentStepFirstSatisfiedSeconds = -1.0;
	bool bComplete = false;
	bool bFailed = false;
	bool bStarted = false;
	FString FailureReason;
	FString LastActionFailureReason;
};

struct FAINpcBuiltInEventAdapter
{
		bool Execute(const FAINpcVisualScenarioStep& Step, const FAINpcVisualScenarioRuntimeView& Runtime, FString& OutFailureReason) const
		{
			if (!AINpcVisualInternalAdapters::RequireCapability(Step.Payload.AdapterId, AINpcVisualInternalAdapters::WorldEventEmitCapability(), OutFailureReason))
			{
				return false;
			}
			if (!Step.Payload.TargetRef.IsEmpty() && Step.Payload.TargetRef != FixtureNpcRef)
			{
				OutFailureReason = FString::Printf(TEXT("world.event targetRef '%s' is unsupported."), *Step.Payload.TargetRef);
				return false;
			}
			UWorld* World = Runtime.GetWorld();
			UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
			UNpcEventSubsystem* EventSubsystem = GameInstance ? GameInstance->GetSubsystem<UNpcEventSubsystem>() : nullptr;
			if (!EventSubsystem)
			{
				OutFailureReason = TEXT("world.event could not find UNpcEventSubsystem.");
				return false;
			}
			const FString EventRoute = Step.Payload.EventTag.IsEmpty() ? Step.Payload.EventId : Step.Payload.EventTag;
			const FGameplayTag EventTag = FGameplayTag::RequestGameplayTag(FName(*EventRoute), false);
			if (!EventTag.IsValid())
			{
				OutFailureReason = FString::Printf(TEXT("world.event requires gameplay tag '%s'."), *EventRoute);
				return false;
			}
			FGameplayTagContainer RoutingTags;
			RoutingTags.AddTag(EventTag);
			const FNpcEventMessage EventMessage = UNpcEventPayloadBlueprintLibrary::MakeGiftEventMessage(EventTag, RoutingTags, nullptr, Runtime.GetNpcActor(), FGameplayTag(), 1);
			EventSubsystem->BroadcastEvent(EventMessage);
			return true;
		}
	};

	enum class EAINpcActionAdapterExecuteResult
	{
		Accepted,
		Rejected,
		Invalid
	};

struct FAINpcBuiltInActionAdapter
{
		EAINpcActionAdapterExecuteResult Execute(const FAINpcVisualScenarioStep& Step, const FAINpcVisualScenarioRuntimeView& Runtime, FAINpcVisualSmartObjectActionExecution& OutExecution, FString& OutFailureReason) const
		{
			if (!AINpcVisualInternalAdapters::RequireCapability(Step.Payload.AdapterId, AINpcVisualInternalAdapters::ExecuteLatestIntentCapability(), OutFailureReason))
			{
				return EAINpcActionAdapterExecuteResult::Invalid;
			}
			if (Step.Payload.ActorRef != FixtureNpcRef)
			{
				OutFailureReason = FString::Printf(TEXT("action.executeLatestIntent actorRef '%s' is unsupported."), *Step.Payload.ActorRef);
				return EAINpcActionAdapterExecuteResult::Invalid;
			}
			if (!Runtime.HasSmartObjectFixture())
			{
				OutFailureReason = TEXT("action.executeLatestIntent requires a SmartObject fixture.");
				return EAINpcActionAdapterExecuteResult::Invalid;
			}
			AActor* NpcActor = Runtime.GetNpcActor();
			if (!NpcActor)
			{
				OutFailureReason = TEXT("action.executeLatestIntent NPC actor is null.");
				return EAINpcActionAdapterExecuteResult::Invalid;
			}
			return AINpcSmartObjectActionTestSupport::TryExecuteLatestActionIntent(Runtime.GetNpcComponent(), *NpcActor, SmartObjectSearchRadius, SmartObjectClaimPriority, OutExecution, OutFailureReason)
				? EAINpcActionAdapterExecuteResult::Accepted
				: EAINpcActionAdapterExecuteResult::Rejected;
		}
	};

FAINpcDataDrivenVisualScenarioTest::FAINpcDataDrivenVisualScenarioTest(const FAINpcVisualTestContext& Context, FAINpcVisualScenarioConfig InConfig)
	: Impl(MakeUnique<FImplementation>(Context, MoveTemp(InConfig)))
{
}

FAINpcDataDrivenVisualScenarioTest::~FAINpcDataDrivenVisualScenarioTest() = default;

bool FAINpcDataDrivenVisualScenarioTest::Start(FString& OutFailureReason) { return Impl->Start(OutFailureReason); }
void FAINpcDataDrivenVisualScenarioTest::Poll() { Impl->Poll(); }
bool FAINpcDataDrivenVisualScenarioTest::IsComplete() const { return Impl->IsComplete(); }
bool FAINpcDataDrivenVisualScenarioTest::HasFailed() const { return Impl->HasFailed(); }
const FString& FAINpcDataDrivenVisualScenarioTest::GetFailureReason() const { return Impl->GetFailureReason(); }
FString FAINpcDataDrivenVisualScenarioTest::BuildSummary() const { return Impl->BuildSummary(); }
FAINpcVisualTestObservations FAINpcDataDrivenVisualScenarioTest::BuildObservations() const { return Impl->BuildObservations(); }
TArray<FAINpcVisualTestStepDiagnostic> FAINpcDataDrivenVisualScenarioTest::BuildStepDiagnostics() const { return Impl->BuildStepDiagnostics(); }

FAINpcDataDrivenVisualScenarioTest::FImplementation::FImplementation(const FAINpcVisualTestContext& Context, FAINpcVisualScenarioConfig InConfig)
	: Runtime(MakeUnique<FAINpcVisualScenarioRuntimeView>())
	, Config(MoveTemp(InConfig))
	, Observations(MakeUnique<FAINpcVisualObservationStore>())
{
	Runtime->World = Context.World;
	Runtime->TestId = Context.TestId;
	Runtime->RunId = Context.RunId;
	Runtime->SmartObject = Context.Fixture.SmartObject;
	if (Context.Fixture.Npc)
	{
		Runtime->CharacterDriver = MakeUnique<FAINpcBuiltInCharacterDriver>(*Context.Fixture.Npc);
	}
}

FAINpcDataDrivenVisualScenarioTest::FImplementation::~FImplementation()
{
	if (UWorld* World = Runtime->GetWorld())
	{
		World->GetTimerManager().ClearTimer(StepTimerHandle);
		World->GetTimerManager().ClearTimer(TimeoutTimerHandle);
	}
	if (Runtime->DialogueObservationProvider) { Runtime->DialogueObservationProvider->Stop(); }
}

bool FAINpcDataDrivenVisualScenarioTest::FImplementation::Start(FString& OutFailureReason)
{
	if (!AINpcVisualInternalAdapters::ValidateBuiltInCatalog(OutFailureReason))
	{
		return false;
	}
	if (!StartProjectAdapters(OutFailureReason)) { return false; }
	if (!ResolveProjectFixture(OutFailureReason)) { return false; }
	const bool bRequiresBuiltInNpcRuntime = RequiresBuiltInNpcRuntime();
	if (bRequiresBuiltInNpcRuntime)
	{
		if (!Runtime->GetNpcComponent())
		{
			OutFailureReason = FString::Printf(TEXT("Visual scenario '%s' cannot start because NPC component is null."), *Config.TestId);
			return false;
		}
		if (!ConfigurePersona(OutFailureReason)) { return false; }
		if (!AINpcDialogueVisualTestSupport::ValidateProviderConfiguration(Runtime->GetNpcComponent(), OutFailureReason)) { return false; }
		if (!StartDialogueObservation(OutFailureReason)) { return false; }
	}
	bStarted = true;
	if (UWorld* World = Runtime->GetWorld())
	{
		const float StepStartDelaySeconds = bRequiresBuiltInNpcRuntime ? InitialDialogueDelaySeconds : 0.0f;
		ShowStatus(FString::Printf(TEXT("Visual scenario '%s' ready. Starting DSL steps in %.0fs."), *Config.TestId, StepStartDelaySeconds), FColor::Green, 15.0f);
		World->GetTimerManager().SetTimer(TimeoutTimerHandle, FTimerDelegate::CreateRaw(this, &FAINpcDataDrivenVisualScenarioTest::FImplementation::HandleTimeout), static_cast<float>(Config.TimeoutSec), false);
		if (bRequiresBuiltInNpcRuntime)
		{
			World->GetTimerManager().SetTimer(StepTimerHandle, FTimerDelegate::CreateRaw(this, &FAINpcDataDrivenVisualScenarioTest::FImplementation::StartNextStep), StepStartDelaySeconds, false);
		}
		else
		{
			StartNextStep();
		}
		return true;
	}
	OutFailureReason = FString::Printf(TEXT("Visual scenario '%s' cannot start because World is null."), *Config.TestId);
	return false;
}

bool FAINpcDataDrivenVisualScenarioTest::FImplementation::RequiresBuiltInNpcRuntime() const
{
	if (Config.Fixture.Kind != ProjectFixtureKind)
	{
		return true;
	}
	for (const FAINpcVisualScenarioStep& Step : Config.Steps)
	{
		if (Step.Type == TEXT("dialogue.start")
			|| Step.Type == TEXT("world.event")
			|| Step.Type == TEXT("action.executeLatestIntent")
			|| Step.Type == TEXT("observe.hold"))
		{
			return true;
		}
	}
	return false;
}

bool FAINpcDataDrivenVisualScenarioTest::FImplementation::StartProjectAdapters(FString& OutFailureReason)
{
	using namespace AINpc::Visual::TestInternal;

	FAINpcVisualAdapterCreateContext AdapterContext;
	AdapterContext.World = Runtime->World;
	AdapterContext.TestId = Config.TestId;
	AdapterContext.RunId = Runtime->RunId;
	AdapterContext.StoryIds = Config.StoryIds;
	AdapterContext.PhaseIds = Config.PhaseIds;
	FAINpcVisualAdapterDiagnosticSink DiagnosticSink;
	AdapterContext.DiagnosticSink = &DiagnosticSink;
	const FAdapterRunViewCreateResult CreateResult = FAdapterRunView::Create(AdapterContext, Runtime->AdapterRunView);
	if (!CreateResult.IsSuccess())
	{
		AddStartupDiagnostic(TEXT("RuntimeStartup"), CreateResult.Diagnostic);
		OutFailureReason = CreateResult.Diagnostic;
		return false;
	}
	return true;
}

bool FAINpcDataDrivenVisualScenarioTest::FImplementation::ResolveProjectFixture(FString& OutFailureReason)
{
	if (Config.Fixture.Kind != ProjectFixtureKind)
	{
		return true;
	}
	if (!Runtime->AdapterRunView.IsValid())
	{
		OutFailureReason = FString::Printf(TEXT("stage=RuntimeStartup testId=%s adapter=%s reason=adapter run view unavailable before fixture resolution"), *Config.TestId, *Config.Fixture.AdapterId);
		AddStartupDiagnostic(TEXT("RuntimeStartup"), OutFailureReason, TEXT("FixtureResolver"), Config.Fixture.AdapterId, Config.Fixture.ActorClass, Config.Fixture.ActorTag, FixtureActorRef, TEXT("adapterId"), ExistingActorCapability);
		return false;
	}

	UClass* ResolvedClass = FindObject<UClass>(nullptr, *Config.Fixture.ActorClass);
	if (ResolvedClass == nullptr)
	{
		OutFailureReason = FString::Printf(TEXT("stage=RuntimeStartup testId=%s adapter=%s actorClass=%s actorTag=%s reason=native class is not loaded"),
			*Config.TestId, *Config.Fixture.AdapterId, *Config.Fixture.ActorClass, *Config.Fixture.ActorTag);
		AddStartupDiagnostic(TEXT("RuntimeStartup"), OutFailureReason, TEXT("FixtureResolver"), Config.Fixture.AdapterId, Config.Fixture.ActorClass, Config.Fixture.ActorTag, FixtureActorRef, TEXT("actorClass"), ExistingActorCapability);
		return false;
	}

	TArray<AActor*> Matches;
	for (TActorIterator<AActor> It(Runtime->World); It; ++It)
	{
		AActor* Actor = *It;
		if (IsValid(Actor) && Actor->GetClass() == ResolvedClass && Actor->ActorHasTag(FName(*Config.Fixture.ActorTag)))
		{
			Matches.Add(Actor);
		}
	}
	if (Matches.Num() != 1)
	{
		OutFailureReason = FString::Printf(TEXT("stage=RuntimeStartup testId=%s adapter=%s actorClass=%s actorTag=%s reason=expected exactly one matching actor but found %d"),
			*Config.TestId, *Config.Fixture.AdapterId, *Config.Fixture.ActorClass, *Config.Fixture.ActorTag, Matches.Num());
		AddStartupDiagnostic(TEXT("RuntimeStartup"), OutFailureReason, TEXT("FixtureResolver"), Config.Fixture.AdapterId, Config.Fixture.ActorClass, Config.Fixture.ActorTag, FixtureActorRef, TEXT("actorTag"), ExistingActorCapability);
		return false;
	}

	using namespace AINpc::Visual::TestInternal;
	const FAdapterViewLookupResult Lookup = Runtime->AdapterRunView->FindAdapter(EAINpcVisualAdapterCategory::FixtureResolver, FName(*Config.Fixture.AdapterId), TEXT("RuntimeStartup"));
	FString LookupDiagnostic;
	FAINpcVisualFixtureResolveResult ResolveResult;
	const bool bUsedAdapter = Lookup.UseAdapter([this, &Matches, &ResolveResult](IAINpcVisualAdapterInstance& Instance)
	{
		FAINpcVisualFixtureResolveRequest Request;
		Request.World = Runtime->World;
		Request.TestId = Config.TestId;
		Request.RunId = Runtime->RunId;
		Request.AdapterId = FName(*Config.Fixture.AdapterId);
		Request.FixtureKind = Config.Fixture.Kind;
		Request.ActorClass = Config.Fixture.ActorClass;
		Request.ActorTag = Config.Fixture.ActorTag;
		Request.TargetRef = FixtureActorRef;
		ResolveResult = static_cast<IAINpcVisualFixtureResolverAdapter&>(Instance).ResolveFixture(Request);
		if (ResolveResult.TargetRef.IsEmpty())
		{
			ResolveResult.TargetRef = FixtureActorRef;
		}
	}, &LookupDiagnostic);
	if (!bUsedAdapter)
	{
		OutFailureReason = LookupDiagnostic;
		AddStartupDiagnostic(TEXT("RuntimeStartup"), OutFailureReason, TEXT("FixtureResolver"), Config.Fixture.AdapterId, Config.Fixture.ActorClass, Config.Fixture.ActorTag, FixtureActorRef, TEXT("adapterId"), ExistingActorCapability);
		return false;
	}
	if (!ResolveResult.bSuccess || ResolveResult.TargetRef != FixtureActorRef || !ResolveResult.Actor.IsValid() || ResolveResult.Actor.Get() != Matches[0])
	{
		OutFailureReason = ResolveResult.Diagnostic.IsEmpty()
			? FString::Printf(TEXT("stage=RuntimeStartup testId=%s adapter=%s targetRef=%s reason=fixture resolver did not return the exact class/tag actor"), *Config.TestId, *Config.Fixture.AdapterId, *ResolveResult.TargetRef)
			: ResolveResult.Diagnostic;
		AddStartupDiagnostic(TEXT("RuntimeStartup"), OutFailureReason, TEXT("FixtureResolver"), Config.Fixture.AdapterId, Config.Fixture.ActorClass, Config.Fixture.ActorTag, FixtureActorRef, TEXT("targetRef"), ExistingActorCapability);
		return false;
	}
	Runtime->ProjectFixtureActor = ResolveResult.Actor;
	return true;
}

bool FAINpcDataDrivenVisualScenarioTest::FImplementation::StartDialogueObservation(FString& OutFailureReason)
{
	UAINpcComponent* NpcComponent = Runtime->GetNpcComponent();
	if (!NpcComponent)
	{
		OutFailureReason = FString::Printf(TEXT("Scenario '%s' cannot observe dialogue because NPC component is null."), *Config.TestId);
		return false;
	}
	Runtime->DialogueObservationProvider = MakeUnique<FAINpcInternalDialogueObservationProvider>();
	Runtime->DialogueObservationProvider->RecordBool = [this](const FString& Name, const bool bValue, const FAINpcVisualObservationSourceInfo& SourceInfo, const bool bRequiresAllowedFinalSource) { RecordBoolObservation(Name, bValue, SourceInfo, bRequiresAllowedFinalSource); };
	Runtime->DialogueObservationProvider->RecordInteger = [this](const FString& Name, const int32 Value, const FAINpcVisualObservationSourceInfo& SourceInfo) { RecordIntegerObservation(Name, Value, SourceInfo); };
	Runtime->DialogueObservationProvider->MarkReady = [this](const FString& Name, const FAINpcVisualObservationSourceInfo& SourceInfo) { MarkObservationSourceReady(Name, SourceInfo); };
	Runtime->DialogueObservationProvider->HasBool = [this](const FString& Name) { return Observations->HasTrueBooleanSummary(Name); };
	Runtime->DialogueObservationProvider->SetVisibleDialogueText = [this](const FString& Text) { Runtime->SetVisibleDialogueText(Text); };
	Runtime->DialogueObservationProvider->UpdateDialogueStateEvidence = [this]() { UpdateDialogueStateEvidence(); };
	Runtime->DialogueObservationProvider->HasLatestActionIntent = [this]() { FNpcAction LatestAction; return Runtime->TryGetLatestActionIntent(LatestAction); };
	Runtime->DialogueObservationProvider->Fail = [this](const FString& Reason) { Fail(Reason); };
	Runtime->DialogueObservationProvider->ShowStatus = [this](const FString& Message, const FColor& Color, const float DurationSeconds) { ShowStatus(Message, Color, DurationSeconds); };
	Runtime->DialogueObservationProvider->MakeDialogueSource = [this](const TCHAR* CallbackName) { return MakeDialogueSource(CallbackName); };
	return Runtime->DialogueObservationProvider->Start(*NpcComponent, OutFailureReason);
}

void FAINpcDataDrivenVisualScenarioTest::FImplementation::Poll()
{
	if (!bStarted || bComplete || bFailed) { return; }
	UpdateDialogueStateEvidence();
	RefreshActionTargetObservation();
	PollActiveStep();
}

bool FAINpcDataDrivenVisualScenarioTest::FImplementation::IsComplete() const { return bComplete; }
bool FAINpcDataDrivenVisualScenarioTest::FImplementation::HasFailed() const { return bFailed; }
const FString& FAINpcDataDrivenVisualScenarioTest::FImplementation::GetFailureReason() const { return FailureReason; }

FString FAINpcDataDrivenVisualScenarioTest::FImplementation::BuildSummary() const
{
	FAINpcVisualAssertionFailureDetail Failure;
	const bool bExpectSatisfied = Observations->EvaluateAssertion(Config.Expect.Assertion, GetScenarioHistoryWindow(), &Failure);
	return FString::Printf(TEXT("TestId=%s Step=%d/%d Expect=%s"), *Config.TestId, ActiveStepIndex + 1, Config.Steps.Num(), bExpectSatisfied ? TEXT("true") : TEXT("false"));
}

FAINpcVisualTestObservations FAINpcDataDrivenVisualScenarioTest::FImplementation::BuildObservations() const
{
	return Observations->BuildObservations();
}

TArray<FAINpcVisualTestStepDiagnostic> FAINpcDataDrivenVisualScenarioTest::FImplementation::BuildStepDiagnostics() const
{
	return StepDiagnostics;
}

void FAINpcInternalDialogueObservationProvider::OnSessionStarted()
{
	const auto Source = MakeDialogueSource(TEXT("OnDialogueSessionStartedNative"));
	MarkReady(TEXT("sessionStarted"), Source);
	RecordBool(TEXT("sessionStarted"), true, Source, false);
	ShowStatus(TEXT("Dialogue session started through the real provider chain."), FColor::Green, 6.0f);
}

void FAINpcInternalDialogueObservationProvider::OnResponse(const FString& Text)
{
	LastNpcResponseText = Text;
	LastNpcResponseText.TrimStartAndEndInline();
	if (LastNpcResponseText.IsEmpty())
	{
		Fail(TEXT("Provider returned an empty dialogue response."));
		return;
	}
	const auto Source = MakeDialogueSource(TEXT("OnDialogueResponseNative"));
	MarkReady(TEXT("dialogueResponseObserved"), Source);
	MarkReady(TEXT("structuredResponseObserved"), Source);
	RecordBool(TEXT("dialogueResponseObserved"), true, Source, true);
	RecordBool(TEXT("structuredResponseObserved"), true, Source, true);
	RecordInteger(TEXT("responseLength"), LastNpcResponseText.Len(), Source);
	if (HasLatestActionIntent())
	{
		MarkReady(TEXT("actionIntentObserved"), Source);
		RecordBool(TEXT("actionIntentObserved"), true, Source, false);
	}
	SetVisibleDialogueText(LastNpcResponseText);
	UpdateDialogueStateEvidence();
	ShowStatus(FString::Printf(TEXT("<<< NPC response received. Length=%d"), LastNpcResponseText.Len()), FColor::Yellow, 8.0f);
}

void FAINpcInternalDialogueObservationProvider::OnPartialResponse(const FString& Text)
{
	const auto Source = MakeDialogueSource(TEXT("OnDialoguePartialResponseNative"));
	MarkReady(TEXT("partialResponseObserved"), Source);
	if (!Text.IsEmpty())
	{
		LastPartialResponseText = Text;
		RecordBool(TEXT("partialResponseObserved"), true, Source, true);
		RecordInteger(TEXT("partialResponseLength"), Text.Len(), Source);
	}
	ShowStatus(FString::Printf(TEXT("[stream] partial response. Length=%d"), Text.Len()), FColor::Orange, 3.0f);
}

void FAINpcInternalDialogueObservationProvider::OnError(const FString& ErrorMessage)
{
	Fail(FString::Printf(TEXT("Provider chain reported an error. Length=%d"), ErrorMessage.Len()));
}

void FAINpcInternalDialogueObservationProvider::OnSessionEnded()
{
	UpdateDialogueStateEvidence();
}

void FAINpcInternalDialogueObservationProvider::OnDegraded(const FString& FallbackResponse, const FString& DegradedFailureReason)
{
	(void)FallbackResponse;
	Fail(FString::Printf(TEXT("Provider chain degraded. ReasonLength=%d"), DegradedFailureReason.Len()));
}

void FAINpcInternalDialogueObservationProvider::OnDelayMaskingStart(UAnimMontage* Montage, const FText& FillerText)
{
	(void)Montage;
	LastDelayFillerText = FillerText.ToString();
	const auto Source = MakeDialogueSource(TEXT("OnDelayMaskingStartNative"));
	MarkReady(TEXT("delayMaskingStartObserved"), Source);
	RecordBool(TEXT("delayMaskingStartObserved"), true, Source, false);
	RecordInteger(TEXT("delayFillerLength"), LastDelayFillerText.Len(), Source);
	if (HasBool(TEXT("eventTriggerBroadcast")))
	{
		MarkReady(TEXT("eventDelayMaskingStartObserved"), Source);
		RecordBool(TEXT("eventDelayMaskingStartObserved"), true, Source, false);
	}
	ShowStatus(FString::Printf(TEXT("Delay masking started. FillerLength=%d"), LastDelayFillerText.Len()), FColor::Purple, 5.0f);
}

void FAINpcInternalDialogueObservationProvider::OnDelayMaskingEnd()
{
	const auto Source = MakeDialogueSource(TEXT("OnDelayMaskingEndNative"));
	MarkReady(TEXT("delayMaskingEndObserved"), Source);
	RecordBool(TEXT("delayMaskingEndObserved"), true, Source, true);
	ShowStatus(TEXT("Delay masking ended."), FColor::Purple, 3.0f);
}

void FAINpcDataDrivenVisualScenarioTest::FImplementation::StartNextStep()
{
	if (bComplete || bFailed) { return; }
	CurrentStepFirstSatisfiedSeconds = -1.0;
	++ActiveStepIndex;
	if (!Config.Steps.IsValidIndex(ActiveStepIndex))
	{
		FAINpcVisualAssertionFailureDetail Failure;
		if (EvaluateFinalAssertion(&Failure))
		{
			bComplete = true;
		}
		else
		{
			FailWithAssertion(FString::Printf(TEXT("Scenario '%s' completed steps but final expect assertion failed."), *Config.TestId), Failure);
		}
		return;
	}
	ActiveStepStartSeconds = Runtime->GetTimeSeconds(0.0);
	FAINpcVisualTestStepDiagnostic Diagnostic;
	Diagnostic.TestId = Config.TestId;
	Diagnostic.StepIndex = ActiveStepIndex;
	Diagnostic.StepType = Config.Steps[ActiveStepIndex].Type;
	Diagnostic.Status = TEXT("running");
	if (Diagnostic.StepType == TEXT("project.action.execute"))
	{
		Diagnostic.Stage = TEXT("StepExecution");
		Diagnostic.AdapterCategory = TEXT("ActionAdapter");
		Diagnostic.AdapterId = Config.Steps[ActiveStepIndex].Payload.AdapterId;
		Diagnostic.TargetRef = Config.Steps[ActiveStepIndex].Payload.TargetRef;
		Diagnostic.ActionName = Config.Steps[ActiveStepIndex].Payload.ActionName;
	}
	StepDiagnostics.Add(MoveTemp(Diagnostic));
	FString StepFailureReason;
	if (!RunStep(Config.Steps[ActiveStepIndex], StepFailureReason))
	{
		Fail(StepFailureReason);
	}
}

void FAINpcDataDrivenVisualScenarioTest::FImplementation::PollActiveStep()
{
	if (!Config.Steps.IsValidIndex(ActiveStepIndex)) { return; }
	const FAINpcVisualScenarioStep& Step = Config.Steps[ActiveStepIndex];
	const FAINpcVisualObservationWindow Window = GetCurrentStepWindow();
	if (Step.Type == TEXT("wait.until"))
	{
		RecordWindowReadinessForAssertion(Step.Condition, Window);
		FAINpcVisualAssertionFailureDetail Failure;
		if (EvaluateAssertion(Step.Condition, Window, &Failure))
		{
			CompleteCurrentStep();
			return;
		}
		const double Now = Runtime->GetTimeSeconds(ActiveStepStartSeconds);
		if (Now - ActiveStepStartSeconds > Step.Payload.TimeoutSec)
		{
			FailWithAssertion(FString::Printf(TEXT("Scenario '%s' step[%d] wait.until timed out after %.1fs."), *Config.TestId, ActiveStepIndex, Step.Payload.TimeoutSec), Failure);
		}
	}
	else if (Step.Type == TEXT("observe.hold"))
	{
		FAINpcVisualScenarioAssertion HoldAssertion;
		HoldAssertion.Operator = TEXT("equals");
		HoldAssertion.Observation = Step.Payload.Observation;
		HoldAssertion.bHasEqualsBool = true;
		HoldAssertion.EqualsBool = true;
		RecordWindowReadinessForAssertion(HoldAssertion, Window);
		FAINpcVisualAssertionFailureDetail Failure;
		const double Now = Runtime->GetTimeSeconds(ActiveStepStartSeconds);
		if (!EvaluateAssertion(HoldAssertion, Window, &Failure))
		{
			CurrentStepFirstSatisfiedSeconds = -1.0;
			return;
		}
		if (CurrentStepFirstSatisfiedSeconds < 0.0)
		{
			CurrentStepFirstSatisfiedSeconds = Now;
			return;
		}
		if (Now - CurrentStepFirstSatisfiedSeconds >= Step.Payload.DurationSec)
		{
			MarkObservationSourceReady(TEXT("actionTargetReachedHoldElapsed"), MakeCharacterSource(TEXT("holdWindow")));
			RecordBoolObservation(TEXT("actionTargetReachedHoldElapsed"), true, MakeCharacterSource(TEXT("holdWindow")), false);
			CompleteCurrentStep();
		}
	}
}

bool FAINpcDataDrivenVisualScenarioTest::FImplementation::RunStep(const FAINpcVisualScenarioStep& Step, FString& OutFailureReason)
{
	if (Step.Type == TEXT("dialogue.start")) { return RunDialogueStart(Step, OutFailureReason); }
	if (Step.Type == TEXT("world.event")) { return RunWorldEvent(Step, OutFailureReason); }
	if (Step.Type == TEXT("action.executeLatestIntent")) { return RunActionExecuteLatestIntent(Step, OutFailureReason); }
	if (Step.Type == TEXT("project.action.execute")) { return RunProjectActionExecute(Step, OutFailureReason); }
	if (Step.Type == TEXT("observe.hold")) { return RunObserveHold(Step, OutFailureReason); }
	if (Step.Type == TEXT("wait.until")) { return true; }
	OutFailureReason = FString::Printf(TEXT("Scenario '%s' step[%d] has unsupported type '%s'."), *Config.TestId, ActiveStepIndex, *Step.Type);
	return false;
}

bool FAINpcDataDrivenVisualScenarioTest::FImplementation::RunProjectActionExecute(const FAINpcVisualScenarioStep& Step, FString& OutFailureReason)
{
	if (!Runtime->AdapterRunView.IsValid())
	{
		OutFailureReason = FString::Printf(TEXT("stage=StepExecution testId=%s stepIndex=%d adapter=%s actionName=%s targetRef=%s reason=adapter run view unavailable"), *Config.TestId, ActiveStepIndex, *Step.Payload.AdapterId, *Step.Payload.ActionName, *Step.Payload.TargetRef);
		return false;
	}
	AActor* TargetActor = Runtime->GetProjectFixtureActor();
	if (Step.Payload.TargetRef != FixtureActorRef || !IsValid(TargetActor))
	{
		OutFailureReason = FString::Printf(TEXT("stage=StepExecution testId=%s stepIndex=%d adapter=%s actionName=%s targetRef=%s reason=fixture.actor is not bound"), *Config.TestId, ActiveStepIndex, *Step.Payload.AdapterId, *Step.Payload.ActionName, *Step.Payload.TargetRef);
		return false;
	}

	using namespace AINpc::Visual::TestInternal;
	const FAdapterViewLookupResult Lookup = Runtime->AdapterRunView->FindAdapter(EAINpcVisualAdapterCategory::ActionAdapter, FName(*Step.Payload.AdapterId), TEXT("StepExecution"));
	FString LookupDiagnostic;
	FAINpcVisualActionExecuteResult ExecuteResult;
	const bool bUsedAdapter = Lookup.UseAdapter([this, &Step, TargetActor, &ExecuteResult](IAINpcVisualAdapterInstance& Instance)
	{
		FAINpcVisualActionExecuteRequest Request;
		Request.TestId = Config.TestId;
		Request.RunId = Runtime->RunId;
		Request.StepIndex = ActiveStepIndex;
		Request.AdapterId = FName(*Step.Payload.AdapterId);
		Request.ActionName = Step.Payload.ActionName;
		Request.TargetRef = Step.Payload.TargetRef;
		Request.TargetActor = TargetActor;
		ExecuteResult = static_cast<IAINpcVisualActionAdapter&>(Instance).ExecuteAction(Request);
	}, &LookupDiagnostic);
	if (!bUsedAdapter)
	{
		OutFailureReason = LookupDiagnostic;
		return false;
	}
	if (!ExecuteResult.bAccepted || !ExecuteResult.bSucceeded)
	{
		OutFailureReason = !ExecuteResult.FailureReason.IsEmpty() ? ExecuteResult.FailureReason : ExecuteResult.Diagnostic;
		if (OutFailureReason.IsEmpty())
		{
			OutFailureReason = FString::Printf(TEXT("stage=StepExecution testId=%s stepIndex=%d adapter=%s actionName=%s targetRef=%s reason=project action adapter rejected action"), *Config.TestId, ActiveStepIndex, *Step.Payload.AdapterId, *Step.Payload.ActionName, *Step.Payload.TargetRef);
		}
		return false;
	}
	CompleteCurrentStep();
	return true;
}

bool FAINpcDataDrivenVisualScenarioTest::FImplementation::RunDialogueStart(const FAINpcVisualScenarioStep& Step, FString& OutFailureReason)
{
	(void)Step;
	FString Prompt = BuildPrompt(OutFailureReason);
	if (Prompt.IsEmpty()) { return false; }
	ShowStatus(FString::Printf(TEXT(">>> %s"), *Prompt.Left(220)), FColor::Cyan, 10.0f);
	if (!Runtime->GetNpcComponent()->StartDialogue(Prompt))
	{
		OutFailureReason = FString::Printf(TEXT("Scenario '%s' step[%d] StartDialogue rejected. %s"), *Config.TestId, ActiveStepIndex, *AINpcDialogueVisualTestSupport::DescribeDialogueState(Runtime->GetNpcComponent()));
		return false;
	}
	UpdateDialogueStateEvidence();
	CompleteCurrentStep();
	return true;
}

bool FAINpcDataDrivenVisualScenarioTest::FImplementation::RunWorldEvent(const FAINpcVisualScenarioStep& Step, FString& OutFailureReason)
{
	FAINpcBuiltInEventAdapter Adapter;
	if (!Adapter.Execute(Step, *Runtime, OutFailureReason)) { return false; }
	const FAINpcVisualObservationSourceInfo EventSource = MakeEventSource();
	MarkObservationSourceReady(TEXT("eventTriggerBroadcast"), EventSource);
	RecordBoolObservation(TEXT("eventTriggerBroadcast"), true, EventSource, false);
	ShowStatus(TEXT("Gameplay event trigger broadcast while provider request is in flight."), FColor::Orange, 7.0f);
	CompleteCurrentStep();
	return true;
}

bool FAINpcDataDrivenVisualScenarioTest::FImplementation::RunActionExecuteLatestIntent(const FAINpcVisualScenarioStep& Step, FString& OutFailureReason)
{
	FAINpcBuiltInActionAdapter Adapter;
	FAINpcVisualSmartObjectActionExecution Execution;
	FString ActionFailureReason;
	const EAINpcActionAdapterExecuteResult ExecuteResult = Adapter.Execute(Step, *Runtime, Execution, ActionFailureReason);
	if (ExecuteResult == EAINpcActionAdapterExecuteResult::Accepted)
	{
		MarkObservationSourceReady(TEXT("actionExecutionAccepted"), MakeActionSource());
		RecordBoolObservation(TEXT("actionExecutionAccepted"), true, MakeActionSource(), false);
		Runtime->SetSmartObjectInteractionState(true);
		Runtime->BeginVisualActionMove(Execution.ClaimedSlotTransform, Execution.RequestedTarget);
		ShowStatus(FString::Printf(TEXT("SmartObject action accepted: %s -> %s."), *Execution.ActionType, *Execution.RequestedTarget), FColor::Green, 8.0f);
		CompleteCurrentStep();
		return true;
	}
	LastActionFailureReason = ActionFailureReason;
	RecordStringObservation(TEXT("lastActionFailure"), LastActionFailureReason, MakeActionSource());
	if (ExecuteResult == EAINpcActionAdapterExecuteResult::Rejected && Step.Payload.bAllowActionRejection)
	{
		MarkObservationSourceReady(TEXT("actionRejectedVisible"), MakeActionSource());
		RecordBoolObservation(TEXT("actionRejectedVisible"), true, MakeActionSource(), false);
		Runtime->SetSmartObjectInteractionState(false);
		ShowStatus(FString::Printf(TEXT("SmartObject action rejected (allowed): %s"), *ActionFailureReason), FColor::Orange, 8.0f);
		CompleteCurrentStep();
		return true;
	}
	OutFailureReason = FString::Printf(TEXT("Scenario '%s' step[%d] required SmartObject action failed: %s"), *Config.TestId, ActiveStepIndex, *ActionFailureReason);
	return false;
}

bool FAINpcDataDrivenVisualScenarioTest::FImplementation::RunObserveHold(const FAINpcVisualScenarioStep& Step, FString& OutFailureReason)
{
	if (!IsKnownBooleanObservation(Step.Payload.Observation))
	{
		OutFailureReason = FString::Printf(TEXT("Scenario '%s' step[%d] observe.hold references unknown observation '%s'."), *Config.TestId, ActiveStepIndex, *Step.Payload.Observation);
		return false;
	}
	return true;
}

void FAINpcDataDrivenVisualScenarioTest::FImplementation::RecordWindowReadinessForAssertion(const FAINpcVisualScenarioAssertion& Assertion, const FAINpcVisualObservationWindow& Window)
{
	if (Assertion.Operator == TEXT("all") || Assertion.Operator == TEXT("any") || Assertion.Operator == TEXT("anyOf"))
	{
		for (const FAINpcVisualScenarioAssertion& Child : Assertion.Children)
		{
			RecordWindowReadinessForAssertion(Child, FAINpcVisualObservationStore::ApplyAssertionScope(Child, Window));
		}
		return;
	}
	if (!IsWindowSampledObservation(Assertion.Observation)) { return; }
	MarkObservationSourceReadyForWindow(Assertion.Observation, MakeWindowSampledSource(Assertion.Observation), Window);
}

bool FAINpcDataDrivenVisualScenarioTest::FImplementation::IsWindowSampledObservation(const FString& Name) const
{
	return Name == TEXT("waitingStateObserved") || Name == TEXT("speakingStateObserved") || Name == TEXT("actionTargetReached");
}

FAINpcVisualObservationSourceInfo FAINpcDataDrivenVisualScenarioTest::FImplementation::MakeWindowSampledSource(const FString& Name) const
{
	if (Name == TEXT("waitingStateObserved") || Name == TEXT("speakingStateObserved"))
	{
		return MakeDialogueStateSource(TEXT("GetDialogueState"));
	}
	return MakeCharacterSource(TEXT("poll"));
}

bool FAINpcDataDrivenVisualScenarioTest::FImplementation::EvaluateAssertion(const FAINpcVisualScenarioAssertion& Assertion, const FAINpcVisualObservationWindow& Window, FAINpcVisualAssertionFailureDetail* OutFailure) const
{
	return Observations->EvaluateAssertion(Assertion, Window, OutFailure);
}

bool FAINpcDataDrivenVisualScenarioTest::FImplementation::EvaluateFinalAssertion(FAINpcVisualAssertionFailureDetail* OutFailure)
{
	if (!SampleProjectObservationForFinalAssertion(Config.Expect.Assertion, OutFailure))
	{
		return false;
	}
	return EvaluateAssertion(Config.Expect.Assertion, GetScenarioHistoryWindow(), OutFailure);
}

bool FAINpcDataDrivenVisualScenarioTest::FImplementation::SampleProjectObservationForFinalAssertion(const FAINpcVisualScenarioAssertion& Assertion, FAINpcVisualAssertionFailureDetail* OutFailure)
{
	if (Assertion.Operator == TEXT("all") || Assertion.Operator == TEXT("any") || Assertion.Operator == TEXT("anyOf"))
	{
		for (const FAINpcVisualScenarioAssertion& Child : Assertion.Children)
		{
			if (!SampleProjectObservationForFinalAssertion(Child, OutFailure))
			{
				return false;
			}
		}
		return true;
	}
	if (!Assertion.Observation.StartsWith(TEXT("project.")))
	{
		return true;
	}
	if (!Runtime->AdapterRunView.IsValid() || !Runtime->ProjectFixtureActor.IsValid())
	{
		if (OutFailure)
		{
			OutFailure->Category = TEXT("final-project-source");
			OutFailure->ObservationName = Assertion.Observation;
			OutFailure->Message = FString::Printf(TEXT("stage=FinalAssertion testId=%s observation=%s targetRef=%s reason=fixture.actor is unavailable for project observation sampling"), *Config.TestId, *Assertion.Observation, FixtureActorRef);
		}
		return false;
	}

	using namespace AINpc::Visual::TestInternal;
	FAINpcVisualObservationDeclaration Declaration;
	const FVisualAdapterDescriptorValidationResult ProviderDescriptor = FindObservationProviderDeclaration(Assertion.Observation, Declaration, TEXT("FinalAssertion"), Config.TestId);
	if (!ProviderDescriptor.IsSuccess())
	{
		if (OutFailure)
		{
			OutFailure->Category = TEXT("undeclared-project-observation");
			OutFailure->ObservationName = Assertion.Observation;
			OutFailure->Message = ProviderDescriptor.Diagnostic;
		}
		return false;
	}
	const FAdapterViewLookupResult Lookup = Runtime->AdapterRunView->FindAdapter(EAINpcVisualAdapterCategory::ObservationProvider, ProviderDescriptor.Descriptor.AdapterId, TEXT("FinalAssertion"));
	FString LookupDiagnostic;
	FAINpcVisualObservationSampleResult SampleResult;
	const bool bUsedAdapter = Lookup.UseAdapter([this, &Assertion, &ProviderDescriptor, &SampleResult](IAINpcVisualAdapterInstance& Instance)
	{
		FAINpcVisualObservationSampleRequest Request;
		Request.TestId = Config.TestId;
		Request.RunId = Runtime->RunId;
		Request.AdapterId = ProviderDescriptor.Descriptor.AdapterId;
		Request.ObservationName = Assertion.Observation;
		Request.SourceActor = Runtime->ProjectFixtureActor;
		SampleResult = static_cast<IAINpcVisualObservationProviderAdapter&>(Instance).SampleObservation(Request);
	}, &LookupDiagnostic);
	if (!bUsedAdapter)
	{
		if (OutFailure)
		{
			OutFailure->Category = TEXT("project-observation-provider");
			OutFailure->ObservationName = Assertion.Observation;
			OutFailure->Message = LookupDiagnostic;
		}
		return false;
	}
	if (!SampleResult.bSuccess)
	{
		if (OutFailure)
		{
			OutFailure->Category = TEXT("project-observation-sample");
			OutFailure->ObservationName = Assertion.Observation;
			OutFailure->Message = !SampleResult.FailureReason.IsEmpty() ? SampleResult.FailureReason : SampleResult.Diagnostic;
		}
		return false;
	}
	const FString ObservationName = SampleResult.Observation.Name;
	const EAINpcVisualObservationValueType ObservationValueType = SampleResult.Observation.ValueType;
	const bool bBoolValue = SampleResult.Observation.BoolValue;
	const int32 IntegerValue = SampleResult.Observation.IntegerValue;
	const double NumberValue = SampleResult.Observation.NumberValue;
	const FString StringValue = SampleResult.Observation.StringValue;
	SampleResult.Observation.StepIndex = ActiveStepIndex;
	SampleResult.Observation.TimestampSeconds = Runtime->GetTimeSeconds(0.0);
	SampleResult.Observation.ElapsedSeconds = SampleResult.Observation.TimestampSeconds - ActiveStepStartSeconds;
	if (SampleResult.Observation.Name != Assertion.Observation
		|| SampleResult.Observation.ValueType != Declaration.ValueType
		|| SampleResult.Observation.SourceKind != Declaration.SourceKind
		|| SampleResult.Observation.SamplingMethod != Declaration.SamplingMethod
		|| SampleResult.Observation.AdapterOrProviderId != ProviderDescriptor.Descriptor.AdapterId.ToString()
		|| (Declaration.bRequiresSourceObjectPath && SampleResult.Observation.SourceObjectPath.IsEmpty())
		|| (Declaration.bRequiresSourceClass && SampleResult.Observation.SourceClass.IsEmpty()))
	{
		if (OutFailure)
		{
			OutFailure->Category = TEXT("project-observation-metadata");
			OutFailure->ObservationName = Assertion.Observation;
			OutFailure->SourceKind = SampleResult.Observation.SourceKind;
			OutFailure->SourceId = SampleResult.Observation.AdapterOrProviderId;
			OutFailure->Message = FString::Printf(TEXT("stage=FinalAssertion testId=%s observation=%s adapter=%s reason=sampled project observation metadata does not match declaration"), *Config.TestId, *Assertion.Observation, *ProviderDescriptor.Descriptor.AdapterId.ToString());
		}
		return false;
	}
	if (!RecordObservation(MoveTemp(SampleResult.Observation), true))
	{
		return false;
	}
	if (ObservationValueType == EAINpcVisualObservationValueType::Boolean)
	{
		Observations->RecordBoolSummary(ObservationName, bBoolValue);
	}
	else if (ObservationValueType == EAINpcVisualObservationValueType::Integer)
	{
		Observations->RecordIntegerSummary(ObservationName, IntegerValue);
	}
	else if (ObservationValueType == EAINpcVisualObservationValueType::Number)
	{
		Observations->RecordNumberSummary(ObservationName, NumberValue);
	}
	else if (ObservationValueType == EAINpcVisualObservationValueType::String)
	{
		Observations->RecordStringSummary(ObservationName, StringValue);
	}
	return true;
}

FAINpcVisualObservationWindow FAINpcDataDrivenVisualScenarioTest::FImplementation::GetCurrentStepWindow() const
{
	FAINpcVisualObservationWindow Window;
	Window.StepIndex = ActiveStepIndex;
	Window.StartSeconds = ActiveStepStartSeconds;
	Window.EndSeconds = Runtime->GetTimeSeconds(ActiveStepStartSeconds);
	return Window;
}

FAINpcVisualObservationWindow FAINpcDataDrivenVisualScenarioTest::FImplementation::GetScenarioHistoryWindow() const
{
	FAINpcVisualObservationWindow Window;
	Window.StepIndex = INDEX_NONE;
	Window.StartSeconds = 0.0;
	Window.EndSeconds = Runtime->GetTimeSeconds(0.0);
	Window.bScenarioHistory = true;
	return Window;
}

FString FAINpcDataDrivenVisualScenarioTest::FImplementation::BuildPrompt(FString& OutFailureReason) const
{
	FString Prompt;
	if (!AINpcDialogueVisualTestSupport::LoadRequiredConfigText(*Config.Prompt.File, TEXT("prompt template"), Prompt, OutFailureReason)) { return FString(); }
	const TArray<FString> UnknownPlaceholders = FindUnknownPromptPlaceholders(Prompt, Config.Prompt.Variables);
	if (!UnknownPlaceholders.IsEmpty())
	{
		OutFailureReason = FString::Printf(TEXT("Scenario '%s' prompt file '%s' contains undeclared placeholder(s): %s"), *Config.TestId, *Config.Prompt.File, *DescribeUnresolvedPromptPlaceholders(UnknownPlaceholders));
		return FString();
	}
	TMap<FString, FString> RuntimeVariables = Config.Prompt.Variables;
	if (RuntimeVariables.Contains(SmartObjectTargetIdVariable))
	{
		const TArray<FString> AvailableTargets = Runtime->GetAvailableSmartObjectTargetsForExecution();
		if (AvailableTargets.IsEmpty())
		{
			OutFailureReason = FString::Printf(TEXT("Scenario '%s' prompt variable '%s' cannot resolve because SmartObject target list is empty."), *Config.TestId, SmartObjectTargetIdVariable);
			return FString();
		}
		RuntimeVariables[SmartObjectTargetIdVariable] = AvailableTargets[0];
	}
	for (const TPair<FString, FString>& Variable : RuntimeVariables)
	{
		Prompt = Prompt.Replace(*FString::Printf(TEXT("{%s}"), *Variable.Key), *Variable.Value, ESearchCase::CaseSensitive);
	}
	return Prompt;
}

void FAINpcDataDrivenVisualScenarioTest::FImplementation::HandleTimeout()
{
	if (!bComplete && !bFailed)
	{
		Fail(FString::Printf(TEXT("Timed out after %ds. Evidence={%s}. %s"), Config.TimeoutSec, *BuildSummary(), *AINpcDialogueVisualTestSupport::DescribeDialogueState(Runtime->GetNpcComponent())));
	}
}

void FAINpcDataDrivenVisualScenarioTest::FImplementation::UpdateDialogueStateEvidence()
{
	if (!Runtime->GetNpcComponent()) { return; }
	const ENpcDialogueState State = Runtime->GetNpcComponent()->GetDialogueState();
	Runtime->SetVisibleStateText(AINpcDialogueVisualTestSupport::GetDialogueStateText(Runtime->GetNpcComponent()));
	if (State == ENpcDialogueState::WaitingForLLM)
	{
		const FAINpcVisualObservationSourceInfo StateSource = MakeDialogueStateSource(TEXT("GetDialogueState"));
		MarkObservationSourceReady(TEXT("waitingStateObserved"), StateSource);
		RecordBoolObservation(TEXT("waitingStateObserved"), true, StateSource, false);
	}
	else if (State == ENpcDialogueState::Speaking)
	{
		const FAINpcVisualObservationSourceInfo StateSource = MakeDialogueStateSource(TEXT("GetDialogueState"));
		MarkObservationSourceReady(TEXT("speakingStateObserved"), StateSource);
		RecordBoolObservation(TEXT("speakingStateObserved"), true, StateSource, false);
	}
}

bool FAINpcDataDrivenVisualScenarioTest::FImplementation::ConfigurePersona(FString& OutFailureReason)
{
	FString DelayFillerText;
	if (!AINpcDialogueVisualTestSupport::LoadRequiredConfigText(*Config.Persona.DelayFillerFile, TEXT("delay filler"), DelayFillerText, OutFailureReason)) { return false; }
	AINpcDialogueVisualTestSupport::FVisualHarnessPersonaText PersonaText;
	if (!AINpcDialogueVisualTestSupport::LoadPersonaText(*Config.Persona.File, PersonaText, OutFailureReason)) { return false; }
	VisualHarnessPersona = NewObject<UNpcPersonaDataAsset>(Runtime->CreatePersonaOuter(), *FString::Printf(TEXT("%sPersona"), *Config.TestId.Replace(TEXT("."), TEXT("_"))));
	if (!VisualHarnessPersona)
	{
		OutFailureReason = FString::Printf(TEXT("Failed to allocate persona for scenario '%s'."), *Config.TestId);
		return false;
	}
	VisualHarnessPersona->PersonaName = PersonaText.PersonaName;
	VisualHarnessPersona->Background = PersonaText.Background;
	VisualHarnessPersona->SpeakingStyle = PersonaText.SpeakingStyle;
	VisualHarnessPersona->DelayFillerThreshold = Config.Persona.DelayFillerThreshold;
	VisualHarnessPersona->DelayFillerTexts.Add(FText::FromString(DelayFillerText));
	for (const FAINpcVisualScenarioStep& Step : Config.Steps)
	{
		if (Step.Type == TEXT("world.event"))
		{
			if (Config.Persona.DelayFillerThreshold > 0.0f) { VisualHarnessPersona->InspectDelayMaskingMontages.Add(NewObject<UAnimMontage>(VisualHarnessPersona)); }
			const FString EventRoute = Step.Payload.EventTag.IsEmpty() ? Step.Payload.EventId : Step.Payload.EventTag;
				const FGameplayTag RouteTag = FGameplayTag::RequestGameplayTag(FName(*EventRoute), false);
			if (!RouteTag.IsValid())
			{
				OutFailureReason = FString::Printf(TEXT("Scenario '%s' requires gameplay tag '%s'."), *Config.TestId, *EventRoute);
				return false;
			}
			Runtime->AddEventSubscriptionTag(RouteTag);
		}
	}
	Runtime->SetPersonaData(VisualHarnessPersona);
	return true;
}

void FAINpcDataDrivenVisualScenarioTest::FImplementation::Fail(const FString& Reason)
{
	if (bComplete || bFailed) { return; }
	bFailed = true;
	FailureReason = Reason;
	FinalizeActiveStepDiagnostic(TEXT("FAIL"), Reason);
	if (UWorld* World = Runtime->GetWorld())
	{
		World->GetTimerManager().ClearTimer(StepTimerHandle);
		World->GetTimerManager().ClearTimer(TimeoutTimerHandle);
	}
}

void FAINpcDataDrivenVisualScenarioTest::FImplementation::AddStartupDiagnostic(const FString& Stage, const FString& Reason, const FString& AdapterCategory, const FString& AdapterId, const FString& ActorClass, const FString& ActorTag, const FString& TargetRef, const FString& FieldName, const FString& Capability, const FString& ObservationName, const FString& OwnerModuleName)
{
	FAINpcVisualTestStepDiagnostic Diagnostic;
	Diagnostic.TestId = Config.TestId;
	Diagnostic.StepIndex = INDEX_NONE;
	Diagnostic.Status = TEXT("FAIL");
	Diagnostic.Stage = Stage;
	Diagnostic.FailureReason = Reason;
	Diagnostic.AdapterCategory = AdapterCategory;
	Diagnostic.AdapterId = AdapterId;
	Diagnostic.OwnerModuleName = OwnerModuleName;
	Diagnostic.ActorClass = ActorClass;
	Diagnostic.ActorTag = ActorTag;
	Diagnostic.TargetRef = TargetRef;
	Diagnostic.FieldName = FieldName;
	Diagnostic.Capability = Capability;
	Diagnostic.ObservationName = ObservationName;
	StepDiagnostics.Add(MoveTemp(Diagnostic));
}

void FAINpcDataDrivenVisualScenarioTest::FImplementation::FailWithAssertion(const FString& Prefix, const FAINpcVisualAssertionFailureDetail& Failure)
{
	const FString Reason = FString::Printf(TEXT("%s FailureCategory=%s TestId=%s StepIndex=%d Observation=%s SourceKind=%s SourceId=%s Detail=%s"), *Prefix, *Failure.Category, *Config.TestId, ActiveStepIndex, *Failure.ObservationName, *Failure.SourceKind, *Failure.SourceId, *Failure.Message);
	if (bComplete || bFailed) { return; }
	bFailed = true;
	FailureReason = Reason;
	if (StepDiagnostics.IsValidIndex(StepDiagnostics.Num() - 1)
		&& StepDiagnostics.Last().StepIndex == ActiveStepIndex
		&& StepDiagnostics.Last().Status == TEXT("running"))
	{
		FinalizeActiveStepDiagnostic(TEXT("FAIL"), Reason, Failure.Category, Failure.ObservationName, Failure.SourceKind, Failure.SourceId);
	}
	else
	{
		FAINpcVisualTestStepDiagnostic Diagnostic;
		Diagnostic.TestId = Config.TestId;
		Diagnostic.StepIndex = ActiveStepIndex;
		Diagnostic.Status = TEXT("FAIL");
		Diagnostic.Stage = TEXT("FinalAssertion");
		Diagnostic.FailureReason = Reason;
		Diagnostic.FailureCategory = Failure.Category;
		Diagnostic.ObservationName = Failure.ObservationName;
		Diagnostic.SourceKind = Failure.SourceKind;
		Diagnostic.SourceId = Failure.SourceId;
		StepDiagnostics.Add(MoveTemp(Diagnostic));
	}
	if (UWorld* World = Runtime->GetWorld())
	{
		World->GetTimerManager().ClearTimer(StepTimerHandle);
		World->GetTimerManager().ClearTimer(TimeoutTimerHandle);
	}
}

void FAINpcDataDrivenVisualScenarioTest::FImplementation::ShowStatus(const FString& Message, const FColor& Color, const float DurationSeconds) const
{
	UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);
	if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, DurationSeconds, Color, Message); }
}

void FAINpcDataDrivenVisualScenarioTest::FImplementation::RecordBoolObservation(const FString& Name, const bool bValue, const FAINpcVisualObservationSourceInfo& SourceInfo, const bool bRequiresAllowedFinalSource)
{
	FAINpcVisualObservationRecord Record;
	Record.Name = Name;
	Record.ValueType = EAINpcVisualObservationValueType::Boolean;
	Record.BoolValue = bValue;
	Record.SourceKind = SourceInfo.SourceKind;
	Record.SourceIdentity = SourceInfo.SourceIdentity;
	Record.SourceObjectPath = SourceInfo.SourceObjectPath;
	Record.SourceClass = SourceInfo.SourceClass;
	Record.SamplingMethod = SourceInfo.SamplingMethod;
	Record.AdapterOrProviderId = SourceInfo.AdapterOrProviderId;
	Record.StepIndex = ActiveStepIndex;
	Record.TimestampSeconds = Runtime->GetTimeSeconds(0.0);
	Record.ElapsedSeconds = Record.TimestampSeconds - ActiveStepStartSeconds;
	if (RecordObservation(MoveTemp(Record), bRequiresAllowedFinalSource || FAINpcVisualObservationStore::IsFinalSuccessObservationName(Name)))
	{
		Observations->RecordBoolSummary(Name, bValue);
	}
}

void FAINpcDataDrivenVisualScenarioTest::FImplementation::RecordIntegerObservation(const FString& Name, const int32 Value, const FAINpcVisualObservationSourceInfo& SourceInfo)
{
	FAINpcVisualObservationRecord Record;
	Record.Name = Name;
	Record.ValueType = EAINpcVisualObservationValueType::Integer;
	Record.IntegerValue = Value;
	Record.SourceKind = SourceInfo.SourceKind;
	Record.SourceIdentity = SourceInfo.SourceIdentity;
	Record.SourceObjectPath = SourceInfo.SourceObjectPath;
	Record.SourceClass = SourceInfo.SourceClass;
	Record.SamplingMethod = SourceInfo.SamplingMethod;
	Record.AdapterOrProviderId = SourceInfo.AdapterOrProviderId;
	Record.StepIndex = ActiveStepIndex;
	Record.TimestampSeconds = Runtime->GetTimeSeconds(0.0);
	Record.ElapsedSeconds = Record.TimestampSeconds - ActiveStepStartSeconds;
	if (RecordObservation(MoveTemp(Record), false))
	{
		Observations->RecordIntegerSummary(Name, Value);
	}
}

void FAINpcDataDrivenVisualScenarioTest::FImplementation::RecordNumberObservation(const FString& Name, const double Value, const FAINpcVisualObservationSourceInfo& SourceInfo)
{
	FAINpcVisualObservationRecord Record;
	Record.Name = Name;
	Record.ValueType = EAINpcVisualObservationValueType::Number;
	Record.NumberValue = Value;
	Record.SourceKind = SourceInfo.SourceKind;
	Record.SourceIdentity = SourceInfo.SourceIdentity;
	Record.SourceObjectPath = SourceInfo.SourceObjectPath;
	Record.SourceClass = SourceInfo.SourceClass;
	Record.SamplingMethod = SourceInfo.SamplingMethod;
	Record.AdapterOrProviderId = SourceInfo.AdapterOrProviderId;
	Record.StepIndex = ActiveStepIndex;
	Record.TimestampSeconds = Runtime->GetTimeSeconds(0.0);
	Record.ElapsedSeconds = Record.TimestampSeconds - ActiveStepStartSeconds;
	if (RecordObservation(MoveTemp(Record), false))
	{
		Observations->RecordNumberSummary(Name, Value);
	}
}

void FAINpcDataDrivenVisualScenarioTest::FImplementation::RecordStringObservation(const FString& Name, const FString& Value, const FAINpcVisualObservationSourceInfo& SourceInfo)
{
	FAINpcVisualObservationRecord Record;
	Record.Name = Name;
	Record.ValueType = EAINpcVisualObservationValueType::String;
	Record.StringValue = Value;
	Record.SourceKind = SourceInfo.SourceKind;
	Record.SourceIdentity = SourceInfo.SourceIdentity;
	Record.SourceObjectPath = SourceInfo.SourceObjectPath;
	Record.SourceClass = SourceInfo.SourceClass;
	Record.SamplingMethod = SourceInfo.SamplingMethod;
	Record.AdapterOrProviderId = SourceInfo.AdapterOrProviderId;
	Record.StepIndex = ActiveStepIndex;
	Record.TimestampSeconds = Runtime->GetTimeSeconds(0.0);
	Record.ElapsedSeconds = Record.TimestampSeconds - ActiveStepStartSeconds;
	if (RecordObservation(MoveTemp(Record), false))
	{
		Observations->RecordStringSummary(Name, Value);
	}
}

bool FAINpcDataDrivenVisualScenarioTest::FImplementation::RecordObservation(FAINpcVisualObservationRecord Record, const bool bRequiresAllowedFinalSource)
{
	FString StoreFailureReason;
	if (!Observations->RecordObservation(MoveTemp(Record), bRequiresAllowedFinalSource, StoreFailureReason))
	{
		Fail(StoreFailureReason);
		return false;
	}
	return true;
}

void FAINpcDataDrivenVisualScenarioTest::FImplementation::MarkObservationSourceReady(const FString& Name, const FAINpcVisualObservationSourceInfo& SourceInfo)
{
	const double Now = Runtime->GetTimeSeconds(0.0);
	Observations->MarkSourceReady(Name, SourceInfo, ActiveStepIndex, Now);
}

void FAINpcDataDrivenVisualScenarioTest::FImplementation::MarkObservationSourceReadyForWindow(const FString& Name, const FAINpcVisualObservationSourceInfo& SourceInfo, const FAINpcVisualObservationWindow& Window)
{
	if (Window.bScenarioHistory) { return; }
	Observations->MarkSourceReadyForWindow(Name, SourceInfo, Window);
}

void FAINpcDataDrivenVisualScenarioTest::FImplementation::RefreshActionTargetObservation()
{
	const auto CharacterSource = MakeCharacterSource(TEXT("poll"));
	RecordNumberObservation(TEXT("distanceToActionTarget"), Runtime->GetVisualActionTargetDistance(), CharacterSource);
	if (Runtime->HasReachedVisualActionTarget())
	{
		MarkObservationSourceReady(TEXT("actionTargetReached"), CharacterSource);
		RecordBoolObservation(TEXT("actionTargetReached"), true, CharacterSource, true);
	}
}

void FAINpcDataDrivenVisualScenarioTest::FImplementation::CompleteCurrentStep()
{
	FinalizeActiveStepDiagnostic(TEXT("PASS"), FString());
	StartNextStep();
}

void FAINpcDataDrivenVisualScenarioTest::FImplementation::FinalizeActiveStepDiagnostic(const FString& Status, const FString& Reason, const FString& FailureCategory, const FString& ObservationName, const FString& SourceKind, const FString& SourceId)
{
	if (!StepDiagnostics.IsValidIndex(StepDiagnostics.Num() - 1)) { return; }
	FAINpcVisualTestStepDiagnostic& Diagnostic = StepDiagnostics.Last();
	if (Diagnostic.StepIndex != ActiveStepIndex || Diagnostic.Status != TEXT("running")) { return; }
	const double Now = Runtime->GetTimeSeconds(ActiveStepStartSeconds);
	Diagnostic.Status = Status;
	Diagnostic.FailureReason = Reason;
	Diagnostic.FailureCategory = FailureCategory;
	Diagnostic.ObservationName = ObservationName;
	Diagnostic.SourceKind = SourceKind;
	Diagnostic.SourceId = SourceId;
	Diagnostic.DurationMs = FMath::Max(0.0, Now - ActiveStepStartSeconds) * 1000.0;
}

FAINpcVisualObservationSourceInfo FAINpcDataDrivenVisualScenarioTest::FImplementation::MakeDialogueSource(const TCHAR* CallbackName) const
{
	FAINpcVisualObservationSourceInfo Source;
	Source.SourceKind = TEXT("callback");
	Source.SourceIdentity = CallbackName;
	Source.SourceObjectPath = Runtime->GetNpcComponent() ? Runtime->GetNpcComponent()->GetPathName() : FString();
	Source.SourceClass = Runtime->GetNpcComponent() ? Runtime->GetNpcComponent()->GetClass()->GetName() : FString();
	Source.SamplingMethod = TEXT("delegate");
	Source.AdapterOrProviderId = TEXT("provider.primary.dialogue");
	return Source;
}

FAINpcVisualObservationSourceInfo FAINpcDataDrivenVisualScenarioTest::FImplementation::MakeDialogueStateSource(const TCHAR* SamplingName) const
{
	FAINpcVisualObservationSourceInfo Source;
	Source.SourceKind = TEXT("component");
	Source.SourceIdentity = SamplingName;
	Source.SourceObjectPath = Runtime->GetNpcComponent() ? Runtime->GetNpcComponent()->GetPathName() : FString();
	Source.SourceClass = Runtime->GetNpcComponent() ? Runtime->GetNpcComponent()->GetClass()->GetName() : FString();
	Source.SamplingMethod = TEXT("state-read");
	Source.AdapterOrProviderId = TEXT("builtin.dialogueObservation");
	return Source;
}

FAINpcVisualObservationSourceInfo FAINpcDataDrivenVisualScenarioTest::FImplementation::MakeEventSource() const
{
	FAINpcVisualObservationSourceInfo Source;
	Source.SourceKind = TEXT("subsystem");
	Source.SourceIdentity = TEXT("UNpcEventSubsystem");
	Source.SourceObjectPath = Runtime->GetWorld() && Runtime->GetWorld()->GetGameInstance() && Runtime->GetWorld()->GetGameInstance()->GetSubsystem<UNpcEventSubsystem>() ? Runtime->GetWorld()->GetGameInstance()->GetSubsystem<UNpcEventSubsystem>()->GetPathName() : FString();
	Source.SourceClass = TEXT("UNpcEventSubsystem");
	Source.SamplingMethod = TEXT("broadcast");
	Source.AdapterOrProviderId = AINpcVisualInternalAdapters::NpcEventAdapterId();
	return Source;
}

FAINpcVisualObservationSourceInfo FAINpcDataDrivenVisualScenarioTest::FImplementation::MakeActionSource() const
{
	FAINpcVisualObservationSourceInfo Source;
	Source.SourceKind = TEXT("observation-provider");
	Source.SourceIdentity = TEXT("builtin.smartObjectAction");
	Source.SourceObjectPath = Runtime->GetNpcActor() ? Runtime->GetNpcActor()->GetPathName() : FString();
	Source.SourceClass = Runtime->GetNpcActor() ? Runtime->GetNpcActor()->GetClass()->GetName() : FString();
	Source.SamplingMethod = TEXT("action-execution");
	Source.AdapterOrProviderId = AINpcVisualInternalAdapters::SmartObjectActionAdapterId();
	return Source;
}

FAINpcVisualObservationSourceInfo FAINpcDataDrivenVisualScenarioTest::FImplementation::MakeCharacterSource(const TCHAR* SamplingName) const
{
	FAINpcVisualObservationSourceInfo Source;
	Source.SourceKind = TEXT("actor");
	Source.SourceIdentity = SamplingName;
	Source.SourceObjectPath = Runtime->GetNpcActor() ? Runtime->GetNpcActor()->GetPathName() : FString();
	Source.SourceClass = Runtime->GetNpcActor() ? Runtime->GetNpcActor()->GetClass()->GetName() : FString();
	Source.SamplingMethod = TEXT("driver-poll");
	Source.AdapterOrProviderId = TEXT("builtin.characterDriver");
	return Source;
}
