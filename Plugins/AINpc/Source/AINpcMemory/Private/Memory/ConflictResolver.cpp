#include "Memory/ConflictResolver.h"
#include "Embedding/VectorSimilarity.h"
#include "Algo/LevenshteinDistance.h"

FConflictResolutionResult UConflictResolver::ResolveConflict(
	const FMemoryEntry& NewEntry,
	const TArray<FMemoryEntry>& ExistingMemories,
	int32 TopK,
	float SimilarityThreshold)
{
	FConflictResolutionResult Result;
	Result.Operation = EConflictOperation::ADD;

	if (ActiveResolutions.Load() >= MaxConcurrentResolutions)
	{
		return Result;
	}

	const TArray<int32> SimilarIndices = FindSimilarMemories(NewEntry, ExistingMemories, TopK, SimilarityThreshold);
	if (SimilarIndices.Num() == 0)
	{
		return Result;
	}

	ActiveResolutions++;

	const int32 MostSimilarIndex = SimilarIndices[0];
	const FMemoryEntry& SimilarEntry = ExistingMemories[MostSimilarIndex];

	if (EnabledOperations.Contains(EConflictOperation::UPDATE) && NewEntry.Importance > SimilarEntry.Importance)
	{
		Result.Operation = EConflictOperation::UPDATE;
		Result.TargetIndex = MostSimilarIndex;
	}
	else if (EnabledOperations.Contains(EConflictOperation::COEXIST))
	{
		Result.Operation = EConflictOperation::COEXIST;
	}

	ActiveResolutions--;
	return Result;
}

float UConflictResolver::CalculateSimilarity(const FString& A, const FString& B) const
{
	if (A.IsEmpty() || B.IsEmpty())
	{
		return 0.0f;
	}

	const int32 Distance = Algo::LevenshteinDistance(A, B);
	const int32 MaxLen = FMath::Max(A.Len(), B.Len());
	return 1.0f - static_cast<float>(Distance) / MaxLen;
}

float UConflictResolver::CalculateVectorSimilarity(const TArray<float>& A, const TArray<float>& B) const
{
	return FVectorSimilarity::CosineSimilarity(A, B);
}

TArray<int32> UConflictResolver::FindSimilarMemories(
	const FMemoryEntry& NewEntry,
	const TArray<FMemoryEntry>& ExistingMemories,
	int32 TopK,
	float Threshold) const
{
	TArray<TPair<int32, float>> Scores;
	const bool bUseVectorSearch = NewEntry.Embedding.Num() > 0;

	for (int32 i = 0; i < ExistingMemories.Num(); ++i)
	{
		float Similarity = 0.0f;
		if (bUseVectorSearch && ExistingMemories[i].Embedding.Num() > 0)
		{
			Similarity = CalculateVectorSimilarity(NewEntry.Embedding, ExistingMemories[i].Embedding);
		}
		else
		{
			Similarity = CalculateSimilarity(NewEntry.Content, ExistingMemories[i].Content);
		}

		if (Similarity >= Threshold)
		{
			Scores.Add(TPair<int32, float>(i, Similarity));
		}
	}

	Scores.Sort([](const TPair<int32, float>& A, const TPair<int32, float>& B)
	{
		return A.Value > B.Value;
	});

	TArray<int32> Result;
	for (int32 i = 0; i < FMath::Min(TopK, Scores.Num()); ++i)
	{
		Result.Add(Scores[i].Key);
	}
	return Result;
}
