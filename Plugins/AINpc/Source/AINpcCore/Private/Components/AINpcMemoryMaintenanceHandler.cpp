#include "Components/AINpcMemoryMaintenanceHandler.h"
#include "Components/AINpcComponent.h"
#include "LLM/LLMConcurrencyManager.h"

bool FAINpcMemoryMaintenanceHandler::TryStart(UAINpcComponent& Component)
{
	return TryAcquireSlot(Component);
}

void FAINpcMemoryMaintenanceHandler::End(UAINpcComponent& Component)
{
	CancelQueuedRequest(Component);
	ReleaseSlot(Component);
}

bool FAINpcMemoryMaintenanceHandler::IsActive(const UAINpcComponent& Component)
{
	return Component.bOwnsMemoryMaintenanceSlot;
}

bool FAINpcMemoryMaintenanceHandler::TryAcquireSlot(UAINpcComponent& Component)
{
	check(IsInGameThread());
	if (Component.bOwnsMemoryMaintenanceSlot || Component.QueuedMemoryMaintenanceRequestToken != 0)
	{
		return false;
	}

	uint64 QueueToken = 0;
	if (!FLLMConcurrencyManager::Get().TryAcquireMemorySlot(&Component, QueueToken))
	{
		Component.QueuedMemoryMaintenanceRequestToken = QueueToken;
		return false;
	}

	Component.bOwnsMemoryMaintenanceSlot = true;
	return true;
}

bool FAINpcMemoryMaintenanceHandler::TryAcquireQueuedSlot(UAINpcComponent& Component, const uint64 QueueToken)
{
	if (Component.QueuedMemoryMaintenanceRequestToken != QueueToken)
	{
		return false;
	}

	Component.QueuedMemoryMaintenanceRequestToken = 0;
	Component.bOwnsMemoryMaintenanceSlot = true;
	return true;
}

void FAINpcMemoryMaintenanceHandler::CancelQueuedRequest(UAINpcComponent& Component)
{
	if (Component.QueuedMemoryMaintenanceRequestToken == 0)
	{
		return;
	}

	const uint64 QueueTokenToRemove = Component.QueuedMemoryMaintenanceRequestToken;
	Component.QueuedMemoryMaintenanceRequestToken = 0;
	FLLMConcurrencyManager::Get().CancelQueuedMemoryRequest(&Component, QueueTokenToRemove);
}

void FAINpcMemoryMaintenanceHandler::ReleaseSlot(UAINpcComponent& Component)
{
	check(IsInGameThread());
	if (!Component.bOwnsMemoryMaintenanceSlot)
	{
		return;
	}

	Component.bOwnsMemoryMaintenanceSlot = false;
	FLLMConcurrencyManager::Get().ReleaseMemorySlot();
}

void FAINpcMemoryMaintenanceHandler::PumpQueuedRequests()
{
	FLLMConcurrencyManager::Get().PumpMemoryQueue();
}
