// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class UAINpcComponent;

/**
 * Manages concurrency limits for dialogue and memory maintenance requests.
 *
 * Thread Safety: All methods must be called from the game thread.
 * Uses check(IsInGameThread()) assertions to enforce this requirement.
 * These assertions compile out in shipping builds, but the game thread
 * requirement is fundamental to UE5's actor/component model.
 */
class AINPCCORE_API FLLMConcurrencyManager
{
public:
	static FLLMConcurrencyManager& Get();

	// Dialogue concurrency control
	bool TryAcquireDialogueSlot(UAINpcComponent* Component, uint64& OutQueueToken);
	void ReleaseDialogueSlot();
	void PumpDialogueQueue();
	bool TryDequeueDialogueRequest(UAINpcComponent* Component, uint64 QueueToken);
	void CancelQueuedDialogueRequest(UAINpcComponent* Component, uint64 QueueToken);

	// Memory maintenance concurrency control
	bool TryAcquireMemorySlot(UAINpcComponent* Component, uint64& OutQueueToken);
	void ReleaseMemorySlot();
	void PumpMemoryQueue();
	bool TryDequeueMemoryRequest(UAINpcComponent* Component, uint64 QueueToken);
	void CancelQueuedMemoryRequest(UAINpcComponent* Component, uint64 QueueToken);

	// Accessors
	int32 GetActiveDialogueSlots() const { return ActiveDialogueSlots; }
	int32 GetActiveMemorySlots() const { return ActiveMemorySlots; }
	int32 GetQueuedDialogueCount() const { return QueuedDialogueRequests.Num(); }
	int32 GetQueuedMemoryCount() const { return QueuedMemoryRequests.Num(); }

#if WITH_EDITOR
	void ResetForTest();
	void SetActiveDialogueSlotsForTest(int32 Slots) { ActiveDialogueSlots = FMath::Max(0, Slots); }
	void SetActiveMemorySlotsForTest(int32 Slots) { ActiveMemorySlots = FMath::Max(0, Slots); }
#endif

private:
	FLLMConcurrencyManager();
	~FLLMConcurrencyManager() = default;
	FLLMConcurrencyManager(const FLLMConcurrencyManager&) = delete;
	FLLMConcurrencyManager& operator=(const FLLMConcurrencyManager&) = delete;

	struct FQueuedRequest
	{
		TWeakObjectPtr<UAINpcComponent> Component;
		uint64 QueueToken = 0;
	};

	TArray<FQueuedRequest> QueuedDialogueRequests;
	TArray<FQueuedRequest> QueuedMemoryRequests;
	int32 ActiveDialogueSlots = 0;
	int32 ActiveMemorySlots = 0;
	uint64 NextDialogueQueueToken = 1;
	uint64 NextMemoryQueueToken = 1;

	int32 GetDialogueLimit() const;
	int32 GetMemoryLimit() const;
};
