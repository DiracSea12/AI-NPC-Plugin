#include "Components/AINpcDelayMaskingHandler.h"

#include "AINpcCoreLog.h"
#include "Animation/AnimMontage.h"
#include "Components/AINpcComponent.h"
#include "Data/NpcPersonaDataAsset.h"
#include "Events/NpcEventPayloadTypes.h"
#include "Events/NpcEventSubsystem.h"
#include "TimerManager.h"
#include "Engine/World.h"

void FAINpcDelayMaskingHandler::Schedule(UAINpcComponent& Component)
{
	ClearTimer(Component);

	if (!Component.bIsRequestInFlight || Component.CurrentDialogueState != ENpcDialogueState::WaitingForLLM)
	{
		return;
	}

	const float DelayThresholdSeconds = GetThresholdSeconds(Component);
	if (DelayThresholdSeconds <= UE_KINDA_SMALL_NUMBER)
	{
		HandleThresholdReached(Component);
		return;
	}

	if (UWorld* World = Component.GetWorld())
	{
		World->GetTimerManager().SetTimer(
			Component.DelayMaskingTimerHandle,
			&Component,
			&UAINpcComponent::HandleDelayMaskingThresholdReached,
			DelayThresholdSeconds,
			false);
	}
}

void FAINpcDelayMaskingHandler::ClearTimer(UAINpcComponent& Component)
{
	if (UWorld* World = Component.GetWorld())
	{
		World->GetTimerManager().ClearTimer(Component.DelayMaskingTimerHandle);
	}

	Component.DelayMaskingTimerHandle.Invalidate();
}

void FAINpcDelayMaskingHandler::HandleThresholdReached(UAINpcComponent& Component)
{
	if (!Component.bIsRequestInFlight || Component.CurrentDialogueState != ENpcDialogueState::WaitingForLLM)
	{
		return;
	}

	if (Component.bDelayMaskingActive)
	{
		const FText FillerText = SelectFillerText(Component);
		if (!FillerText.IsEmptyOrWhitespace())
		{
			BroadcastStart(Component, nullptr, FillerText);
		}
		return;
	}

	Start(Component);
}

void FAINpcDelayMaskingHandler::BroadcastStart(UAINpcComponent& Component, UAnimMontage* Montage, const FText& FillerText)
{
	Component.OnDelayMaskingStart.Broadcast(Montage, FillerText);
	Component.DelayMaskingStartNative.Broadcast(Montage, FillerText);
}

void FAINpcDelayMaskingHandler::Start(UAINpcComponent& Component)
{
	if (Component.bDelayMaskingActive)
	{
		return;
	}

	Component.bDelayMaskingActive = true;
	UAnimMontage* Montage = SelectMontage(Component);
	const FText FillerText = SelectFillerText(Component);
	BroadcastStart(Component, Montage, FillerText);
}

void FAINpcDelayMaskingHandler::End(UAINpcComponent& Component)
{
	if (!Component.bDelayMaskingActive)
	{
		return;
	}

	Component.bDelayMaskingActive = false;
	Component.OnDelayMaskingEnd.Broadcast();
	Component.DelayMaskingEndNative.Broadcast();
}

float FAINpcDelayMaskingHandler::GetThresholdSeconds(const UAINpcComponent& Component)
{
	if (!Component.PersonaDataAsset)
	{
		return 3.0f;
	}

	return FMath::Max(0.0f, Component.PersonaDataAsset->DelayFillerThreshold);
}

UAnimMontage* FAINpcDelayMaskingHandler::SelectMontage(const UAINpcComponent& Component)
{
	if (!Component.PersonaDataAsset)
	{
		return nullptr;
	}

	return SelectRandomMontage(Component.PersonaDataAsset->DelayMaskingMontages);
}

UAnimMontage* FAINpcDelayMaskingHandler::SelectRandomMontage(const TArray<TSoftObjectPtr<UAnimMontage>>& MontageOptions)
{
	if (MontageOptions.IsEmpty())
	{
		return nullptr;
	}

	TArray<int32> ValidIndices;
	ValidIndices.Reserve(MontageOptions.Num());
	for (int32 Index = 0; Index < MontageOptions.Num(); ++Index)
	{
		if (!MontageOptions[Index].IsNull())
		{
			ValidIndices.Add(Index);
		}
	}

	if (ValidIndices.IsEmpty())
	{
		return nullptr;
	}

	const int32 ChosenIndex = ValidIndices[FMath::RandHelper(ValidIndices.Num())];
	UAnimMontage* Montage = MontageOptions[ChosenIndex].LoadSynchronous();
	if (!Montage)
	{
		UE_LOG(LogAINpc, Warning, TEXT("SelectRandomDelayMaskingMontage: Synchronous load required for montage at index %d"), ChosenIndex);
	}
	return Montage;
}

UAnimMontage* FAINpcDelayMaskingHandler::SelectEventDrivenMontage(const UAINpcComponent& Component, const FNpcEventMessage& EventMessage)
{
	if (!Component.PersonaDataAsset)
	{
		return nullptr;
	}

	if (!IsEventRelevantForImmediate(Component, EventMessage))
	{
		return nullptr;
	}

	if (EventMessage.Payload.GetPtr<FNpcAttackEventPayload>())
	{
		if (Component.PersonaDataAsset->HitReactionDelayMaskingMontages.IsEmpty())
		{
			UE_LOG(LogAINpc, Warning, TEXT("SelectEventDrivenDelayMaskingMontage: HitReactionDelayMaskingMontages is empty for attack event"));
			return nullptr;
		}
		return SelectRandomMontage(Component.PersonaDataAsset->HitReactionDelayMaskingMontages);
	}

	if (EventMessage.Payload.GetPtr<FNpcGiftEventPayload>())
	{
		if (Component.PersonaDataAsset->InspectDelayMaskingMontages.IsEmpty())
		{
			UE_LOG(LogAINpc, Warning, TEXT("SelectEventDrivenDelayMaskingMontage: InspectDelayMaskingMontages is empty for gift event"));
			return nullptr;
		}
		return SelectRandomMontage(Component.PersonaDataAsset->InspectDelayMaskingMontages);
	}

	if (EventMessage.Payload.GetPtr<FNpcTradeEventPayload>())
	{
		if (Component.PersonaDataAsset->InspectDelayMaskingMontages.IsEmpty())
		{
			UE_LOG(LogAINpc, Warning, TEXT("SelectEventDrivenDelayMaskingMontage: InspectDelayMaskingMontages is empty for trade event"));
			return nullptr;
		}
		return SelectRandomMontage(Component.PersonaDataAsset->InspectDelayMaskingMontages);
	}

	return nullptr;
}

bool FAINpcDelayMaskingHandler::IsEventRelevantForImmediate(const UAINpcComponent& Component, const FNpcEventMessage& EventMessage)
{
	if (const FNpcAttackEventPayload* AttackPayload = EventMessage.Payload.GetPtr<FNpcAttackEventPayload>())
	{
		return !AttackPayload->TargetActor || AttackPayload->TargetActor == Component.GetOwner();
	}

	if (const FNpcGiftEventPayload* GiftPayload = EventMessage.Payload.GetPtr<FNpcGiftEventPayload>())
	{
		return !GiftPayload->ReceiverActor || GiftPayload->ReceiverActor == Component.GetOwner();
	}

	if (const FNpcTradeEventPayload* TradePayload = EventMessage.Payload.GetPtr<FNpcTradeEventPayload>())
	{
		const AActor* const OwnerActor = Component.GetOwner();
		if (!OwnerActor)
		{
			UE_LOG(LogAINpc, Warning, TEXT("IsEventRelevantForImmediateDelayMasking: GetOwner() returned null for trade event"));
			return false;
		}
		return (TradePayload->InitiatorActor == OwnerActor)
			|| (TradePayload->CounterpartyActor == OwnerActor)
			|| (!TradePayload->InitiatorActor && !TradePayload->CounterpartyActor);
	}

	return false;
}

FText FAINpcDelayMaskingHandler::SelectFillerText(const UAINpcComponent& Component)
{
	if (!Component.PersonaDataAsset || Component.PersonaDataAsset->DelayFillerTexts.IsEmpty())
	{
		return FText::GetEmpty();
	}

	TArray<int32> ValidIndices;
	ValidIndices.Reserve(Component.PersonaDataAsset->DelayFillerTexts.Num());
	for (int32 Index = 0; Index < Component.PersonaDataAsset->DelayFillerTexts.Num(); ++Index)
	{
		if (!Component.PersonaDataAsset->DelayFillerTexts[Index].IsEmptyOrWhitespace())
		{
			ValidIndices.Add(Index);
		}
	}

	if (ValidIndices.IsEmpty())
	{
		return FText::GetEmpty();
	}

	const int32 ChosenIndex = ValidIndices[FMath::RandHelper(ValidIndices.Num())];
	return Component.PersonaDataAsset->DelayFillerTexts[ChosenIndex];
}

void FAINpcDelayMaskingHandler::ProcessNpcEvent(UAINpcComponent& Component, const FNpcEventMessage& EventMessage)
{
	if (!IsEventRelevantForImmediate(Component, EventMessage))
	{
		return;
	}

	if (UAnimMontage* EventDrivenMontage = SelectEventDrivenMontage(Component, EventMessage))
	{
		UE_LOG(LogAINpc, Log, TEXT("Event-driven Montage Play triggered immediately (bypassing StateTree): %s for event %s"),
			*EventDrivenMontage->GetName(), *EventMessage.EventTag.ToString());

		if (Component.bDelayMaskingActive)
		{
			ClearTimer(Component);
		}
		else
		{
			Component.bDelayMaskingActive = true;
		}

		BroadcastStart(Component, EventDrivenMontage, FText::GetEmpty());
		return;
	}

	if (Component.bIsRequestInFlight && Component.CurrentDialogueState == ENpcDialogueState::WaitingForLLM)
	{
		Start(Component);
	}
}
