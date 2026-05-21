#include "Test/AINpcUs2PerceptionBehaviorVisualTest.h"

#include "Animation/AnimMontage.h"
#include "Data/NpcPersonaDataAsset.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Events/NpcEventPayloadBlueprintLibrary.h"
#include "Events/NpcEventSubsystem.h"
#include "GameplayTagContainer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "SmartObjectBridge/SmartObjectBridgeContext.h"
#include "Test/AINpcDialogueVisualTestSupport.h"
#include "Test/AINpcSmartObjectActionTestSupport.h"
#include "Test/AINpcTestCharacter.h"
#include "Test/AINpcTestSmartObjectActor.h"
#include "TimerManager.h"

namespace
{
	const float StartDialogueDelaySeconds = 3.0f;
	const float VerificationTimeoutSeconds = 75.0f;
	const float DefaultActionObservationHoldSeconds = 3.0f;
	const float SmartObjectSearchRadius = 1200.0f;
	const int32 SmartObjectClaimPriority = 2;
	const TCHAR* Us2PromptTemplateFileName = TEXT("AINpcVisualHarnessUs2Prompt.txt");
	const TCHAR* DelayFillerFileName = TEXT("AINpcVisualHarnessDelayFiller.txt");
	const TCHAR* PersonaFileName = TEXT("AINpcVisualHarnessUs2Persona.txt");
	const TCHAR* SmartObjectTargetIdPlaceholder = TEXT("{SmartObjectTargetId}");
	const TCHAR* EventTriggerIdPlaceholder = TEXT("{EventTriggerId}");

	FGameplayTag RequestOptionalGameplayTag(const TCHAR* TagName)
	{
		return FGameplayTag::RequestGameplayTag(FName(TagName), false);
	}
}

FAINpcUs2PerceptionBehaviorVisualTest::FAINpcUs2PerceptionBehaviorVisualTest(
	AAINpcTestCharacter& InNpc,
	AAINpcTestSmartObjectActor& InSmartObject)
	: Npc(InNpc)
	, SmartObject(InSmartObject)
{
}

FAINpcUs2PerceptionBehaviorVisualTest::~FAINpcUs2PerceptionBehaviorVisualTest()
{
	if (UWorld* World = Npc.GetWorld())
	{
		World->GetTimerManager().ClearTimer(StartDialogueTimerHandle);
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

bool FAINpcUs2PerceptionBehaviorVisualTest::Start(FString& OutFailureReason)
{
	if (!Npc.NpcComponent)
	{
		OutFailureReason = TEXT("US-2 test cannot start because NPC component is null.");
		return false;
	}

	float RequestedHoldSeconds = DefaultActionObservationHoldSeconds;
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
	SessionStartedHandle = NpcComponent.OnDialogueSessionStartedNative().AddRaw(this, &FAINpcUs2PerceptionBehaviorVisualTest::OnNpcSessionStarted);
	ResponseHandle = NpcComponent.OnDialogueResponseNative().AddRaw(this, &FAINpcUs2PerceptionBehaviorVisualTest::OnNpcResponse);
	PartialResponseHandle = NpcComponent.OnDialoguePartialResponseNative().AddRaw(this, &FAINpcUs2PerceptionBehaviorVisualTest::OnNpcPartialResponse);
	ErrorHandle = NpcComponent.OnDialogueErrorNative().AddRaw(this, &FAINpcUs2PerceptionBehaviorVisualTest::OnNpcError);
	SessionEndedHandle = NpcComponent.OnDialogueSessionEndedNative().AddRaw(this, &FAINpcUs2PerceptionBehaviorVisualTest::OnNpcSessionEnded);
	DegradedHandle = NpcComponent.OnDialogueDegradedNative().AddRaw(this, &FAINpcUs2PerceptionBehaviorVisualTest::OnNpcDegraded);
	DelayMaskingStartHandle = NpcComponent.OnDelayMaskingStartNative().AddRaw(this, &FAINpcUs2PerceptionBehaviorVisualTest::OnNpcDelayMaskingStart);
	DelayMaskingEndHandle = NpcComponent.OnDelayMaskingEndNative().AddRaw(this, &FAINpcUs2PerceptionBehaviorVisualTest::OnNpcDelayMaskingEnd);

	UWorld* World = Npc.GetWorld();
	if (!World)
	{
		OutFailureReason = TEXT("US-2 test cannot start because World is null.");
		return false;
	}

	bStarted = true;
	ShowStatus(TEXT("US-2 visual harness ready. Real configured provider dialogue starts in 3 seconds; gameplay event follows while waiting."), FColor::Green, 15.0f);
	World->GetTimerManager().SetTimer(TimeoutTimerHandle, FTimerDelegate::CreateRaw(this, &FAINpcUs2PerceptionBehaviorVisualTest::HandleTimeout), VerificationTimeoutSeconds, false);
	World->GetTimerManager().SetTimer(StartDialogueTimerHandle, FTimerDelegate::CreateRaw(this, &FAINpcUs2PerceptionBehaviorVisualTest::StartEventDialogue), StartDialogueDelaySeconds, false);
	return true;
}

void FAINpcUs2PerceptionBehaviorVisualTest::Poll()
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
			World->GetTimerManager().SetTimer(ActionObservationHoldTimerHandle, FTimerDelegate::CreateRaw(this, &FAINpcUs2PerceptionBehaviorVisualTest::MarkActionObservationHoldElapsed), ActionObservationHoldSeconds, false);
		}
	}

	if (HasRequiredEvidence())
	{
		bComplete = true;
	}
}

bool FAINpcUs2PerceptionBehaviorVisualTest::IsComplete() const
{
	return bComplete;
}

bool FAINpcUs2PerceptionBehaviorVisualTest::HasFailed() const
{
	return bFailed;
}

const FString& FAINpcUs2PerceptionBehaviorVisualTest::GetFailureReason() const
{
	return FailureReason;
}

FString FAINpcUs2PerceptionBehaviorVisualTest::BuildSummary() const
{
	return FString::Printf(
		TEXT("SessionStarted=%s WaitingState=%s SpeakingState=%s EventTrigger=%s EventDelayMaskingStart=%s DelayEnd=%s PartialObserved=%s StructuredResponseObserved=%s ActionIntentObserved=%s ActionAccepted=%s ActionRejectedVisible=%s ActionTargetReached=%s ActionObservationHoldElapsed=%s ResponseLen=%d PartialLen=%d DelayTextLen=%d LastActionFailure=%s DistanceToActionTarget=%.1f"),
		bDialogueSessionStartedObserved ? TEXT("true") : TEXT("false"),
		bWaitingStateObserved ? TEXT("true") : TEXT("false"),
		bSpeakingStateObserved ? TEXT("true") : TEXT("false"),
		bEventTriggerBroadcastObserved ? TEXT("true") : TEXT("false"),
		bEventDelayMaskingStartObserved ? TEXT("true") : TEXT("false"),
		bDelayMaskingEndObserved ? TEXT("true") : TEXT("false"),
		bPartialResponseObserved ? TEXT("true") : TEXT("false"),
		bStructuredResponseObserved ? TEXT("true") : TEXT("false"),
		bActionIntentObserved ? TEXT("true") : TEXT("false"),
		bActionExecutionAccepted ? TEXT("true") : TEXT("false"),
		bActionRejectedVisible ? TEXT("true") : TEXT("false"),
		bActionTargetReached ? TEXT("true") : TEXT("false"),
		bActionObservationHoldElapsed ? TEXT("true") : TEXT("false"),
		LastNpcResponseText.Len(),
		LastPartialResponseText.Len(),
		LastDelayFillerText.Len(),
		LastActionFailureReason.IsEmpty() ? TEXT("<none>") : *LastActionFailureReason,
		Npc.GetVisualActionTargetDistance());
}

FAINpcVisualTestObservations FAINpcUs2PerceptionBehaviorVisualTest::BuildObservations() const
{
	FAINpcVisualTestObservations Observations;
	Observations.BooleanFields.Add(TEXT("sessionStarted"), bDialogueSessionStartedObserved);
	Observations.BooleanFields.Add(TEXT("waitingStateObserved"), bWaitingStateObserved);
	Observations.BooleanFields.Add(TEXT("speakingStateObserved"), bSpeakingStateObserved);
	Observations.BooleanFields.Add(TEXT("eventTriggerBroadcast"), bEventTriggerBroadcastObserved);
	Observations.BooleanFields.Add(TEXT("eventDelayMaskingStartObserved"), bEventDelayMaskingStartObserved);
	Observations.BooleanFields.Add(TEXT("delayMaskingEndObserved"), bDelayMaskingEndObserved);
	Observations.BooleanFields.Add(TEXT("partialResponseObserved"), bPartialResponseObserved);
	Observations.BooleanFields.Add(TEXT("structuredResponseObserved"), bStructuredResponseObserved);
	Observations.BooleanFields.Add(TEXT("actionIntentObserved"), bActionIntentObserved);
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

void FAINpcUs2PerceptionBehaviorVisualTest::StartEventDialogue()
{
	if (bComplete || bFailed || !Npc.NpcComponent)
	{
		return;
	}

	const TArray<FString> AvailableTargets = Npc.NpcComponent->GetAvailableSmartObjectTargetsForExecution();
	if (AvailableTargets.IsEmpty())
	{
		Fail(TEXT("Runtime SmartObject target list was empty before US-2 prompt construction."));
		return;
	}

	FString EventPrompt;
	FString PromptFailureReason;
	if (!LoadEventPrompt(EventPrompt, PromptFailureReason))
	{
		Fail(PromptFailureReason);
		return;
	}

	const FString EventTriggerId = TEXT("US2_EVENT_TRIGGER_GIFT_001");
	EventPrompt = EventPrompt.Replace(SmartObjectTargetIdPlaceholder, *AvailableTargets[0], ESearchCase::CaseSensitive);
	EventPrompt = EventPrompt.Replace(EventTriggerIdPlaceholder, *EventTriggerId, ESearchCase::CaseSensitive);

	ShowStatus(FString::Printf(TEXT(">>> US-2 Player/Event prompt: %s"), *EventPrompt.Left(220)), FColor::Cyan, 10.0f);
	if (!Npc.NpcComponent->StartDialogue(EventPrompt))
	{
		Fail(FString::Printf(TEXT("US-2 StartDialogue was rejected before real provider/event verification. %s"), *AINpcDialogueVisualTestSupport::DescribeDialogueState(Npc.NpcComponent)));
		return;
	}

	UpdateDialogueStateEvidence();
	BroadcastGameplayEventTrigger();
}

void FAINpcUs2PerceptionBehaviorVisualTest::BroadcastGameplayEventTrigger()
{
	if (bComplete || bFailed || !Npc.NpcComponent)
	{
		return;
	}

	UWorld* World = Npc.GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UNpcEventSubsystem* EventSubsystem = GameInstance ? GameInstance->GetSubsystem<UNpcEventSubsystem>() : nullptr;
	if (!EventSubsystem)
	{
		Fail(TEXT("US-2 visible event trigger could not find UNpcEventSubsystem."));
		return;
	}

	const FGameplayTag EventTag = RequestOptionalGameplayTag(TEXT("AINpc.Tests.Route.Match"));
	if (!EventTag.IsValid())
	{
		Fail(TEXT("US-2 visible event trigger requires gameplay tag AINpc.Tests.Route.Match."));
		return;
	}

	FGameplayTagContainer RoutingTags;
	RoutingTags.AddTag(EventTag);
	const FNpcEventMessage EventMessage = UNpcEventPayloadBlueprintLibrary::MakeGiftEventMessage(
		EventTag,
		RoutingTags,
		nullptr,
		&Npc,
		FGameplayTag(),
		1);

	bEventTriggerBroadcastObserved = true;
	ShowStatus(TEXT("US-2 gameplay event trigger broadcast while the provider request is in flight."), FColor::Orange, 7.0f);
	EventSubsystem->BroadcastEvent(EventMessage);
}

void FAINpcUs2PerceptionBehaviorVisualTest::HandleTimeout()
{
	if (bComplete || bFailed)
	{
		return;
	}

	Fail(FString::Printf(
		TEXT("Timed out after %.1fs waiting for US-2 visible evidence. Evidence={%s}. %s"),
		VerificationTimeoutSeconds,
		*BuildSummary(),
		*AINpcDialogueVisualTestSupport::DescribeDialogueState(Npc.NpcComponent)));
}

void FAINpcUs2PerceptionBehaviorVisualTest::MarkActionObservationHoldElapsed()
{
	bActionObservationHoldElapsed = true;
	Poll();
}

void FAINpcUs2PerceptionBehaviorVisualTest::UpdateDialogueStateEvidence()
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

bool FAINpcUs2PerceptionBehaviorVisualTest::HasRequiredEvidence() const
{
	return bDialogueSessionStartedObserved
		&& bWaitingStateObserved
		&& bSpeakingStateObserved
		&& bEventTriggerBroadcastObserved
		&& bEventDelayMaskingStartObserved
		&& bDelayMaskingEndObserved
		&& bPartialResponseObserved
		&& bStructuredResponseObserved
		&& bActionIntentObserved
		&& ((bActionExecutionAccepted && bActionTargetReached && bActionObservationHoldElapsed) || bActionRejectedVisible);
}

bool FAINpcUs2PerceptionBehaviorVisualTest::ConfigurePersona(FString& OutFailureReason)
{
	FString DelayFillerText;
	if (!LoadDelayFillerText(DelayFillerText, OutFailureReason))
	{
		return false;
	}

	AINpcDialogueVisualTestSupport::FVisualHarnessPersonaText PersonaText;
	if (!AINpcDialogueVisualTestSupport::LoadPersonaText(PersonaFileName, PersonaText, OutFailureReason))
	{
		return false;
	}

	VisualHarnessPersona = NewObject<UNpcPersonaDataAsset>(&Npc, TEXT("US2VisualHarnessPersona"));
	if (!VisualHarnessPersona)
	{
		OutFailureReason = TEXT("Failed to allocate US-2 visual harness persona data.");
		return false;
	}

	VisualHarnessPersona->PersonaName = PersonaText.PersonaName;
	VisualHarnessPersona->Background = PersonaText.Background;
	VisualHarnessPersona->SpeakingStyle = PersonaText.SpeakingStyle;
	VisualHarnessPersona->DelayFillerThreshold = 30.0f;
	VisualHarnessPersona->DelayFillerTexts.Add(FText::FromString(DelayFillerText));
	VisualHarnessPersona->InspectDelayMaskingMontages.Add(NewObject<UAnimMontage>(VisualHarnessPersona));
	Npc.NpcComponent->SetPersonaData(VisualHarnessPersona);
	const FGameplayTag RouteTag = RequestOptionalGameplayTag(TEXT("AINpc.Tests.Route.Match"));
	if (!RouteTag.IsValid())
	{
		OutFailureReason = TEXT("US-2 visual harness requires gameplay tag AINpc.Tests.Route.Match.");
		return false;
	}
	Npc.NpcComponent->EventSubscriptionTags.AddTag(RouteTag);
	return true;
}

void FAINpcUs2PerceptionBehaviorVisualTest::Fail(const FString& Reason)
{
	if (bComplete || bFailed)
	{
		return;
	}

	bFailed = true;
	FailureReason = Reason;
	if (UWorld* World = Npc.GetWorld())
	{
		World->GetTimerManager().ClearTimer(StartDialogueTimerHandle);
		World->GetTimerManager().ClearTimer(TimeoutTimerHandle);
		World->GetTimerManager().ClearTimer(ActionObservationHoldTimerHandle);
	}
}

bool FAINpcUs2PerceptionBehaviorVisualTest::LoadEventPrompt(FString& OutPrompt, FString& OutFailureReason) const
{
	FString LoadedTemplate;
	if (!AINpcDialogueVisualTestSupport::LoadRequiredConfigText(Us2PromptTemplateFileName, TEXT("US-2 event prompt template"), LoadedTemplate, OutFailureReason))
	{
		return false;
	}

	if (!LoadedTemplate.Contains(SmartObjectTargetIdPlaceholder))
	{
		const FString TemplatePath = FPaths::Combine(FPaths::ProjectConfigDir(), Us2PromptTemplateFileName);
		OutFailureReason = FString::Printf(TEXT("US-2 visual harness prompt template %s is missing required placeholder %s."), *TemplatePath, SmartObjectTargetIdPlaceholder);
		return false;
	}

	if (!LoadedTemplate.Contains(EventTriggerIdPlaceholder))
	{
		const FString TemplatePath = FPaths::Combine(FPaths::ProjectConfigDir(), Us2PromptTemplateFileName);
		OutFailureReason = FString::Printf(TEXT("US-2 visual harness prompt template %s is missing required placeholder %s."), *TemplatePath, EventTriggerIdPlaceholder);
		return false;
	}

	OutPrompt = MoveTemp(LoadedTemplate);
	return true;
}

bool FAINpcUs2PerceptionBehaviorVisualTest::LoadDelayFillerText(FString& OutText, FString& OutFailureReason) const
{
	return AINpcDialogueVisualTestSupport::LoadRequiredConfigText(DelayFillerFileName, TEXT("delay filler"), OutText, OutFailureReason);
}

void FAINpcUs2PerceptionBehaviorVisualTest::ShowStatus(const FString& Message, const FColor& Color, const float DurationSeconds) const
{
	UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, DurationSeconds, Color, Message);
	}
}

void FAINpcUs2PerceptionBehaviorVisualTest::OnNpcSessionStarted()
{
	bDialogueSessionStartedObserved = true;
	ShowStatus(TEXT("US-2 dialogue session started through the real configured provider chain."), FColor::Green, 6.0f);
}

void FAINpcUs2PerceptionBehaviorVisualTest::OnNpcResponse(const FString& Text)
{
	LastNpcResponseText = Text;
	LastNpcResponseText.TrimStartAndEndInline();
	bStructuredResponseObserved = !LastNpcResponseText.IsEmpty();
	if (!bStructuredResponseObserved)
	{
		Fail(TEXT("Real provider returned an empty US-2 structured dialogue response."));
		return;
	}

	Npc.SetVisibleDialogueText(LastNpcResponseText);
	UpdateDialogueStateEvidence();
	ShowStatus(FString::Printf(TEXT("<<< US-2 NPC structured dialogue received. Length=%d Present=%s"), LastNpcResponseText.Len(), LastNpcResponseText.IsEmpty() ? TEXT("false") : TEXT("true")), FColor::Yellow, 8.0f);

	FNpcAction LatestAction;
	bActionIntentObserved = Npc.NpcComponent && Npc.NpcComponent->TryGetLatestActionIntent(LatestAction);
	if (!bActionIntentObserved)
	{
		Fail(TEXT("US-2 structured provider response did not produce an action intent for SmartObject validation."));
		return;
	}

	FAINpcVisualSmartObjectActionExecution Execution;
	FString ActionFailureReason;
	if (AINpcSmartObjectActionTestSupport::TryExecuteLatestActionIntent(Npc.NpcComponent, Npc, SmartObjectSearchRadius, SmartObjectClaimPriority, Execution, ActionFailureReason))
	{
		bActionExecutionAccepted = true;
		SmartObject.SetInteractionState(true);
		Npc.BeginVisualActionMove(Execution.ClaimedSlotTransform, Execution.RequestedTarget);
		ShowStatus(
			FString::Printf(TEXT("US-2 SmartObject action accepted: %s -> %s. NPC must visibly reach the slot and hold for %.1fs before PASS."), *Execution.ActionType, *Execution.RequestedTarget, ActionObservationHoldSeconds),
			FColor::Green,
			8.0f);
		return;
	}

	LastActionFailureReason = ActionFailureReason;
	bActionRejectedVisible = true;
	SmartObject.SetInteractionState(false);
	Npc.SetVisibleDelayMaskingText(FString::Printf(TEXT("SmartObject rejected visibly: %s"), *ActionFailureReason.Left(100)));
	ShowStatus(FString::Printf(TEXT("US-2 SmartObject action visibly rejected: %s"), *ActionFailureReason.Left(160)), FColor::Red, 8.0f);
}

void FAINpcUs2PerceptionBehaviorVisualTest::OnNpcPartialResponse(const FString& Text)
{
	const FString TrimmedText = Text.TrimStartAndEnd();
	if (TrimmedText.IsEmpty())
	{
		return;
	}

	LastPartialResponseText += TrimmedText;
	bPartialResponseObserved = true;
	Npc.SetVisibleDialogueText(FString::Printf(TEXT("[stream] %s"), *LastPartialResponseText.Left(140)));
	ShowStatus(FString::Printf(TEXT("[US-2 stream] partial response received. Length=%d Present=%s"), TrimmedText.Len(), TrimmedText.IsEmpty() ? TEXT("false") : TEXT("true")), FColor::Orange, 3.0f);
}

void FAINpcUs2PerceptionBehaviorVisualTest::OnNpcError(const FString& ErrorMessage)
{
	Fail(FString::Printf(TEXT("US-2 real provider chain reported an error. ErrorLength=%d Present=%s"), ErrorMessage.Len(), ErrorMessage.IsEmpty() ? TEXT("false") : TEXT("true")));
}

void FAINpcUs2PerceptionBehaviorVisualTest::OnNpcSessionEnded()
{
	ShowStatus(TEXT("US-2 dialogue session ended."), FColor::Silver, 4.0f);
}

void FAINpcUs2PerceptionBehaviorVisualTest::OnNpcDegraded(const FString& FallbackResponse, const FString& FailureReasonText)
{
	Fail(FString::Printf(
		TEXT("US-2 dialogue degraded instead of producing a real provider structured response. FailureReasonPresent=%s FailureReasonLength=%d FallbackPresent=%s FallbackLength=%d"),
		FailureReasonText.IsEmpty() ? TEXT("false") : TEXT("true"),
		FailureReasonText.Len(),
		FallbackResponse.IsEmpty() ? TEXT("false") : TEXT("true"),
		FallbackResponse.Len()));
}

void FAINpcUs2PerceptionBehaviorVisualTest::OnNpcDelayMaskingStart(UAnimMontage* Montage, const FText& FillerText)
{
	if (!bEventTriggerBroadcastObserved)
	{
		return;
	}

	bEventDelayMaskingStartObserved = true;
	LastDelayFillerText = FillerText.ToString();
	const FString DebugDelayText = LastDelayFillerText.IsEmpty()
		? FString::Printf(TEXT("event delay masking active; montage=%s"), Montage ? *Montage->GetName() : TEXT("<none>"))
		: FString::Printf(TEXT("event delay masking active: %s"), *LastDelayFillerText);
	Npc.SetVisibleDelayMaskingText(DebugDelayText);

	ShowStatus(
		FString::Printf(
			TEXT("US-2 event-driven delay masking observed. Montage=%s Filler=%s"),
			Montage ? *Montage->GetName() : TEXT("<none>"),
			LastDelayFillerText.IsEmpty() ? TEXT("<empty>") : *LastDelayFillerText.Left(120)),
		FColor::Orange,
		6.0f);
}

void FAINpcUs2PerceptionBehaviorVisualTest::OnNpcDelayMaskingEnd()
{
	bDelayMaskingEndObserved = true;
	Npc.SetVisibleDelayMaskingText(TEXT("event delay masking ended after provider response"));
	ShowStatus(TEXT("US-2 delay masking end delegate observed after provider response."), FColor::Orange, 6.0f);
}
