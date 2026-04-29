// Copyright Epic Games, Inc. All Rights Reserved.

#include "LLM/LLMConcurrencyManager.h"
#include "Components/AINpcComponent.h"
#include "Settings/AINpcSettings.h"

FLLMConcurrencyManager::FLLMConcurrencyManager()
{
}

FLLMConcurrencyManager& FLLMConcurrencyManager::Get()
{
	static FLLMConcurrencyManager Instance;
	return Instance;
}

int32 FLLMConcurrencyManager::GetDialogueLimit() const
{
	const UAINpcSettings* Settings = GetDefault<UAINpcSettings>();
	return FMath::Max(1, Settings ? Settings->DialogueRequestConcurrencyLimit : 1);
}

int32 FLLMConcurrencyManager::GetMemoryLimit() const
{
	const UAINpcSettings* Settings = GetDefault<UAINpcSettings>();
	return FMath::Max(1, Settings ? Settings->MemoryMaintenanceConcurrencyLimit : 1);
}

bool FLLMConcurrencyManager::TryAcquireDialogueSlot(UAINpcComponent* Component, uint64& OutQueueToken)
{
	check(IsInGameThread());
	check(Component);

	if (ActiveDialogueSlots >= GetDialogueLimit())
	{
		OutQueueToken = NextDialogueQueueToken++;
		FQueuedRequest Request;
		Request.Component = Component;
		Request.QueueToken = OutQueueToken;
		QueuedDialogueRequests.Add(MoveTemp(Request));
		return false;
	}

	++ActiveDialogueSlots;
	OutQueueToken = 0;
	return true;
}

void FLLMConcurrencyManager::ReleaseDialogueSlot()
{
	check(IsInGameThread());
	ActiveDialogueSlots = FMath::Max(0, ActiveDialogueSlots - 1);
	PumpDialogueQueue();
}

void FLLMConcurrencyManager::PumpDialogueQueue()
{
	check(IsInGameThread());

	while (ActiveDialogueSlots < GetDialogueLimit())
	{
		int32 ValidIndex = INDEX_NONE;
		for (int32 i = 0; i < QueuedDialogueRequests.Num(); ++i)
		{
			if (QueuedDialogueRequests[i].Component.IsValid())
			{
				ValidIndex = i;
				break;
			}
		}

		if (ValidIndex == INDEX_NONE)
		{
			QueuedDialogueRequests.Reset();
			return;
		}

		const FQueuedRequest Request = QueuedDialogueRequests[ValidIndex];
		QueuedDialogueRequests.RemoveAt(ValidIndex);

		UAINpcComponent* Comp = Request.Component.Get();
		if (!Comp)
		{
			continue;
		}

		if (Comp->TryDispatchQueuedDialogueRequest(Request.QueueToken))
		{
			++ActiveDialogueSlots;
		}
	}
}

bool FLLMConcurrencyManager::TryDequeueDialogueRequest(UAINpcComponent* Component, uint64 QueueToken)
{
	check(IsInGameThread());
	check(Component);
	return Component->GetQueuedDialogueRequestToken() == QueueToken;
}

void FLLMConcurrencyManager::CancelQueuedDialogueRequest(UAINpcComponent* Component, uint64 QueueToken)
{
	check(IsInGameThread());
	QueuedDialogueRequests.RemoveAll([Component, QueueToken](const FQueuedRequest& Entry)
	{
		return Entry.QueueToken == QueueToken && Entry.Component.Get() == Component;
	});
}

bool FLLMConcurrencyManager::TryAcquireMemorySlot(UAINpcComponent* Component, uint64& OutQueueToken)
{
	check(IsInGameThread());
	check(Component);

	if (ActiveMemorySlots >= GetMemoryLimit())
	{
		OutQueueToken = NextMemoryQueueToken++;
		FQueuedRequest Request;
		Request.Component = Component;
		Request.QueueToken = OutQueueToken;
		QueuedMemoryRequests.Add(MoveTemp(Request));
		return false;
	}

	++ActiveMemorySlots;
	OutQueueToken = 0;
	return true;
}

void FLLMConcurrencyManager::ReleaseMemorySlot()
{
	check(IsInGameThread());
	ActiveMemorySlots = FMath::Max(0, ActiveMemorySlots - 1);
	PumpMemoryQueue();
}

void FLLMConcurrencyManager::PumpMemoryQueue()
{
	check(IsInGameThread());

	while (ActiveMemorySlots < GetMemoryLimit())
	{
		int32 ValidIndex = INDEX_NONE;
		for (int32 i = 0; i < QueuedMemoryRequests.Num(); ++i)
		{
			if (QueuedMemoryRequests[i].Component.IsValid())
			{
				ValidIndex = i;
				break;
			}
		}

		if (ValidIndex == INDEX_NONE)
		{
			QueuedMemoryRequests.Reset();
			return;
		}

		const FQueuedRequest Request = QueuedMemoryRequests[ValidIndex];
		QueuedMemoryRequests.RemoveAt(ValidIndex);

		UAINpcComponent* Comp = Request.Component.Get();
		if (!Comp)
		{
			continue;
		}

		if (Comp->TryAcquireQueuedMemoryMaintenanceSlot(Request.QueueToken))
		{
			++ActiveMemorySlots;
		}
	}
}

bool FLLMConcurrencyManager::TryDequeueMemoryRequest(UAINpcComponent* Component, uint64 QueueToken)
{
	check(IsInGameThread());
	check(Component);
	return Component->GetQueuedMemoryMaintenanceRequestToken() == QueueToken;
}

void FLLMConcurrencyManager::CancelQueuedMemoryRequest(UAINpcComponent* Component, uint64 QueueToken)
{
	check(IsInGameThread());
	QueuedMemoryRequests.RemoveAll([Component, QueueToken](const FQueuedRequest& Entry)
	{
		return Entry.QueueToken == QueueToken && Entry.Component.Get() == Component;
	});
}

#if WITH_EDITOR
void FLLMConcurrencyManager::ResetForTest()
{
	QueuedDialogueRequests.Reset();
	QueuedMemoryRequests.Reset();
	ActiveDialogueSlots = 0;
	ActiveMemorySlots = 0;
	NextDialogueQueueToken = 1;
	NextMemoryQueueToken = 1;
}
#endif
