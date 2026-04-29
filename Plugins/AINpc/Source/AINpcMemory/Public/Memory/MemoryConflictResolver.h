#pragma once

#include "CoreMinimal.h"
#include "Memory/MemoryTypes.h"
#include "Memory/MemoryConflictTypes.h"

class ILLMProvider;

class AINPCMEMORY_API FMemoryConflictResolver
{
public:
	explicit FMemoryConflictResolver(TSharedPtr<ILLMProvider> InProvider);

	void SetConfig(const FMemoryConflictConfig& InConfig);
	bool TryResolveConflict(const FMemoryEntry& NewEntry, const TArray<FMemoryEntry>& SimilarMemories, FMemoryEntry& OutResolvedEntry);

private:
	EMemoryConflictOperation DetermineOperation(const FMemoryEntry& NewEntry, const TArray<FMemoryEntry>& SimilarMemories);

private:
	TSharedPtr<ILLMProvider> LLMProvider;
	FMemoryConflictConfig Config;
	TAtomic<int32> ActiveResolutions;
};
