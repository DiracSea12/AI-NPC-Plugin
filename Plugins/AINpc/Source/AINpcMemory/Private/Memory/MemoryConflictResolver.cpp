#include "Memory/MemoryConflictResolver.h"

FMemoryConflictResolver::FMemoryConflictResolver(TSharedPtr<ILLMProvider> InProvider)
	: LLMProvider(InProvider)
	, ActiveResolutions(0)
{
}

void FMemoryConflictResolver::SetConfig(const FMemoryConflictConfig& InConfig)
{
	Config = InConfig;
}

bool FMemoryConflictResolver::TryResolveConflict(const FMemoryEntry& NewEntry, const TArray<FMemoryEntry>& SimilarMemories, FMemoryEntry& OutResolvedEntry)
{
	if (ActiveResolutions.Load() >= Config.MaxConcurrentResolutions)
	{
		OutResolvedEntry = NewEntry;
		return true;
	}

	ActiveResolutions++;

	const EMemoryConflictOperation Operation = DetermineOperation(NewEntry, SimilarMemories);

	OutResolvedEntry = NewEntry;

	ActiveResolutions--;
	return true;
}

EMemoryConflictOperation FMemoryConflictResolver::DetermineOperation(const FMemoryEntry& NewEntry, const TArray<FMemoryEntry>& SimilarMemories)
{
	if (SimilarMemories.Num() == 0)
	{
		return EMemoryConflictOperation::Add;
	}

	return EMemoryConflictOperation::Add;
}
