#pragma once

#include "CoreMinimal.h"
#include "Memory/MemoryTypes.h"
#include "Memory/ConflictResolutionTypes.h"
#include "ConflictResolver.generated.h"

UCLASS()
class AINPCMEMORY_API UConflictResolver : public UObject
{
	GENERATED_BODY()

public:
	FConflictResolutionResult ResolveConflict(
		const FMemoryEntry& NewEntry,
		const TArray<FMemoryEntry>& ExistingMemories,
		int32 TopK = 5,
		float SimilarityThreshold = 0.75f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	TSet<EConflictOperation> EnabledOperations = {
		EConflictOperation::ADD,
		EConflictOperation::UPDATE,
		EConflictOperation::COEXIST
	};

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	int32 MaxConcurrentResolutions = 2;

private:
	float CalculateSimilarity(const FString& A, const FString& B) const;
	float CalculateVectorSimilarity(const TArray<float>& A, const TArray<float>& B) const;
	TArray<int32> FindSimilarMemories(
		const FMemoryEntry& NewEntry,
		const TArray<FMemoryEntry>& ExistingMemories,
		int32 TopK,
		float Threshold) const;

	TAtomic<int32> ActiveResolutions{0};
};
