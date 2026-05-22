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
	const TCHAR* EventTriggerIdVariable = TEXT("EventTriggerId");
	const float SmartObjectSearchRadius = 1200.0f;
	const int32 SmartObjectClaimPriority = 2;

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
}

FAINpcDataDrivenVisualScenarioTest::FAINpcDataDrivenVisualScenarioTest(AAINpcTestCharacter& InNpc, AAINpcTestSmartObjectActor& InSmartObject, FAINpcVisualScenarioConfig InConfig)
	: Npc(InNpc)
	, SmartObject(InSmartObject)
	, Config(MoveTemp(InConfig))
{
}

FAINpcDataDrivenVisualScenarioTest::~FAINpcDataDrivenVisualScenarioTest()
{
	if (UWorld* World = Npc.GetWorld())
	{
		World->GetTimerManager().ClearTimer(StepTimerHandle);
		World->GetTimerManager().ClearTimer(TimeoutTimerHandle);
	}
	if (UAINpcComponent* NpcComponent = Npc.NpcComponent)
	{
		NpcComponent->OnDialogueSessionStartedNative().Remove(SessionStartedHandle);
		NpcComponent->OnDialogueResponseNative().Remove(ResponseHandle);
		NpcComponent->OnDialoguePartialResponseNative().Remove(PartialResponseHandle);
		NpcComponent->OnDialogueErrorNative().Remove(ErrorHandle);
		NpcComponent->OnDialogueSessionEndedNative().Remove(SessionEndedHandle);
		NpcComponent->OnDialogueDegradedNative().Remove(DegradedHandle);
		NpcComponent->OnDelayMaskingStartNative().Remove(DelayMaskingStartHandle);
		NpcComponent->OnDelayMaskingEndNative().Remove(DelayMaskingEndHandle);
	}
}

bool FAINpcDataDrivenVisualScenarioTest::Start(FString& OutFailureReason)
{
	if (!Npc.NpcComponent)
	{
		OutFailureReason = FString::Printf(TEXT("Visual scenario '%s' cannot start because NPC component is null."), *Config.TestId);
		return false;
	}
	if (!ConfigurePersona(OutFailureReason)) { return false; }
	if (!AINpcDialogueVisualTestSupport::ValidateProviderConfiguration(Npc.NpcComponent, OutFailureReason)) { return false; }

	BindNpcDelegates();
	bStarted = true;
	ShowStatus(FString::Printf(TEXT("Visual scenario '%s' ready. Starting DSL steps in %.0fs."), *Config.TestId, InitialDialogueDelaySeconds), FColor::Green, 15.0f);
	if (UWorld* World = Npc.GetWorld())
	{
		World->GetTimerManager().SetTimer(TimeoutTimerHandle, FTimerDelegate::CreateRaw(this, &FAINpcDataDrivenVisualScenarioTest::HandleTimeout), static_cast<float>(Config.TimeoutSec), false);
		World->GetTimerManager().SetTimer(StepTimerHandle, FTimerDelegate::CreateRaw(this, &FAINpcDataDrivenVisualScenarioTest::StartNextStep), InitialDialogueDelaySeconds, false);
		return true;
	}

	OutFailureReason = FString::Printf(TEXT("Visual scenario '%s' cannot start because World is null."), *Config.TestId);
	return false;
}

void FAINpcDataDrivenVisualScenarioTest::BindNpcDelegates()
{
	UAINpcComponent& NpcComponent = *Npc.NpcComponent;
	SessionStartedHandle = NpcComponent.OnDialogueSessionStartedNative().AddRaw(this, &FAINpcDataDrivenVisualScenarioTest::OnNpcSessionStarted);
	ResponseHandle = NpcComponent.OnDialogueResponseNative().AddRaw(this, &FAINpcDataDrivenVisualScenarioTest::OnNpcResponse);
	PartialResponseHandle = NpcComponent.OnDialoguePartialResponseNative().AddRaw(this, &FAINpcDataDrivenVisualScenarioTest::OnNpcPartialResponse);
	ErrorHandle = NpcComponent.OnDialogueErrorNative().AddRaw(this, &FAINpcDataDrivenVisualScenarioTest::OnNpcError);
	SessionEndedHandle = NpcComponent.OnDialogueSessionEndedNative().AddRaw(this, &FAINpcDataDrivenVisualScenarioTest::OnNpcSessionEnded);
	DegradedHandle = NpcComponent.OnDialogueDegradedNative().AddRaw(this, &FAINpcDataDrivenVisualScenarioTest::OnNpcDegraded);
	DelayMaskingStartHandle = NpcComponent.OnDelayMaskingStartNative().AddRaw(this, &FAINpcDataDrivenVisualScenarioTest::OnNpcDelayMaskingStart);
	DelayMaskingEndHandle = NpcComponent.OnDelayMaskingEndNative().AddRaw(this, &FAINpcDataDrivenVisualScenarioTest::OnNpcDelayMaskingEnd);
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
	Observations.IntegerFields.Add(TEXT("responseLength"), LastNpcResponseText.Len());
	Observations.IntegerFields.Add(TEXT("partialResponseLength"), LastPartialResponseText.Len());
	Observations.IntegerFields.Add(TEXT("delayFillerLength"), LastDelayFillerText.Len());
	Observations.NumberFields.Add(TEXT("distanceToActionTarget"), Npc.GetVisualActionTargetDistance());
	Observations.StringFields = StringObservations;
	Observations.StringFields.Add(TEXT("lastActionFailure"), LastActionFailureReason);
	return Observations;
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

	ActiveStepStartSeconds = Npc.GetWorld() ? Npc.GetWorld()->GetTimeSeconds() : 0.0;
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
		const double Now = Npc.GetWorld() ? Npc.GetWorld()->GetTimeSeconds() : ActiveStepStartSeconds;
		if (Now - ActiveStepStartSeconds > Step.Payload.TimeoutSec)
		{
			Fail(FString::Printf(TEXT("Scenario '%s' step[%d] wait.until timed out after %.1fs."), *Config.TestId, ActiveStepIndex, Step.Payload.TimeoutSec));
		}
	}
	else if (Step.Type == TEXT("observe.hold"))
	{
		const double Now = Npc.GetWorld() ? Npc.GetWorld()->GetTimeSeconds() : ActiveStepStartSeconds;
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
	if (!Npc.NpcComponent->StartDialogue(Prompt))
	{
		OutFailureReason = FString::Printf(TEXT("Scenario '%s' step[%d] StartDialogue rejected. %s"), *Config.TestId, ActiveStepIndex, *AINpcDialogueVisualTestSupport::DescribeDialogueState(Npc.NpcComponent));
		return false;
	}
	UpdateDialogueStateEvidence();
	CompleteCurrentStep();
	return true;
}

bool FAINpcDataDrivenVisualScenarioTest::RunWorldEvent(const FAINpcVisualScenarioStep& Step, FString& OutFailureReason)
{
	UWorld* World = Npc.GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UNpcEventSubsystem* EventSubsystem = GameInstance ? GameInstance->GetSubsystem<UNpcEventSubsystem>() : nullptr;
	if (!EventSubsystem)
	{
		OutFailureReason = FString::Printf(TEXT("Scenario '%s' step[%d] could not find UNpcEventSubsystem."), *Config.TestId, ActiveStepIndex);
		return false;
	}
	const FGameplayTag EventTag = FGameplayTag::RequestGameplayTag(FName(*Step.Payload.EventTag), false);
	if (!EventTag.IsValid())
	{
		OutFailureReason = FString::Printf(TEXT("Scenario '%s' step[%d] requires gameplay tag '%s'."), *Config.TestId, ActiveStepIndex, *Step.Payload.EventTag);
		return false;
	}
	FGameplayTagContainer RoutingTags;
	RoutingTags.AddTag(EventTag);
	const FNpcEventMessage EventMessage = UNpcEventPayloadBlueprintLibrary::MakeGiftEventMessage(EventTag, RoutingTags, nullptr, &Npc, FGameplayTag(), 1);
	RecordBoolObservation(TEXT("eventTriggerBroadcast"), true);
	ShowStatus(TEXT("Gameplay event trigger broadcast while provider request is in flight."), FColor::Orange, 7.0f);
	EventSubsystem->BroadcastEvent(EventMessage);
	CompleteCurrentStep();
	return true;
}

bool FAINpcDataDrivenVisualScenarioTest::RunActionExecuteLatestIntent(const FAINpcVisualScenarioStep& Step, FString& OutFailureReason)
{
	FAINpcVisualSmartObjectActionExecution Execution;
	FString ActionFailureReason;
	if (AINpcSmartObjectActionTestSupport::TryExecuteLatestActionIntent(Npc.NpcComponent, Npc, SmartObjectSearchRadius, SmartObjectClaimPriority, Execution, ActionFailureReason))
	{
		RecordBoolObservation(TEXT("actionExecutionAccepted"), true);
		SmartObject.SetInteractionState(true);
		Npc.BeginVisualActionMove(Execution.ClaimedSlotTransform, Execution.RequestedTarget);
		ShowStatus(FString::Printf(TEXT("SmartObject action accepted: %s -> %s."), *Execution.ActionType, *Execution.RequestedTarget), FColor::Green, 8.0f);
		CompleteCurrentStep();
		return true;
	}

	LastActionFailureReason = ActionFailureReason;
	if (Step.Payload.bAllowActionRejection)
	{
		RecordBoolObservation(TEXT("actionRejectedVisible"), true);
		SmartObject.SetInteractionState(false);
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
		return BoolObservations.Contains(Assertion.Observation) || StringObservations.Contains(Assertion.Observation);
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

FString FAINpcDataDrivenVisualScenarioTest::BuildPrompt(FString& OutFailureReason) const
{
	FString Prompt;
	if (!AINpcDialogueVisualTestSupport::LoadRequiredConfigText(*Config.Prompt.File, TEXT("prompt template"), Prompt, OutFailureReason)) { return FString(); }
	TMap<FString, FString> RuntimeVariables = Config.Prompt.Variables;
	if (RuntimeVariables.Contains(SmartObjectTargetIdVariable))
	{
		const TArray<FString> AvailableTargets = Npc.NpcComponent->GetAvailableSmartObjectTargetsForExecution();
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
	if (Prompt.Contains(TEXT("{")) && Prompt.Contains(TEXT("}")))
	{
		OutFailureReason = FString::Printf(TEXT("Scenario '%s' prompt file '%s' still contains unresolved placeholder after variable replacement."), *Config.TestId, *Config.Prompt.File);
		return FString();
	}
	return Prompt;
}

void FAINpcDataDrivenVisualScenarioTest::HandleTimeout()
{
	if (!bComplete && !bFailed)
	{
		Fail(FString::Printf(TEXT("Timed out after %ds. Evidence={%s}. %s"), Config.TimeoutSec, *BuildSummary(), *AINpcDialogueVisualTestSupport::DescribeDialogueState(Npc.NpcComponent)));
	}
}

void FAINpcDataDrivenVisualScenarioTest::UpdateDialogueStateEvidence()
{
	if (!Npc.NpcComponent) { return; }
	const ENpcDialogueState State = Npc.NpcComponent->GetDialogueState();
	Npc.SetVisibleStateText(AINpcDialogueVisualTestSupport::GetDialogueStateText(Npc.NpcComponent));
	if (State == ENpcDialogueState::WaitingForLLM) { RecordBoolObservation(TEXT("waitingStateObserved"), true); }
	else if (State == ENpcDialogueState::Speaking) { RecordBoolObservation(TEXT("speakingStateObserved"), true); }
}

bool FAINpcDataDrivenVisualScenarioTest::ConfigurePersona(FString& OutFailureReason)
{
	FString DelayFillerText;
	if (!AINpcDialogueVisualTestSupport::LoadRequiredConfigText(*Config.Persona.DelayFillerFile, TEXT("delay filler"), DelayFillerText, OutFailureReason)) { return false; }
	AINpcDialogueVisualTestSupport::FVisualHarnessPersonaText PersonaText;
	if (!AINpcDialogueVisualTestSupport::LoadPersonaText(*Config.Persona.File, PersonaText, OutFailureReason)) { return false; }

	VisualHarnessPersona = NewObject<UNpcPersonaDataAsset>(&Npc, *FString::Printf(TEXT("%sPersona"), *Config.TestId.Replace(TEXT("."), TEXT("_"))));
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
			Npc.NpcComponent->EventSubscriptionTags.AddTag(RouteTag);
		}
	}
	Npc.NpcComponent->SetPersonaData(VisualHarnessPersona);
	return true;
}

void FAINpcDataDrivenVisualScenarioTest::Fail(const FString& Reason)
{
	if (bComplete || bFailed) { return; }
	bFailed = true;
	FailureReason = Reason;
	if (UWorld* World = Npc.GetWorld())
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
	const bool bReached = Npc.HasReachedVisualActionTarget();
	if (bReached) { RecordBoolObservation(TEXT("actionTargetReached"), true); }
}

void FAINpcDataDrivenVisualScenarioTest::CompleteCurrentStep()
{
	StartNextStep();
}

void FAINpcDataDrivenVisualScenarioTest::OnNpcSessionStarted()
{
	RecordBoolObservation(TEXT("sessionStarted"), true);
	ShowStatus(TEXT("Dialogue session started through the real provider chain."), FColor::Green, 6.0f);
}

void FAINpcDataDrivenVisualScenarioTest::OnNpcResponse(const FString& Text)
{
	LastNpcResponseText = Text;
	LastNpcResponseText.TrimStartAndEndInline();
	if (LastNpcResponseText.IsEmpty())
	{
		Fail(TEXT("Provider returned an empty dialogue response."));
		return;
	}
	RecordBoolObservation(TEXT("dialogueResponseObserved"), true);
	RecordBoolObservation(TEXT("structuredResponseObserved"), true);
	FNpcAction LatestAction;
	if (Npc.NpcComponent && Npc.NpcComponent->TryGetLatestActionIntent(LatestAction)) { RecordBoolObservation(TEXT("actionIntentObserved"), true); }
	Npc.SetVisibleDialogueText(LastNpcResponseText);
	UpdateDialogueStateEvidence();
	ShowStatus(FString::Printf(TEXT("<<< NPC response received. Length=%d"), LastNpcResponseText.Len()), FColor::Yellow, 8.0f);
}

void FAINpcDataDrivenVisualScenarioTest::OnNpcPartialResponse(const FString& Text)
{
	if (!Text.IsEmpty())
	{
		LastPartialResponseText = Text;
		RecordBoolObservation(TEXT("partialResponseObserved"), true);
	}
	ShowStatus(FString::Printf(TEXT("[stream] partial response. Length=%d"), Text.Len()), FColor::Orange, 3.0f);
}

void FAINpcDataDrivenVisualScenarioTest::OnNpcError(const FString& ErrorMessage)
{
	Fail(FString::Printf(TEXT("Provider chain reported an error. Length=%d"), ErrorMessage.Len()));
}

void FAINpcDataDrivenVisualScenarioTest::OnNpcSessionEnded()
{
	UpdateDialogueStateEvidence();
}

void FAINpcDataDrivenVisualScenarioTest::OnNpcDegraded(const FString& FallbackResponse, const FString& DegradedFailureReason)
{
	(void)FallbackResponse;
	Fail(FString::Printf(TEXT("Provider chain degraded. ReasonLength=%d"), DegradedFailureReason.Len()));
}

void FAINpcDataDrivenVisualScenarioTest::OnNpcDelayMaskingStart(UAnimMontage* Montage, const FText& FillerText)
{
	(void)Montage;
	LastDelayFillerText = FillerText.ToString();
	RecordBoolObservation(TEXT("delayMaskingStartObserved"), true);
	if (BoolObservations.Contains(TEXT("eventTriggerBroadcast"))) { RecordBoolObservation(TEXT("eventDelayMaskingStartObserved"), true); }
	ShowStatus(FString::Printf(TEXT("Delay masking started. FillerLength=%d"), LastDelayFillerText.Len()), FColor::Purple, 5.0f);
}

void FAINpcDataDrivenVisualScenarioTest::OnNpcDelayMaskingEnd()
{
	RecordBoolObservation(TEXT("delayMaskingEndObserved"), true);
	ShowStatus(TEXT("Delay masking ended."), FColor::Purple, 3.0f);
}
