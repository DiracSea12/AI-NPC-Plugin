#include "Test/AINpcDataDrivenVisualScenarioTest.h"

#include "Animation/AnimMontage.h"
#include "Components/AINpcComponent.h"
#include "Data/NpcPersonaDataAsset.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Events/NpcEventPayloadBlueprintLibrary.h"
#include "Events/NpcEventSubsystem.h"
#include "GameplayTagContainer.h"
#include "LLM/LLMResponseParser.h"
#include "SmartObjectBridge/SmartObjectBridgeContext.h"
#include "Test/AINpcDialogueVisualTestSupport.h"
#include "Test/AINpcSmartObjectActionTestSupport.h"
#include "Test/AINpcTestCharacter.h"
#include "Test/AINpcTestSmartObjectActor.h"
#include "TimerManager.h"

namespace
{
	const float InitialDialogueDelaySeconds = 3.0f;
	const TCHAR* SmartObjectTargetIdVariable = TEXT("SmartObjectTargetId");
	const TCHAR* BuiltInNpcEventAdapterId = TEXT("builtin.npcEvent");
	const TCHAR* BuiltInSmartObjectActionAdapterId = TEXT("builtin.smartObjectAction");
	const TCHAR* FixtureNpcRef = TEXT("fixture.npc");
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
}

struct FAINpcInternalDialogueObservationProvider
	{
		UAINpcComponent* NpcComponent = nullptr;
		TFunction<void(const FString&, bool)> RecordBool;
		TFunction<bool(const FString&)> HasBool;
		TFunction<void(const FString&)> SetVisibleDialogueText;
		TFunction<void()> UpdateDialogueStateEvidence;
		TFunction<bool()> HasLatestActionIntent;
		TFunction<void(const FString&)> Fail;
		TFunction<void(const FString&, const FColor&, float)> ShowStatus;
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

		void OnSessionStarted()
		{
			RecordBool(TEXT("sessionStarted"), true);
			ShowStatus(TEXT("Dialogue session started through the real provider chain."), FColor::Green, 6.0f);
		}

		void OnResponse(const FString& Text)
		{
			LastNpcResponseText = Text;
			LastNpcResponseText.TrimStartAndEndInline();
			if (LastNpcResponseText.IsEmpty())
			{
				Fail(TEXT("Provider returned an empty dialogue response."));
				return;
			}
			RecordBool(TEXT("dialogueResponseObserved"), true);
			RecordBool(TEXT("structuredResponseObserved"), true);
			if (HasLatestActionIntent()) { RecordBool(TEXT("actionIntentObserved"), true); }
			SetVisibleDialogueText(LastNpcResponseText);
			UpdateDialogueStateEvidence();
			ShowStatus(FString::Printf(TEXT("<<< NPC response received. Length=%d"), LastNpcResponseText.Len()), FColor::Yellow, 8.0f);
		}

		void OnPartialResponse(const FString& Text)
		{
			if (!Text.IsEmpty())
			{
				LastPartialResponseText = Text;
				RecordBool(TEXT("partialResponseObserved"), true);
			}
			ShowStatus(FString::Printf(TEXT("[stream] partial response. Length=%d"), Text.Len()), FColor::Orange, 3.0f);
		}

		void OnError(const FString& ErrorMessage)
		{
			Fail(FString::Printf(TEXT("Provider chain reported an error. Length=%d"), ErrorMessage.Len()));
		}

		void OnSessionEnded()
		{
			UpdateDialogueStateEvidence();
		}

		void OnDegraded(const FString& FallbackResponse, const FString& DegradedFailureReason)
		{
			(void)FallbackResponse;
			Fail(FString::Printf(TEXT("Provider chain degraded. ReasonLength=%d"), DegradedFailureReason.Len()));
		}

		void OnDelayMaskingStart(UAnimMontage* Montage, const FText& FillerText)
		{
			(void)Montage;
			LastDelayFillerText = FillerText.ToString();
			RecordBool(TEXT("delayMaskingStartObserved"), true);
			if (HasBool(TEXT("eventTriggerBroadcast"))) { RecordBool(TEXT("eventDelayMaskingStartObserved"), true); }
			ShowStatus(FString::Printf(TEXT("Delay masking started. FillerLength=%d"), LastDelayFillerText.Len()), FColor::Purple, 5.0f);
		}

		void OnDelayMaskingEnd()
		{
			RecordBool(TEXT("delayMaskingEndObserved"), true);
			ShowStatus(TEXT("Delay masking ended."), FColor::Purple, 3.0f);
		}
};

namespace
{
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
			TEXT("actionTargetReached")
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

struct FAINpcVisualScenarioRuntimeView
{
	UWorld* World = nullptr;
	FString TestId;
	FString RunId;
	AAINpcTestSmartObjectActor* SmartObject = nullptr;
	TUniquePtr<FAINpcBuiltInCharacterDriver> CharacterDriver;
	TUniquePtr<FAINpcInternalDialogueObservationProvider> DialogueObservationProvider;

	UWorld* GetWorld() const
	{
		return CharacterDriver ? CharacterDriver->GetWorld() : World;
	}

	double GetTimeSeconds(const double FallbackSeconds) const
	{
		const UWorld* RunWorld = GetWorld();
		return RunWorld ? RunWorld->GetTimeSeconds() : FallbackSeconds;
	}

	UAINpcComponent* GetNpcComponent() const
	{
		return CharacterDriver ? CharacterDriver->GetNpcComponent() : nullptr;
	}

	AActor* GetNpcActor() const { return CharacterDriver ? CharacterDriver->GetActor() : nullptr; }
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


namespace
{
	struct FAINpcBuiltInEventAdapter
	{
		bool Execute(const FAINpcVisualScenarioStep& Step, const FAINpcVisualScenarioRuntimeView& Runtime, FString& OutFailureReason) const
		{
			if (Step.Payload.AdapterId != BuiltInNpcEventAdapterId)
			{
				OutFailureReason = FString::Printf(TEXT("world.event adapter '%s' is unsupported."), *Step.Payload.AdapterId);
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
			if (Step.Payload.AdapterId != BuiltInSmartObjectActionAdapterId)
			{
				OutFailureReason = FString::Printf(TEXT("action.executeLatestIntent adapter '%s' is unsupported."), *Step.Payload.AdapterId);
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
}

FAINpcDataDrivenVisualScenarioTest::FAINpcDataDrivenVisualScenarioTest(const FAINpcVisualTestContext& Context, FAINpcVisualScenarioConfig InConfig)
	: Runtime(MakeUnique<FAINpcVisualScenarioRuntimeView>())
	, Config(MoveTemp(InConfig))
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

FAINpcDataDrivenVisualScenarioTest::~FAINpcDataDrivenVisualScenarioTest()
{
	if (UWorld* World = Runtime->GetWorld())
	{
		World->GetTimerManager().ClearTimer(StepTimerHandle);
		World->GetTimerManager().ClearTimer(TimeoutTimerHandle);
	}
	if (Runtime->DialogueObservationProvider) { Runtime->DialogueObservationProvider->Stop(); }
}

bool FAINpcDataDrivenVisualScenarioTest::Start(FString& OutFailureReason)
{
	if (!Runtime->GetNpcComponent())
	{
		OutFailureReason = FString::Printf(TEXT("Visual scenario '%s' cannot start because NPC component is null."), *Config.TestId);
		return false;
	}
	if (!ConfigurePersona(OutFailureReason)) { return false; }
	if (!AINpcDialogueVisualTestSupport::ValidateProviderConfiguration(Runtime->GetNpcComponent(), OutFailureReason)) { return false; }

	if (!StartDialogueObservation(OutFailureReason)) { return false; }
	bStarted = true;
	ShowStatus(FString::Printf(TEXT("Visual scenario '%s' ready. Starting DSL steps in %.0fs."), *Config.TestId, InitialDialogueDelaySeconds), FColor::Green, 15.0f);
	if (UWorld* World = Runtime->GetWorld())
	{
		World->GetTimerManager().SetTimer(TimeoutTimerHandle, FTimerDelegate::CreateRaw(this, &FAINpcDataDrivenVisualScenarioTest::HandleTimeout), static_cast<float>(Config.TimeoutSec), false);
		World->GetTimerManager().SetTimer(StepTimerHandle, FTimerDelegate::CreateRaw(this, &FAINpcDataDrivenVisualScenarioTest::StartNextStep), InitialDialogueDelaySeconds, false);
		return true;
	}

	OutFailureReason = FString::Printf(TEXT("Visual scenario '%s' cannot start because World is null."), *Config.TestId);
	return false;
}

bool FAINpcDataDrivenVisualScenarioTest::StartDialogueObservation(FString& OutFailureReason)
{
	UAINpcComponent* NpcComponent = Runtime->GetNpcComponent();
	if (!NpcComponent)
	{
		OutFailureReason = FString::Printf(TEXT("Scenario '%s' cannot observe dialogue because NPC component is null."), *Config.TestId);
		return false;
	}
	Runtime->DialogueObservationProvider = MakeUnique<FAINpcInternalDialogueObservationProvider>();
	Runtime->DialogueObservationProvider->RecordBool = [this](const FString& Name, const bool bValue) { RecordBoolObservation(Name, bValue); };
	Runtime->DialogueObservationProvider->HasBool = [this](const FString& Name) { bool Value = false; return TryGetObservationBool(Name, Value) && Value; };
	Runtime->DialogueObservationProvider->SetVisibleDialogueText = [this](const FString& Text) { Runtime->SetVisibleDialogueText(Text); };
	Runtime->DialogueObservationProvider->UpdateDialogueStateEvidence = [this]() { UpdateDialogueStateEvidence(); };
	Runtime->DialogueObservationProvider->HasLatestActionIntent = [this]() { FNpcAction LatestAction; return Runtime->TryGetLatestActionIntent(LatestAction); };
	Runtime->DialogueObservationProvider->Fail = [this](const FString& Reason) { Fail(Reason); };
	Runtime->DialogueObservationProvider->ShowStatus = [this](const FString& Message, const FColor& Color, const float DurationSeconds) { ShowStatus(Message, Color, DurationSeconds); };
	return Runtime->DialogueObservationProvider->Start(*NpcComponent, OutFailureReason);
}

void FAINpcDataDrivenVisualScenarioTest::Poll()
{
	if (!bStarted || bComplete || bFailed) { return; }
	UpdateDialogueStateEvidence();
	RefreshActionTargetObservation();
	PollActiveStep();
}

bool FAINpcDataDrivenVisualScenarioTest::IsComplete() const { return bComplete; }
bool FAINpcDataDrivenVisualScenarioTest::HasFailed() const { return bFailed; }
const FString& FAINpcDataDrivenVisualScenarioTest::GetFailureReason() const { return FailureReason; }

FString FAINpcDataDrivenVisualScenarioTest::BuildSummary() const
{
	return FString::Printf(TEXT("TestId=%s Step=%d/%d Expect=%s"), *Config.TestId, ActiveStepIndex + 1, Config.Steps.Num(), IsAssertionSatisfied(Config.Expect.Assertion) ? TEXT("true") : TEXT("false"));
}

FAINpcVisualTestObservations FAINpcDataDrivenVisualScenarioTest::BuildObservations() const
{
	FAINpcVisualTestObservations Observations;
	Observations.BooleanFields = BoolObservations;
	Observations.IntegerFields = IntegerObservations;
	Observations.IntegerFields.Add(TEXT("responseLength"), Runtime->DialogueObservationProvider ? Runtime->DialogueObservationProvider->LastNpcResponseText.Len() : 0);
	Observations.IntegerFields.Add(TEXT("partialResponseLength"), Runtime->DialogueObservationProvider ? Runtime->DialogueObservationProvider->LastPartialResponseText.Len() : 0);
	Observations.IntegerFields.Add(TEXT("delayFillerLength"), Runtime->DialogueObservationProvider ? Runtime->DialogueObservationProvider->LastDelayFillerText.Len() : 0);
	Observations.NumberFields = NumberObservations;
	Observations.NumberFields.Add(TEXT("distanceToActionTarget"), Runtime->GetVisualActionTargetDistance());
	Observations.StringFields = StringObservations;
	Observations.StringFields.Add(TEXT("lastActionFailure"), LastActionFailureReason);
	return Observations;
}

TArray<FAINpcVisualTestStepDiagnostic> FAINpcDataDrivenVisualScenarioTest::BuildStepDiagnostics() const
{
	return StepDiagnostics;
}

void FAINpcDataDrivenVisualScenarioTest::StartNextStep()
{
	if (bComplete || bFailed) { return; }
	++ActiveStepIndex;
	if (!Config.Steps.IsValidIndex(ActiveStepIndex))
	{
		if (IsAssertionSatisfied(Config.Expect.Assertion))
		{
			bComplete = true;
		}
		else
		{
			Fail(FString::Printf(TEXT("Scenario '%s' completed steps but final expect assertion was not satisfied."), *Config.TestId));
		}
		return;
	}

	ActiveStepStartSeconds = Runtime->GetTimeSeconds(0.0);
	FAINpcVisualTestStepDiagnostic Diagnostic;
	Diagnostic.StepIndex = ActiveStepIndex;
	Diagnostic.StepType = Config.Steps[ActiveStepIndex].Type;
	Diagnostic.Status = TEXT("running");
	StepDiagnostics.Add(MoveTemp(Diagnostic));
	FString StepFailureReason;
	if (!RunStep(Config.Steps[ActiveStepIndex], StepFailureReason))
	{
		Fail(StepFailureReason);
	}
}

void FAINpcDataDrivenVisualScenarioTest::PollActiveStep()
{
	if (!Config.Steps.IsValidIndex(ActiveStepIndex)) { return; }
	const FAINpcVisualScenarioStep& Step = Config.Steps[ActiveStepIndex];
	if (Step.Type == TEXT("wait.until"))
	{
		if (IsAssertionSatisfied(Step.Condition))
		{
			CompleteCurrentStep();
			return;
		}
		const double Now = Runtime->GetTimeSeconds(ActiveStepStartSeconds);
		if (Now - ActiveStepStartSeconds > Step.Payload.TimeoutSec)
		{
			Fail(FString::Printf(TEXT("Scenario '%s' step[%d] wait.until timed out after %.1fs."), *Config.TestId, ActiveStepIndex, Step.Payload.TimeoutSec));
		}
	}
	else if (Step.Type == TEXT("observe.hold"))
	{
		const double Now = Runtime->GetTimeSeconds(ActiveStepStartSeconds);
		bool bObserved = false;
		if (!TryGetObservationBool(Step.Payload.Observation, bObserved) || !bObserved)
		{
			ActiveStepStartSeconds = Now;
			return;
		}
		if (Now - ActiveStepStartSeconds >= Step.Payload.DurationSec)
		{
			RecordBoolObservation(Step.Payload.Observation + TEXT("HoldElapsed"), true);
			CompleteCurrentStep();
		}
	}
}

bool FAINpcDataDrivenVisualScenarioTest::RunStep(const FAINpcVisualScenarioStep& Step, FString& OutFailureReason)
{
	if (Step.Type == TEXT("dialogue.start")) { return RunDialogueStart(Step, OutFailureReason); }
	if (Step.Type == TEXT("world.event")) { return RunWorldEvent(Step, OutFailureReason); }
	if (Step.Type == TEXT("action.executeLatestIntent")) { return RunActionExecuteLatestIntent(Step, OutFailureReason); }
	if (Step.Type == TEXT("observe.hold")) { return RunObserveHold(Step, OutFailureReason); }
	if (Step.Type == TEXT("wait.until")) { return true; }
	OutFailureReason = FString::Printf(TEXT("Scenario '%s' step[%d] has unsupported type '%s'."), *Config.TestId, ActiveStepIndex, *Step.Type);
	return false;
}

bool FAINpcDataDrivenVisualScenarioTest::RunDialogueStart(const FAINpcVisualScenarioStep& Step, FString& OutFailureReason)
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

bool FAINpcDataDrivenVisualScenarioTest::RunWorldEvent(const FAINpcVisualScenarioStep& Step, FString& OutFailureReason)
{
	FAINpcBuiltInEventAdapter Adapter;
	if (!Adapter.Execute(Step, *Runtime, OutFailureReason)) { return false; }
	RecordBoolObservation(TEXT("eventTriggerBroadcast"), true);
	ShowStatus(TEXT("Gameplay event trigger broadcast while provider request is in flight."), FColor::Orange, 7.0f);
	CompleteCurrentStep();
	return true;
}

bool FAINpcDataDrivenVisualScenarioTest::RunActionExecuteLatestIntent(const FAINpcVisualScenarioStep& Step, FString& OutFailureReason)
{
	FAINpcBuiltInActionAdapter Adapter;
	FAINpcVisualSmartObjectActionExecution Execution;
	FString ActionFailureReason;
	const EAINpcActionAdapterExecuteResult ExecuteResult = Adapter.Execute(Step, *Runtime, Execution, ActionFailureReason);
	if (ExecuteResult == EAINpcActionAdapterExecuteResult::Accepted)
	{
		RecordBoolObservation(TEXT("actionExecutionAccepted"), true);
		Runtime->SetSmartObjectInteractionState(true);
		Runtime->BeginVisualActionMove(Execution.ClaimedSlotTransform, Execution.RequestedTarget);
		ShowStatus(FString::Printf(TEXT("SmartObject action accepted: %s -> %s."), *Execution.ActionType, *Execution.RequestedTarget), FColor::Green, 8.0f);
		CompleteCurrentStep();
		return true;
	}

	LastActionFailureReason = ActionFailureReason;
	if (ExecuteResult == EAINpcActionAdapterExecuteResult::Rejected && Step.Payload.bAllowActionRejection)
	{
		RecordBoolObservation(TEXT("actionRejectedVisible"), true);
		Runtime->SetSmartObjectInteractionState(false);
		ShowStatus(FString::Printf(TEXT("SmartObject action rejected (allowed): %s"), *ActionFailureReason), FColor::Orange, 8.0f);
		CompleteCurrentStep();
		return true;
	}

	OutFailureReason = FString::Printf(TEXT("Scenario '%s' step[%d] required SmartObject action failed: %s"), *Config.TestId, ActiveStepIndex, *ActionFailureReason);
	return false;
}

bool FAINpcDataDrivenVisualScenarioTest::RunObserveHold(const FAINpcVisualScenarioStep& Step, FString& OutFailureReason)
{
	if (!IsKnownBooleanObservation(Step.Payload.Observation))
	{
		OutFailureReason = FString::Printf(TEXT("Scenario '%s' step[%d] observe.hold references unknown observation '%s'."), *Config.TestId, ActiveStepIndex, *Step.Payload.Observation);
		return false;
	}
	return true;
}

bool FAINpcDataDrivenVisualScenarioTest::IsAssertionSatisfied(const FAINpcVisualScenarioAssertion& Assertion) const
{
	if (Assertion.Operator == TEXT("all"))
	{
		for (const FAINpcVisualScenarioAssertion& Child : Assertion.Children) { if (!IsAssertionSatisfied(Child)) { return false; } }
		return true;
	}
	if (Assertion.Operator == TEXT("any") || Assertion.Operator == TEXT("anyOf"))
	{
		for (const FAINpcVisualScenarioAssertion& Child : Assertion.Children) { if (IsAssertionSatisfied(Child)) { return true; } }
		return false;
	}
	if (Assertion.Operator == TEXT("exists"))
	{
		return HasObservation(Assertion.Observation);
	}
	if (Assertion.Operator == TEXT("equals"))
	{
		if (Assertion.bHasEqualsBool)
		{
			bool Value = false;
			return TryGetObservationBool(Assertion.Observation, Value) && Value == Assertion.EqualsBool;
		}
		FString Value;
		return TryGetObservationString(Assertion.Observation, Value) && Value == Assertion.EqualsString;
	}
	bool Value = false;
	return TryGetObservationBool(Assertion.Observation, Value) && Value;
}

bool FAINpcDataDrivenVisualScenarioTest::TryGetObservationBool(const FString& Name, bool& OutValue) const
{
	if (const bool* Value = BoolObservations.Find(Name)) { OutValue = *Value; return true; }
	return false;
}

bool FAINpcDataDrivenVisualScenarioTest::TryGetObservationString(const FString& Name, FString& OutValue) const
{
	if (const FString* Value = StringObservations.Find(Name)) { OutValue = *Value; return true; }
	return false;
}

bool FAINpcDataDrivenVisualScenarioTest::HasObservation(const FString& Name) const
{
	const FAINpcVisualTestObservations Observations = BuildObservations();
	return Observations.BooleanFields.Contains(Name)
		|| Observations.IntegerFields.Contains(Name)
		|| Observations.NumberFields.Contains(Name)
		|| Observations.StringFields.Contains(Name);
}

FString FAINpcDataDrivenVisualScenarioTest::BuildPrompt(FString& OutFailureReason) const
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

void FAINpcDataDrivenVisualScenarioTest::HandleTimeout()
{
	if (!bComplete && !bFailed)
	{
		Fail(FString::Printf(TEXT("Timed out after %ds. Evidence={%s}. %s"), Config.TimeoutSec, *BuildSummary(), *AINpcDialogueVisualTestSupport::DescribeDialogueState(Runtime->GetNpcComponent())));
	}
}

void FAINpcDataDrivenVisualScenarioTest::UpdateDialogueStateEvidence()
{
	if (!Runtime->GetNpcComponent()) { return; }
	const ENpcDialogueState State = Runtime->GetNpcComponent()->GetDialogueState();
	Runtime->SetVisibleStateText(AINpcDialogueVisualTestSupport::GetDialogueStateText(Runtime->GetNpcComponent()));
	if (State == ENpcDialogueState::WaitingForLLM) { RecordBoolObservation(TEXT("waitingStateObserved"), true); }
	else if (State == ENpcDialogueState::Speaking) { RecordBoolObservation(TEXT("speakingStateObserved"), true); }
}

bool FAINpcDataDrivenVisualScenarioTest::ConfigurePersona(FString& OutFailureReason)
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
			const FGameplayTag RouteTag = FGameplayTag::RequestGameplayTag(FName(*Step.Payload.EventTag), false);
			if (!RouteTag.IsValid())
			{
				OutFailureReason = FString::Printf(TEXT("Scenario '%s' requires gameplay tag '%s'."), *Config.TestId, *Step.Payload.EventTag);
				return false;
			}
			Runtime->AddEventSubscriptionTag(RouteTag);
		}
	}
	Runtime->SetPersonaData(VisualHarnessPersona);
	return true;
}

void FAINpcDataDrivenVisualScenarioTest::Fail(const FString& Reason)
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

void FAINpcDataDrivenVisualScenarioTest::ShowStatus(const FString& Message, const FColor& Color, const float DurationSeconds) const
{
	UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);
	if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, DurationSeconds, Color, Message); }
}

void FAINpcDataDrivenVisualScenarioTest::RecordBoolObservation(const FString& Name, const bool bValue)
{
	BoolObservations.Add(Name, bValue);
}

void FAINpcDataDrivenVisualScenarioTest::RefreshActionTargetObservation()
{
	const bool bReached = Runtime->HasReachedVisualActionTarget();
	if (bReached) { RecordBoolObservation(TEXT("actionTargetReached"), true); }
}

void FAINpcDataDrivenVisualScenarioTest::CompleteCurrentStep()
{
	FinalizeActiveStepDiagnostic(TEXT("PASS"), FString());
	StartNextStep();
}

void FAINpcDataDrivenVisualScenarioTest::FinalizeActiveStepDiagnostic(const FString& Status, const FString& Reason)
{
	if (!StepDiagnostics.IsValidIndex(StepDiagnostics.Num() - 1)) { return; }
	FAINpcVisualTestStepDiagnostic& Diagnostic = StepDiagnostics.Last();
	if (Diagnostic.StepIndex != ActiveStepIndex || Diagnostic.Status != TEXT("running")) { return; }
	const double Now = Runtime->GetTimeSeconds(ActiveStepStartSeconds);
	Diagnostic.Status = Status;
	Diagnostic.FailureReason = Reason;
	Diagnostic.DurationMs = FMath::Max(0.0, Now - ActiveStepStartSeconds) * 1000.0;
}
