#pragma once

#include "CoreMinimal.h"

class UAINpcComponent;

class FAINpcMemoryMaintenanceHandler
{
public:
	static bool TryStart(UAINpcComponent& Component);
	static void End(UAINpcComponent& Component);
	static bool IsActive(const UAINpcComponent& Component);

	static bool TryAcquireSlot(UAINpcComponent& Component);
	static bool TryAcquireQueuedSlot(UAINpcComponent& Component, uint64 QueueToken);
	static void CancelQueuedRequest(UAINpcComponent& Component);
	static void ReleaseSlot(UAINpcComponent& Component);
	static void PumpQueuedRequests();
};
