#pragma once

#include "CoreMinimal.h"
#include "Memory/MemoryTypes.h"
#include "IRelevanceScorer.h"

struct AINPCMEMORY_API FRetrievalConfig
{
	float RecencyWeight = 0.33f;
	float ImportanceWeight = 0.33f;
	float RelevanceWeight = 0.34f;
	float DecayHalfLifeHours = 24.0f;
};

class AINPCMEMORY_API FMemoryRetriever
{
public:
	explicit FMemoryRetriever(TSharedPtr<IRelevanceScorer> InScorer = nullptr);

	void SetConfig(const FRetrievalConfig& InConfig);
	TArray<FMemoryEntry> RetrieveRelevant(const TArray<FMemoryEntry>& Memories, const FString& Query, int32 MaxResults = 10);

private:
	float CalculateScore(const FMemoryEntry& Entry, const FString& Query, const FDateTime& CurrentTime) const;
	float CalculateRecency(const FDateTime& Timestamp, const FDateTime& CurrentTime) const;

private:
	TSharedPtr<IRelevanceScorer> RelevanceScorer;
	FRetrievalConfig Config;
};
