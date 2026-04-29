#pragma once

#include "CoreMinimal.h"
#include "Memory/MemoryTypes.h"

class AINPCMEMORY_API FNpcMemoryStore
{
public:
	FNpcMemoryStore();
	~FNpcMemoryStore();

	bool SaveMemory(const FString& NpcId, const TArray<FMemoryEntry>& Memories);
	bool LoadMemory(const FString& NpcId, TArray<FMemoryEntry>& OutMemories);
	bool SearchMemory(const FString& NpcId, const FString& Query, TArray<FMemoryEntry>& OutResults);
	bool ClearMemory(const FString& NpcId);

private:
	FString GetStoragePath(const FString& NpcId) const;
};
