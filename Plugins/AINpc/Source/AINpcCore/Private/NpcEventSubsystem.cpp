#include "Events/NpcEventSubsystem.h"

// FR-32: Event payload routing using FInstancedStruct for type-safe, flexible event data
void UNpcEventSubsystem::BroadcastEvent(const FNpcEventMessage& EventMessage)
{
	const TArray<ENpcEventDispatchStage> DispatchOrder = GetDefaultDispatchOrder();
	for (const ENpcEventDispatchStage DispatchStage : DispatchOrder)
	{
		OnEventStageDispatched.Broadcast(EventMessage, DispatchStage);
		EventStageDispatchedNative.Broadcast(EventMessage, DispatchStage);
	}
}

TArray<ENpcEventDispatchStage> UNpcEventSubsystem::GetDefaultDispatchOrder()
{
	return {
		ENpcEventDispatchStage::DelayMasking,
		ENpcEventDispatchStage::EmotionAppraisal,
		ENpcEventDispatchStage::MemoryWrite,
		ENpcEventDispatchStage::PromptUpdate
	};
}

FNpcEventStageDispatchedNative& UNpcEventSubsystem::OnEventStageDispatchedNative()
{
	return EventStageDispatchedNative;
}
