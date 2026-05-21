#include "Test/AINpcUs1DialogueActionVisualTest.h"

#include "Animation/AnimMontage.h"
#include "Data/NpcPersonaDataAsset.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Test/AINpcDialogueVisualTestSupport.h"
#include "Test/AINpcSmartObjectActionTestSupport.h"
#include "Test/AINpcTestCharacter.h"
#include "Test/AINpcTestSmartObjectActor.h"
#include "TimerManager.h"

namespace
{
	const float InitialDialogueDelaySeconds = 3.0f;
	const float VerificationTimeoutSeconds = 45.0f;
	const float DefaultActionObservationHoldSeconds = 3.0f;
	const float SmartObjectSearchRadius = 1200.0f;
	const int32 SmartObjectClaimPriority = 2;
	const TCHAR* InitialPromptTemplateFileName = TEXT("AINpcVisualHarnessInitialPrompt.txt");
	const TCHAR* DelayFillerFileName = TEXT("AINpcVisualHarnessDelayFiller.txt");
	const TCHAR* PersonaFileName = TEXT("AINpcVisualHarnessUs1Persona.txt");
	const TCHAR* SmartObjectTargetIdPlaceholder = TEXT("{SmartObjectTargetId}");
}

FAINpcUs1DialogueActionVisualTest::FAINpcUs1DialogueActionVisualTest(AAINpcTestCharacter& InNpc, AAINpcTestSmartObjectActor& InSmartObject)
	: Npc(InNpc)
	, SmartObject(InSmartObject)
{
}

FAINpcUs1DialogueActionVisualTest::~FAINpcUs1DialogueActionVisualTest()
{
	if (UWorld* World = Npc.GetWorld())
	{
		World->GetTimerManager().ClearTimer(InitialDialogueTimerHandle);
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

bool FAINpcUs1DialogueActionVisualTest::Start(FString& OutFailureReason)
{
	if (!Npc.NpcComponent)
	{
		OutFailureReason = TEXT("US-1 test cannot start because NPC component is null.");
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
	SessionStartedHandle = NpcComponent.OnDialogueSessionStartedNative().AddRaw(this, &FAINpcUs1DialogueActionVisualTest::OnNpcSessionStarted);
	ResponseHandle = NpcComponent.OnDialogueResponseNative().AddRaw(this, &FAINpcUs1DialogueActionVisualTest::OnNpcResponse);
	PartialResponseHandle = NpcComponent.OnDialoguePartialResponseNative().AddRaw(this, &FAINpcUs1DialogueActionVisualTest::OnNpcPartialResponse);
	ErrorHandle = NpcComponent.OnDialogueErrorNative().AddRaw(this, &FAINpcUs1DialogueActionVisualTest::OnNpcError);
	SessionEndedHandle = NpcComponent.OnDialogueSessionEndedNative().AddRaw(this, &FAINpcUs1DialogueActionVisualTest::OnNpcSessionEnded);
	DegradedHandle = NpcComponent.OnDialogueDegradedNative().AddRaw(this, &FAINpcUs1DialogueActionVisualTest::OnNpcDegraded);
	DelayMaskingStartHandle = NpcComponent.OnDelayMaskingStartNative().AddRaw(this, &FAINpcUs1DialogueActionVisualTest::OnNpcDelayMaskingStart);
	DelayMaskingEndHandle = NpcComponent.OnDelayMaskingEndNative().AddRaw(this, &FAINpcUs1DialogueActionVisualTest::OnNpcDelayMaskingEnd);

	UWorld* World = Npc.GetWorld();
	if (!World)
	{
		OutFailureReason = TEXT("US-1 test cannot start because World is null.");
		return false;
	}

	bStarted = true;
	ShowStatus(TEXT("NPC and runtime SmartObject spawned. Starting real provider US-1 dialogue verification in 3 seconds..."), FColor::Green, 15.0f);
	World->GetTimerManager().SetTimer(TimeoutTimerHandle, FTimerDelegate::CreateRaw(this, &FAINpcUs1DialogueActionVisualTest::HandleTimeout), VerificationTimeoutSeconds, false);
	World->GetTimerManager().SetTimer(InitialDialogueTimerHandle, FTimerDelegate::CreateRaw(this, &FAINpcUs1DialogueActionVisualTest::StartInitialDialogue), InitialDialogueDelaySeconds, false);
	return true;
}

void FAINpcUs1DialogueActionVisualTest::Poll()
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
			World->GetTimerManager().SetTimer(ActionObservationHoldTimerHandle, FTimerDelegate::CreateRaw(this, &FAINpcUs1DialogueActionVisualTest::MarkActionObservationHoldElapsed), ActionObservationHoldSeconds, false);
		}
	}

	if (HasRequiredEvidence())
	{
		bComplete = true;
	}
}

bool FAINpcUs1DialogueActionVisualTest::IsComplete() const
{
	return bComplete;
}

bool FAINpcUs1DialogueActionVisualTest::HasFailed() const
{
	return bFailed;
}

const FString& FAINpcUs1DialogueActionVisualTest::GetFailureReason() const
{
	return FailureReason;
}

FString FAINpcUs1DialogueActionVisualTest::BuildSummary() const
{
	return FString::Printf(
		TEXT("SessionStarted=%s WaitingState=%s SpeakingState=%s ResponseObserved=%s DelayStart=%s DelayEnd=%s ActionAccepted=%s ActionTargetReached=%s ActionObservationHoldElapsed=%s ResponseLen=%d DelayTextLen=%d DistanceToActionTarget=%.1f"),
		bDialogueSessionStartedObserved ? TEXT("true") : TEXT("false"),
		bWaitingStateObserved ? TEXT("true") : TEXT("false"),
		bSpeakingStateObserved ? TEXT("true") : TEXT("false"),
		bDialogueResponseObserved ? TEXT("true") : TEXT("false"),
		bDelayMaskingStartObserved ? TEXT("true") : TEXT("false"),
		bDelayMaskingEndObserved ? TEXT("true") : TEXT("false"),
		bActionExecutionAccepted ? TEXT("true") : TEXT("false"),
		bActionTargetReached ? TEXT("true") : TEXT("false"),
		bActionObservationHoldElapsed ? TEXT("true") : TEXT("false"),
		LastNpcResponseText.Len(),
		LastDelayFillerText.Len(),
		Npc.GetVisualActionTargetDistance());
}

FAINpcVisualTestObservations FAINpcUs1DialogueActionVisualTest::BuildObservations() const
{
	FAINpcVisualTestObservations Observations;
	Observations.BooleanFields.Add(TEXT("sessionStarted"), bDialogueSessionStartedObserved);
	Observations.BooleanFields.Add(TEXT("waitingStateObserved"), bWaitingStateObserved);
	Observations.BooleanFields.Add(TEXT("speakingStateObserved"), bSpeakingStateObserved);
	Observations.BooleanFields.Add(TEXT("dialogueResponseObserved"), bDialogueResponseObserved);
	Observations.BooleanFields.Add(TEXT("delayMaskingStartObserved"), bDelayMaskingStartObserved);
	Observations.BooleanFields.Add(TEXT("delayMaskingEndObserved"), bDelayMaskingEndObserved);
	Observations.BooleanFields.Add(TEXT("actionExecutionAccepted"), bActionExecutionAccepted);
	Observations.BooleanFields.Add(TEXT("actionTargetReached"), bActionTargetReached);
	Observations.BooleanFields.Add(TEXT("actionObservationHoldElapsed"), bActionObservationHoldElapsed);
	Observations.IntegerFields.Add(TEXT("responseLength"), LastNpcResponseText.Len());
	Observations.IntegerFields.Add(TEXT("delayFillerLength"), LastDelayFillerText.Len());
	Observations.NumberFields.Add(TEXT("distanceToActionTarget"), Npc.GetVisualActionTargetDistance());
	return Observations;
}

void FAINpcUs1DialogueActionVisualTest::StartInitialDialogue()
{
	if (bComplete || bFailed || !Npc.NpcComponent)
	{
		return;
	}

	const TArray<FString> AvailableTargets = Npc.NpcComponent->GetAvailableSmartObjectTargetsForExecution();
	if (AvailableTargets.IsEmpty())
	{
		Fail(TEXT("Runtime SmartObject target list was empty, so the harness could not ask for a legal required action."));
		return;
	}

	FString InitialPrompt;
	FString PromptFailureReason;
	if (!LoadInitialPrompt(InitialPrompt, PromptFailureReason))
	{
		Fail(PromptFailureReason);
		return;
	}

	InitialPrompt = InitialPrompt.Replace(SmartObjectTargetIdPlaceholder, *AvailableTargets[0], ESearchCase::CaseSensitive);
	ShowStatus(FString::Printf(TEXT(">>> Player: %s"), *InitialPrompt), FColor::Cyan, 10.0f);
	if (!Npc.NpcComponent->StartDialogue(InitialPrompt))
	{
		Fail(FString::Printf(TEXT("Initial StartDialogue call was rejected before any real provider response or action verification. %s"), *AINpcDialogueVisualTestSupport::DescribeDialogueState(Npc.NpcComponent)));
		return;
	}

	UpdateDialogueStateEvidence();
}

void FAINpcUs1DialogueActionVisualTest::HandleTimeout()
{
	if (bComplete || bFailed)
	{
		return;
	}

	Fail(FString::Printf(
		TEXT("Timed out after %.1fs waiting for US-1 required visible evidence. Evidence={%s}. %s"),
		VerificationTimeoutSeconds,
		*BuildSummary(),
		*AINpcDialogueVisualTestSupport::DescribeDialogueState(Npc.NpcComponent)));
}

void FAINpcUs1DialogueActionVisualTest::MarkActionObservationHoldElapsed()
{
	bActionObservationHoldElapsed = true;
	Poll();
}

void FAINpcUs1DialogueActionVisualTest::UpdateDialogueStateEvidence()
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

bool FAINpcUs1DialogueActionVisualTest::HasRequiredEvidence() const
{
	return bDialogueSessionStartedObserved
		&& bWaitingStateObserved
		&& bSpeakingStateObserved
		&& bDialogueResponseObserved
		&& bDelayMaskingStartObserved
		&& bDelayMaskingEndObserved
		&& bActionExecutionAccepted
		&& bActionTargetReached
		&& bActionObservationHoldElapsed;
}

bool FAINpcUs1DialogueActionVisualTest::ConfigurePersona(FString& OutFailureReason)
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

	VisualHarnessPersona = NewObject<UNpcPersonaDataAsset>(&Npc, TEXT("US1VisualHarnessPersona"));
	if (!VisualHarnessPersona)
	{
		OutFailureReason = TEXT("Failed to allocate US-1 visual harness persona data.");
		return false;
	}

	VisualHarnessPersona->PersonaName = PersonaText.PersonaName;
	VisualHarnessPersona->Background = PersonaText.Background;
	VisualHarnessPersona->SpeakingStyle = PersonaText.SpeakingStyle;
	VisualHarnessPersona->DelayFillerThreshold = 0.0f;
	VisualHarnessPersona->DelayFillerTexts.Add(FText::FromString(DelayFillerText));
	Npc.NpcComponent->SetPersonaData(VisualHarnessPersona);
	return true;
}

void FAINpcUs1DialogueActionVisualTest::Fail(const FString& Reason)
{
	if (bComplete || bFailed)
	{
		return;
	}

	bFailed = true;
	FailureReason = Reason;
	if (UWorld* World = Npc.GetWorld())
	{
		World->GetTimerManager().ClearTimer(InitialDialogueTimerHandle);
		World->GetTimerManager().ClearTimer(TimeoutTimerHandle);
		World->GetTimerManager().ClearTimer(ActionObservationHoldTimerHandle);
	}
}

bool FAINpcUs1DialogueActionVisualTest::LoadInitialPrompt(FString& OutPrompt, FString& OutFailureReason) const
{
	FString LoadedTemplate;
	if (!AINpcDialogueVisualTestSupport::LoadRequiredConfigText(InitialPromptTemplateFileName, TEXT("initial prompt template"), LoadedTemplate, OutFailureReason))
	{
		return false;
	}

	if (!LoadedTemplate.Contains(SmartObjectTargetIdPlaceholder))
	{
		const FString TemplatePath = FPaths::Combine(FPaths::ProjectConfigDir(), InitialPromptTemplateFileName);
		OutFailureReason = FString::Printf(TEXT("Visual harness initial prompt template %s is missing required placeholder %s."), *TemplatePath, SmartObjectTargetIdPlaceholder);
		return false;
	}

	OutPrompt = MoveTemp(LoadedTemplate);
	return true;
}

bool FAINpcUs1DialogueActionVisualTest::LoadDelayFillerText(FString& OutText, FString& OutFailureReason) const
{
	return AINpcDialogueVisualTestSupport::LoadRequiredConfigText(DelayFillerFileName, TEXT("delay filler"), OutText, OutFailureReason);
}

void FAINpcUs1DialogueActionVisualTest::ShowStatus(const FString& Message, const FColor& Color, const float DurationSeconds) const
{
	UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, DurationSeconds, Color, Message);
	}
}

void FAINpcUs1DialogueActionVisualTest::OnNpcSessionStarted()
{
	bDialogueSessionStartedObserved = true;
	ShowStatus(TEXT("Dialogue session started through the real provider chain."), FColor::Green, 6.0f);
}

void FAINpcUs1DialogueActionVisualTest::OnNpcResponse(const FString& Text)
{
	ShowStatus(FString::Printf(TEXT("<<< NPC response received. Length=%d Present=%s"), Text.Len(), Text.IsEmpty() ? TEXT("false") : TEXT("true")), FColor::Yellow, 8.0f);
	LastNpcResponseText = Text;
	LastNpcResponseText.TrimStartAndEndInline();
	bDialogueResponseObserved = !LastNpcResponseText.IsEmpty();
	if (!bDialogueResponseObserved)
	{
		Fail(TEXT("Real provider returned an empty dialogue response."));
		return;
	}

	Npc.SetVisibleDialogueText(LastNpcResponseText);
	UpdateDialogueStateEvidence();

	FAINpcVisualSmartObjectActionExecution Execution;
	FString ActionFailureReason;
	if (!AINpcSmartObjectActionTestSupport::TryExecuteLatestActionIntent(Npc.NpcComponent, Npc, SmartObjectSearchRadius, SmartObjectClaimPriority, Execution, ActionFailureReason))
	{
		Fail(FString::Printf(TEXT("Required %s"), *ActionFailureReason));
		return;
	}

	bActionExecutionAccepted = true;
	SmartObject.SetInteractionState(true);
	Npc.BeginVisualActionMove(Execution.ClaimedSlotTransform, Execution.RequestedTarget);

	ShowStatus(
		FString::Printf(TEXT("Required SmartObject action accepted: %s -> %s. NPC must reach the claimed slot and hold for %.1fs before PASS."), *Execution.ActionType, *Execution.RequestedTarget, ActionObservationHoldSeconds),
		FColor::Green,
		8.0f);
}

void FAINpcUs1DialogueActionVisualTest::OnNpcPartialResponse(const FString& Text)
{
	ShowStatus(FString::Printf(TEXT("[stream] partial response received. Length=%d Present=%s"), Text.Len(), Text.IsEmpty() ? TEXT("false") : TEXT("true")), FColor::Orange, 3.0f);
}

void FAINpcUs1DialogueActionVisualTest::OnNpcError(const FString& ErrorMessage)
{
	Fail(FString::Printf(TEXT("Real provider chain reported an error. ErrorLength=%d Present=%s"), ErrorMessage.Len(), ErrorMessage.IsEmpty() ? TEXT("false") : TEXT("true")));
}

void FAINpcUs1DialogueActionVisualTest::OnNpcSessionEnded()
{
	ShowStatus(TEXT("Dialogue session ended."), FColor::Silver, 4.0f);
}

void FAINpcUs1DialogueActionVisualTest::OnNpcDegraded(const FString& FallbackResponse, const FString& FailureReasonText)
{
	Fail(FString::Printf(
		TEXT("Dialogue fell back instead of producing a real provider response. FailureReasonPresent=%s FailureReasonLength=%d FallbackPresent=%s FallbackLength=%d"),
		FailureReasonText.IsEmpty() ? TEXT("false") : TEXT("true"),
		FailureReasonText.Len(),
		FallbackResponse.IsEmpty() ? TEXT("false") : TEXT("true"),
		FallbackResponse.Len()));
}

void FAINpcUs1DialogueActionVisualTest::OnNpcDelayMaskingStart(UAnimMontage* Montage, const FText& FillerText)
{
	bDelayMaskingStartObserved = true;
	LastDelayFillerText = FillerText.ToString();
	const FString DebugDelayText = LastDelayFillerText.IsEmpty()
		? FString::Printf(TEXT("active; montage=%s"), Montage ? *Montage->GetName() : TEXT("<none>"))
		: LastDelayFillerText;
	Npc.SetVisibleDelayMaskingText(DebugDelayText);

	ShowStatus(
		FString::Printf(
			TEXT("Delay masking delegate observed. Montage=%s Filler=%s"),
			Montage ? *Montage->GetName() : TEXT("<none>"),
			LastDelayFillerText.IsEmpty() ? TEXT("<empty>") : *LastDelayFillerText.Left(120)),
		FColor::Orange,
		6.0f);
}

void FAINpcUs1DialogueActionVisualTest::OnNpcDelayMaskingEnd()
{
	bDelayMaskingEndObserved = true;
	const FString EndText = LastDelayFillerText.IsEmpty()
		? TEXT("ended; debug display assigned")
		: FString::Printf(TEXT("ended after filler: %s"), *LastDelayFillerText.Left(80));
	Npc.SetVisibleDelayMaskingText(EndText);
	ShowStatus(TEXT("Delay masking end delegate observed."), FColor::Orange, 6.0f);
}
