#include "Retrieval/MemoryRetriever.h"
#include "Retrieval/SimpleRelevanceScorer.h"

FMemoryRetriever::FMemoryRetriever(TSharedPtr<IRelevanceScorer> InScorer)
	: RelevanceScorer(InScorer)
{
	if (!RelevanceScorer.IsValid())
	{
		RelevanceScorer = MakeShared<FSimpleRelevanceScorer>();
	}
}

void FMemoryRetriever::SetConfig(const FRetrievalConfig& InConfig)
{
	Config = InConfig;
}

TArray<FMemoryEntry> FMemoryRetriever::RetrieveRelevant(const TArray<FMemoryEntry>& Memories, const FString& Query, int32 MaxResults)
{
	const FDateTime CurrentTime = FDateTime::Now();
	TArray<TPair<float, FMemoryEntry>> ScoredMemories;

	for (const FMemoryEntry& Entry : Memories)
	{
		const float Score = CalculateScore(Entry, Query, CurrentTime);
		ScoredMemories.Add(TPair<float, FMemoryEntry>(Score, Entry));
	}

	ScoredMemories.Sort([](const TPair<float, FMemoryEntry>& A, const TPair<float, FMemoryEntry>& B)
	{
		return A.Key > B.Key;
	});

	TArray<FMemoryEntry> Results;
	for (int32 i = 0; i < FMath::Min(MaxResults, ScoredMemories.Num()); ++i)
	{
		Results.Add(ScoredMemories[i].Value);
	}

	return Results;
}

float FMemoryRetriever::CalculateScore(const FMemoryEntry& Entry, const FString& Query, const FDateTime& CurrentTime) const
{
	const float Recency = CalculateRecency(Entry.Timestamp, CurrentTime);
	const float Importance = Entry.Importance;
	const float Relevance = RelevanceScorer->CalculateRelevance(Query, Entry.Content);

	return Config.RecencyWeight * Recency + Config.ImportanceWeight * Importance + Config.RelevanceWeight * Relevance;
}

float FMemoryRetriever::CalculateRecency(const FDateTime& Timestamp, const FDateTime& CurrentTime) const
{
	const FTimespan TimeDiff = CurrentTime - Timestamp;
	const float HoursElapsed = TimeDiff.GetTotalHours();
	const float DecayFactor = FMath::Exp(-0.693f * HoursElapsed / Config.DecayHalfLifeHours);
	return FMath::Clamp(DecayFactor, 0.0f, 1.0f);
}
