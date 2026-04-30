#pragma once

#include "CoreMinimal.h"
#include "LLM/LLMProviderTypes.h"

class UAINpcComponent;

class FAINpcDialogueLifecycleHandler
{
public:
	// Session lifecycle
	static bool StartDialogue(UAINpcComponent& Component, const FString& PlayerInput);
	static void EndDialogue(UAINpcComponent& Component);

	// Dispatch
	static bool DispatchDialogueRequest(UAINpcComponent& Component);
	static bool DispatchDialogueRequestNow(UAINpcComponent& Component);

	// Queued dispatch
	static bool TryDispatchQueuedDialogueRequest(UAINpcComponent& Component, uint64 QueueToken);
	static void CancelQueuedDialogueRequest(UAINpcComponent& Component);
	static void ReleaseDialogueDispatchSlot(UAINpcComponent& Component);
	static void PumpQueuedDialogueRequests();

	// Active request lifecycle
	static void ClearActiveRequest(UAINpcComponent& Component);
	static void HandleRequestCompleted(UAINpcComponent& Component, const FLLMResponse& Response);
	static void HandleStateTreeTimeoutFailure(UAINpcComponent& Component);
	static void HandleRetryRequestTimerElapsed(UAINpcComponent& Component);
	static void ScheduleRetryRequest(UAINpcComponent& Component, float DelaySeconds);
	static void ClearRetryTimer(UAINpcComponent& Component);
	static void BroadcastError(UAINpcComponent& Component, const FString& ErrorMessage);

#if WITH_EDITOR
	static void SetDispatchBypassForTest(bool bBypass);
#endif
};
