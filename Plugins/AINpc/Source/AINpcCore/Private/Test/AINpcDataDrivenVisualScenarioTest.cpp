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
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "SmartObjectBridge/SmartObjectBridgeContext.h"
#include "Test/AINpcDialogueVisualTestSupport.h"
#include "Test/AINpcSmartObjectActionTestSupport.h"
#include "Test/AINpcTestCharacter.h"
#include "Test/AINpcTestSmartObjectActor.h"
#include "TimerManager.h"

namespace
{
	const float InitialDialogueDelaySeconds = 3.0f;
	const float VerificationTimeoutSeconds = 75.0f;
	const float SmartObjectSearchRadius = 1200.0f;
	const int32 SmartObjectClaimPriority = 2;
	const TCHAR* SmartObjectTargetIdPlaceholder = TEXT("{SmartObjectTargetId}");
	const TCHAR* EventTriggerIdPlaceholder = TEXT("{EventTriggerId}");
}

FAINpcDataDrivenVisualScenarioTest::FAINpcDataDrivenVisualScenarioTest(
	AAINpcTestCharacter& InNpc,
	AAINpcTestSmartObjectActor& InSmartObject,
	FAINpcVisualScenarioConfig InConfig)
	: Npc(InNpc)
	, SmartObject(InSmartObject)
	, Config(MoveTemp(InConfig))
{
}

FAINpcDataDrivenVisualScenarioTest::~FAINpcDataDrivenVisualScenarioTest()
{
	if (UWorld* World = Npc.GetWorld())
	{
		World->GetTimerManager().ClearTimer(DialogueTimerHandle);
		World->GetTimerManager().ClearTimer(TimeoutTimerHandle);
		World->GetTimerManager().ClearTimer(ActionObservationHoldTimerHandle);
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

	float RequestedHoldSeconds = 3.0f;
	FParse::Value(FCommandLine::Get(), TEXT("AINpcActionObservationHoldSeconds="), RequestedHoldSeconds);
	ActionObservationHoldSeconds = FMath::Max(0.0f, RequestedHoldSeconds);

	if (!ConfigurePersona(OutFailureReason))
	{
		return false;
	}

	if (!AINpcDialogueVisualTestSupport::ValidateProviderConfiguration(Npc.NpcComponent, OutFailureReason))
	{
		return false;
	}

	UAINpcComponent& NpcComponent = *Npc.NpcComponent;
	SessionStartedHandle = NpcComponent.OnDialogueSessionStartedNative().AddRaw(this, &FAINpcDataDrivenVisualScenarioTest::OnNpcSessionStarted);
	ResponseHandle = NpcComponent.OnDialogueResponseNative().AddRaw(this, &FAINpcDataDrivenVisualScenarioTest::OnNpcResponse);
	PartialResponseHandle = NpcComponent.OnDialoguePartialResponseNative().AddRaw(this, &FAINpcDataDrivenVisualScenarioTest::OnNpcPartialResponse);
	ErrorHandle = NpcComponent.OnDialogueErrorNative().AddRaw(this, &FAINpcDataDrivenVisualScenarioTest::OnNpcError);
	SessionEndedHandle = NpcComponent.OnDialogueSessionEndedNative().AddRaw(this, &FAINpcDataDrivenVisualScenarioTest::OnNpcSessionEnded);
	DegradedHandle = NpcComponent.OnDialogueDegradedNative().AddRaw(this, &FAINpcDataDrivenVisualScenarioTest::OnNpcDegraded);
	DelayMaskingStartHandle = NpcComponent.OnDelayMaskingStartNative().AddRaw(this, &FAINpcDataDrivenVisualScenarioTest::OnNpcDelayMaskingStart);
	DelayMaskingEndHandle = NpcComponent.OnDelayMaskingEndNative().AddRaw(this, &FAINpcDataDrivenVisualScenarioTest::OnNpcDelayMaskingEnd);

	UWorld* World = Npc.GetWorld();
	if (!World)
	{
		OutFailureReason = FString::Printf(TEXT("Visual scenario '%s' cannot start because World is null."), *Config.TestId);
		return false;
	}

	bStarted = true;
	ShowStatus(FString::Printf(TEXT("Visual scenario '%s' ready. Starting in %.0fs."), *Config.TestId, InitialDialogueDelaySeconds), FColor::Green, 15.0f);
	World->GetTimerManager().SetTimer(TimeoutTimerHandle, FTimerDelegate::CreateRaw(this, &FAINpcDataDrivenVisualScenarioTest::HandleTimeout), VerificationTimeoutSeconds, false);
	World->GetTimerManager().SetTimer(DialogueTimerHandle, FTimerDelegate::CreateRaw(this, &FAINpcDataDrivenVisualScenarioTest::StartDialogue), InitialDialogueDelaySeconds, false);
	return true;
}

void FAINpcDataDrivenVisualScenarioTest::Poll()
{
	if (!bStarted || bComplete || bFailed)
	{
		return;
	}

	UpdateDialogueStateEvidence();
	const bool bWasActionTargetReached = bActionTargetReached;
	bActionTargetReached = Npc.HasReachedVisualActionTarget();
	if (!bWasActionTargetReached && bActionTargetReached && !bActionObservationHoldStarted)
	{
		bActionObservationHoldStarted = true;
		if (ActionObservationHoldSeconds <= 0.0f)
		{
			bActionObservationHoldElapsed = true;
		}
		else if (UWorld* World = Npc.GetWorld())
		{
			World->GetTimerManager().SetTimer(ActionObservationHoldTimerHandle, FTimerDelegate::CreateRaw(this, &FAINpcDataDrivenVisualScenarioTest::MarkActionObservationHoldElapsed), ActionObservationHoldSeconds, false);
		}
	}

	if (HasRequiredEvidence())
	{
		bComplete = true;
	}
}

bool FAINpcDataDrivenVisualScenarioTest::IsComplete() const { return bComplete; }
bool FAINpcDataDrivenVisualScenarioTest::HasFailed() const { return bFailed; }
const FString& FAINpcDataDrivenVisualScenarioTest::GetFailureReason() const { return FailureReason; }

FString FAINpcDataDrivenVisualScenarioTest::BuildSummary() const
{
	return FString::Printf(
		TEXT("TestId=%s SessionStarted=%s Waiting=%s Speaking=%s Response=%s Partial=%s Structured=%s ActionIntent=%s EventTrigger=%s EventDelay=%s DelayStart=%s DelayEnd=%s ActionAccepted=%s ActionRejected=%s ActionReached=%s HoldElapsed=%s"),
		*Config.TestId,
		bDialogueSessionStartedObserved ? TEXT("true") : TEXT("false"),
		bWaitingStateObserved ? TEXT("true") : TEXT("false"),
		bSpeakingStateObserved ? TEXT("true") : TEXT("false"),
		bDialogueResponseObserved ? TEXT("true") : TEXT("false"),
		bPartialResponseObserved ? TEXT("true") : TEXT("false"),
		bStructuredResponseObserved ? TEXT("true") : TEXT("false"),
		bActionIntentObserved ? TEXT("true") : TEXT("false"),
		bEventTriggerBroadcastObserved ? TEXT("true") : TEXT("false"),
		bEventDelayMaskingStartObserved ? TEXT("true") : TEXT("false"),
		bDelayMaskingStartObserved ? TEXT("true") : TEXT("false"),
		bDelayMaskingEndObserved ? TEXT("true") : TEXT("false"),
		bActionExecutionAccepted ? TEXT("true") : TEXT("false"),
		bActionRejectedVisible ? TEXT("true") : TEXT("false"),
		bActionTargetReached ? TEXT("true") : TEXT("false"),
		bActionObservationHoldElapsed ? TEXT("true") : TEXT("false"));
}

FAINpcVisualTestObservations FAINpcDataDrivenVisualScenarioTest::BuildObservations() const
{
	FAINpcVisualTestObservations Observations;
	Observations.BooleanFields.Add(TEXT("sessionStarted"), bDialogueSessionStartedObserved);
	Observations.BooleanFields.Add(TEXT("waitingStateObserved"), bWaitingStateObserved);
	Observations.BooleanFields.Add(TEXT("speakingStateObserved"), bSpeakingStateObserved);
	Observations.BooleanFields.Add(TEXT("dialogueResponseObserved"), bDialogueResponseObserved);
	Observations.BooleanFields.Add(TEXT("partialResponseObserved"), bPartialResponseObserved);
	Observations.BooleanFields.Add(TEXT("structuredResponseObserved"), bStructuredResponseObserved);
	Observations.BooleanFields.Add(TEXT("actionIntentObserved"), bActionIntentObserved);
	Observations.BooleanFields.Add(TEXT("eventTriggerBroadcast"), bEventTriggerBroadcastObserved);
	Observations.BooleanFields.Add(TEXT("eventDelayMaskingStartObserved"), bEventDelayMaskingStartObserved);
	Observations.BooleanFields.Add(TEXT("delayMaskingStartObserved"), bDelayMaskingStartObserved);
	Observations.BooleanFields.Add(TEXT("delayMaskingEndObserved"), bDelayMaskingEndObserved);
	Observations.BooleanFields.Add(TEXT("actionExecutionAccepted"), bActionExecutionAccepted);
	Observations.BooleanFields.Add(TEXT("actionRejectedVisible"), bActionRejectedVisible);
	Observations.BooleanFields.Add(TEXT("actionTargetReached"), bActionTargetReached);
	Observations.BooleanFields.Add(TEXT("actionObservationHoldElapsed"), bActionObservationHoldElapsed);
	Observations.IntegerFields.Add(TEXT("responseLength"), LastNpcResponseText.Len());
	Observations.IntegerFields.Add(TEXT("partialResponseLength"), LastPartialResponseText.Len());
	Observations.IntegerFields.Add(TEXT("delayFillerLength"), LastDelayFillerText.Len());
	Observations.NumberFields.Add(TEXT("distanceToActionTarget"), Npc.GetVisualActionTargetDistance());
	Observations.StringFields.Add(TEXT("lastActionFailure"), LastActionFailureReason);
	return Observations;
}

void FAINpcDataDrivenVisualScenarioTest::StartDialogue()
{
	if (bComplete || bFailed || !Npc.NpcComponent)
	{
		return;
	}

	const TArray<FString> AvailableTargets = Npc.NpcComponent->GetAvailableSmartObjectTargetsForExecution();
	if (AvailableTargets.IsEmpty())
	{
		Fail(TEXT("Runtime SmartObject target list was empty before prompt construction."));
		return;
	}

	FString Prompt;
	FString PromptFailureReason;
	if (!AINpcDialogueVisualTestSupport::LoadRequiredConfigText(*Config.PromptFile, TEXT("prompt template"), Prompt, PromptFailureReason))
	{
		Fail(PromptFailureReason);
		return;
	}

	if (!Prompt.Contains(SmartObjectTargetIdPlaceholder))
	{
		Fail(FString::Printf(TEXT("Prompt template '%s' is missing placeholder %s."), *Config.PromptFile, SmartObjectTargetIdPlaceholder));
		return;
	}

	Prompt = Prompt.Replace(SmartObjectTargetIdPlaceholder, *AvailableTargets[0], ESearchCase::CaseSensitive);

	if (Config.bRequireEventTrigger && !Config.EventTriggerId.IsEmpty())
	{
		if (!Prompt.Contains(EventTriggerIdPlaceholder))
		{
			Fail(FString::Printf(TEXT("Prompt template '%s' is missing placeholder %s."), *Config.PromptFile, EventTriggerIdPlaceholder));
			return;
		}
		Prompt = Prompt.Replace(EventTriggerIdPlaceholder, *Config.EventTriggerId, ESearchCase::CaseSensitive);
	}

	ShowStatus(FString::Printf(TEXT(">>> %s"), *Prompt.Left(220)), FColor::Cyan, 10.0f);
	if (!Npc.NpcComponent->StartDialogue(Prompt))
	{
		Fail(FString::Printf(TEXT("StartDialogue rejected. %s"), *AINpcDialogueVisualTestSupport::DescribeDialogueState(Npc.NpcComponent)));
		return;
	}

	UpdateDialogueStateEvidence();

	if (Config.bRequireEventTrigger)
	{
		BroadcastEventTrigger();
	}
}

void FAINpcDataDrivenVisualScenarioTest::BroadcastEventTrigger()
{
	if (bComplete || bFailed || !Npc.NpcComponent || Config.EventTag.IsEmpty())
	{
		return;
	}

	UWorld* World = Npc.GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UNpcEventSubsystem* EventSubsystem = GameInstance ? GameInstance->GetSubsystem<UNpcEventSubsystem>() : nullptr;
	if (!EventSubsystem)
	{
		Fail(TEXT("Event trigger could not find UNpcEventSubsystem."));
		return;
	}

	const FGameplayTag EventTag = FGameplayTag::RequestGameplayTag(FName(*Config.EventTag), false);
	if (!EventTag.IsValid())
	{
		Fail(FString::Printf(TEXT("Event trigger requires gameplay tag '%s'."), *Config.EventTag));
		return;
	}

	FGameplayTagContainer RoutingTags;
	RoutingTags.AddTag(EventTag);
	const FNpcEventMessage EventMessage = UNpcEventPayloadBlueprintLibrary::MakeGiftEventMessage(
		EventTag, RoutingTags, nullptr, &Npc, FGameplayTag(), 1);

	bEventTriggerBroadcastObserved = true;
	ShowStatus(TEXT("Gameplay event trigger broadcast while provider request is in flight."), FColor::Orange, 7.0f);
	EventSubsystem->BroadcastEvent(EventMessage);
}

void FAINpcDataDrivenVisualScenarioTest::HandleTimeout()
{
	if (bComplete || bFailed)
	{
		return;
	}

	Fail(FString::Printf(TEXT("Timed out after %.1fs. Evidence={%s}. %s"),
		VerificationTimeoutSeconds, *BuildSummary(),
		*AINpcDialogueVisualTestSupport::DescribeDialogueState(Npc.NpcComponent)));
}

void FAINpcDataDrivenVisualScenarioTest::MarkActionObservationHoldElapsed()
{
	bActionObservationHoldElapsed = true;
	Poll();
}

void FAINpcDataDrivenVisualScenarioTest::UpdateDialogueStateEvidence()
{
	if (!Npc.NpcComponent)
	{
		return;
	}

	const ENpcDialogueState State = Npc.NpcComponent->GetDialogueState();
	Npc.SetVisibleStateText(AINpcDialogueVisualTestSupport::GetDialogueStateText(Npc.NpcComponent));

	if (State == ENpcDialogueState::WaitingForLLM)
	{
		bWaitingStateObserved = true;
	}
	else if (State == ENpcDialogueState::Speaking)
	{
		bSpeakingStateObserved = true;
	}
}

bool FAINpcDataDrivenVisualScenarioTest::HasRequiredEvidence() const
{
	if (!bDialogueSessionStartedObserved || !bWaitingStateObserved || !bSpeakingStateObserved)
	{
		return false;
	}

	if (Config.bRequireEventTrigger && !bEventTriggerBroadcastObserved)
	{
		return false;
	}

	if (Config.bRequirePartialResponse && !bPartialResponseObserved)
	{
		return false;
	}

	if (Config.bRequireStructuredResponse)
	{
		if (!bStructuredResponseObserved)
		{
			return false;
		}
	}
	else
	{
		if (!bDialogueResponseObserved)
		{
			return false;
		}
	}

	if (Config.bRequireActionIntent && !bActionIntentObserved)
	{
		return false;
	}

	if (!bDelayMaskingEndObserved)
	{
		return false;
	}

	if (Config.bAllowActionRejection)
	{
		if (bActionRejectedVisible)
		{
			return true;
		}
	}

	return bActionExecutionAccepted && bActionTargetReached && bActionObservationHoldElapsed;
}

bool FAINpcDataDrivenVisualScenarioTest::ConfigurePersona(FString& OutFailureReason)
{
	FString DelayFillerText;
	if (!AINpcDialogueVisualTestSupport::LoadRequiredConfigText(*Config.DelayFillerFile, TEXT("delay filler"), DelayFillerText, OutFailureReason))
	{
		return false;
	}

	AINpcDialogueVisualTestSupport::FVisualHarnessPersonaText PersonaText;
	if (!AINpcDialogueVisualTestSupport::LoadPersonaText(*Config.PersonaFile, PersonaText, OutFailureReason))
	{
		return false;
	}

	VisualHarnessPersona = NewObject<UNpcPersonaDataAsset>(&Npc, *FString::Printf(TEXT("%sPersona"), *Config.TestId.Replace(TEXT("."), TEXT("_"))));
	if (!VisualHarnessPersona)
	{
		OutFailureReason = FString::Printf(TEXT("Failed to allocate persona for scenario '%s'."), *Config.TestId);
		return false;
	}

	VisualHarnessPersona->PersonaName = PersonaText.PersonaName;
	VisualHarnessPersona->Background = PersonaText.Background;
	VisualHarnessPersona->SpeakingStyle = PersonaText.SpeakingStyle;
	VisualHarnessPersona->DelayFillerThreshold = Config.DelayFillerThreshold;
	VisualHarnessPersona->DelayFillerTexts.Add(FText::FromString(DelayFillerText));

	if (Config.bRequireEventTrigger && !Config.EventTag.IsEmpty())
	{
		if (Config.DelayFillerThreshold > 0.0f)
		{
			VisualHarnessPersona->InspectDelayMaskingMontages.Add(NewObject<UAnimMontage>(VisualHarnessPersona));
		}

		const FGameplayTag RouteTag = FGameplayTag::RequestGameplayTag(FName(*Config.EventTag), false);
		if (!RouteTag.IsValid())
		{
			OutFailureReason = FString::Printf(TEXT("Scenario '%s' requires gameplay tag '%s'."), *Config.TestId, *Config.EventTag);
			return false;
		}
		Npc.NpcComponent->EventSubscriptionTags.AddTag(RouteTag);
	}

	Npc.NpcComponent->SetPersonaData(VisualHarnessPersona);
	return true;
}

void FAINpcDataDrivenVisualScenarioTest::Fail(const FString& Reason)
{
	if (bComplete || bFailed)
	{
		return;
	}

	bFailed = true;
	FailureReason = Reason;
	if (UWorld* World = Npc.GetWorld())
	{
		World->GetTimerManager().ClearTimer(DialogueTimerHandle);
		World->GetTimerManager().ClearTimer(TimeoutTimerHandle);
		World->GetTimerManager().ClearTimer(ActionObservationHoldTimerHandle);
	}
}

void FAINpcDataDrivenVisualScenarioTest::ShowStatus(const FString& Message, const FColor& Color, const float DurationSeconds) const
{
	UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, DurationSeconds, Color, Message);
	}
}

void FAINpcDataDrivenVisualScenarioTest::OnNpcSessionStarted()
{
	bDialogueSessionStartedObserved = true;
	ShowStatus(TEXT("Dialogue session started through the real provider chain."), FColor::Green, 6.0f);
}

void FAINpcDataDrivenVisualScenarioTest::OnNpcResponse(const FString& Text)
{
	LastNpcResponseText = Text;
	LastNpcResponseText.TrimStartAndEndInline();

	if (Config.bRequireStructuredResponse)
	{
		bStructuredResponseObserved = !LastNpcResponseText.IsEmpty();
		if (!bStructuredResponseObserved)
		{
			Fail(TEXT("Provider returned an empty structured dialogue response."));
			return;
		}

		FNpcAction LatestAction;
		bActionIntentObserved = Npc.NpcComponent && Npc.NpcComponent->TryGetLatestActionIntent(LatestAction);
		if (Config.bRequireActionIntent && !bActionIntentObserved)
		{
			Fail(TEXT("Structured provider response did not produce an action intent."));
			return;
		}
	}
	else
	{
		bDialogueResponseObserved = !LastNpcResponseText.IsEmpty();
		if (!bDialogueResponseObserved)
		{
			Fail(TEXT("Provider returned an empty dialogue response."));
			return;
		}
	}

	Npc.SetVisibleDialogueText(LastNpcResponseText);
	UpdateDialogueStateEvidence();
	ShowStatus(FString::Printf(TEXT("<<< NPC response received. Length=%d"), LastNpcResponseText.Len()), FColor::Yellow, 8.0f);

	FAINpcVisualSmartObjectActionExecution Execution;
	FString ActionFailureReason;
	if (AINpcSmartObjectActionTestSupport::TryExecuteLatestActionIntent(Npc.NpcComponent, Npc, SmartObjectSearchRadius, SmartObjectClaimPriority, Execution, ActionFailureReason))
	{
		bActionExecutionAccepted = true;
		SmartObject.SetInteractionState(true);
		Npc.BeginVisualActionMove(Execution.ClaimedSlotTransform, Execution.RequestedTarget);
		ShowStatus(FString::Printf(TEXT("SmartObject action accepted: %s -> %s. Hold=%.1fs."), *Execution.ActionType, *Execution.RequestedTarget, ActionObservationHoldSeconds), FColor::Green, 8.0f);
	}
	else
	{
		LastActionFailureReason = ActionFailureReason;
		if (Config.bAllowActionRejection)
		{
			bActionRejectedVisible = true;
			SmartObject.SetInteractionState(false);
			ShowStatus(FString::Printf(TEXT("SmartObject action rejected (allowed): %s"), *ActionFailureReason), FColor::Orange, 8.0f);
		}
		else
		{
			Fail(FString::Printf(TEXT("Required SmartObject action failed: %s"), *ActionFailureReason));
		}
	}
}

void FAINpcDataDrivenVisualScenarioTest::OnNpcPartialResponse(const FString& Text)
{
	if (!Text.IsEmpty())
	{
		LastPartialResponseText = Text;
		bPartialResponseObserved = true;
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
	Fail(FString::Printf(TEXT("Provider chain degraded. ReasonLength=%d"), DegradedFailureReason.Len()));
}

void FAINpcDataDrivenVisualScenarioTest::OnNpcDelayMaskingStart(UAnimMontage* Montage, const FText& FillerText)
{
	LastDelayFillerText = FillerText.ToString();
	bDelayMaskingStartObserved = true;
	if (Config.bRequireEventTrigger)
	{
		bEventDelayMaskingStartObserved = true;
	}
	ShowStatus(FString::Printf(TEXT("Delay masking started. FillerLength=%d"), LastDelayFillerText.Len()), FColor::Purple, 5.0f);
}

void FAINpcDataDrivenVisualScenarioTest::OnNpcDelayMaskingEnd()
{
	bDelayMaskingEndObserved = true;
	ShowStatus(TEXT("Delay masking ended."), FColor::Purple, 3.0f);
}
