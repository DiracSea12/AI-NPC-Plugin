#include "Components/AINpcEventRoutingHandler.h"

#include "Components/AINpcComponent.h"
#include "Components/AINpcDelayMaskingHandler.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Events/NpcEventSubsystem.h"

void FAINpcEventRoutingHandler::Bind(UAINpcComponent& Component)
{
	if (Component.EventStageDispatchedHandle.IsValid())
	{
		return;
	}

	UWorld* World = Component.GetWorld();
	if (!World)
	{
		return;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	if (!GameInstance)
	{
		return;
	}

	UNpcEventSubsystem* EventSubsystem = GameInstance->GetSubsystem<UNpcEventSubsystem>();
	if (!EventSubsystem)
	{
		return;
	}

	Component.BoundEventSubsystem = EventSubsystem;
	Component.EventStageDispatchedHandle =
		EventSubsystem->OnEventStageDispatchedNative().AddUObject(&Component, &UAINpcComponent::HandleNpcEventStageDispatched);
}

void FAINpcEventRoutingHandler::Unbind(UAINpcComponent& Component)
{
	if (UNpcEventSubsystem* EventSubsystem = Component.BoundEventSubsystem.Get())
	{
		if (Component.EventStageDispatchedHandle.IsValid())
		{
			EventSubsystem->OnEventStageDispatchedNative().Remove(Component.EventStageDispatchedHandle);
		}
	}

	Component.BoundEventSubsystem.Reset();
	Component.EventStageDispatchedHandle.Reset();
}

void FAINpcEventRoutingHandler::HandleStageDispatched(
	UAINpcComponent& Component,
	const FNpcEventMessage& EventMessage,
	const ENpcEventDispatchStage DispatchStage)
{
	if (!ShouldProcess(Component, EventMessage))
	{
		return;
	}

	switch (DispatchStage)
	{
	case ENpcEventDispatchStage::DelayMasking:
		ProcessDelayMasking(Component, EventMessage);
		break;
	case ENpcEventDispatchStage::EmotionAppraisal:
		ProcessEmotionAppraisal(Component, EventMessage);
		break;
	case ENpcEventDispatchStage::MemoryWrite:
		ProcessMemoryWrite(Component, EventMessage);
		break;
	case ENpcEventDispatchStage::PromptUpdate:
		ProcessPromptUpdate(Component, EventMessage);
		break;
	default:
		break;
	}
}

bool FAINpcEventRoutingHandler::ShouldProcess(const UAINpcComponent& Component, const FNpcEventMessage& EventMessage)
{
	if (Component.EventSubscriptionTags.IsEmpty())
	{
		return true;
	}

	FGameplayTagContainer EffectiveRoutingTags = EventMessage.RoutingTags;
	if (EffectiveRoutingTags.IsEmpty() && EventMessage.EventTag.IsValid())
	{
		// EventTag is the routing fallback when explicit routing tags are not provided.
		EffectiveRoutingTags.AddTag(EventMessage.EventTag);
	}

	if (EffectiveRoutingTags.IsEmpty())
	{
		return true;
	}

	return EffectiveRoutingTags.HasAny(Component.EventSubscriptionTags);
}

void FAINpcEventRoutingHandler::ProcessDelayMasking(UAINpcComponent& Component, const FNpcEventMessage& EventMessage)
{
	FAINpcDelayMaskingHandler::ProcessNpcEvent(Component, EventMessage);
}

void FAINpcEventRoutingHandler::ProcessEmotionAppraisal(UAINpcComponent& Component, const FNpcEventMessage& EventMessage)
{
	(void)Component;
	(void)EventMessage;
}

void FAINpcEventRoutingHandler::ProcessMemoryWrite(UAINpcComponent& Component, const FNpcEventMessage& EventMessage)
{
	(void)Component;
	(void)EventMessage;
}

void FAINpcEventRoutingHandler::ProcessPromptUpdate(UAINpcComponent& Component, const FNpcEventMessage& EventMessage)
{
	(void)EventMessage;

	if (!Component.bIsDialogueSessionActive || Component.ConversationHistory.IsEmpty())
	{
		return;
	}

	FLLMMessage& FirstMessage = Component.ConversationHistory[0];
	if (FirstMessage.Role.Equals(TEXT("system"), ESearchCase::IgnoreCase))
	{
		FirstMessage.Content = Component.BuildSystemPrompt();
	}
}
