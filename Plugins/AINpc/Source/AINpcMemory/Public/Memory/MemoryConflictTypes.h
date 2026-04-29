#pragma once

#include "CoreMinimal.h"
#include "MemoryConflictTypes.generated.h"

UENUM(BlueprintType)
enum class EMemoryConflictOperation : uint8
{
	Add UMETA(DisplayName = "Add"),
	Update UMETA(DisplayName = "Update"),
	Merge UMETA(DisplayName = "Merge"),
	Contradict UMETA(DisplayName = "Contradict"),
	Ignore UMETA(DisplayName = "Ignore")
};

USTRUCT(BlueprintType)
struct AINPCMEMORY_API FMemoryConflictConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	int32 MaxConcurrentResolutions = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	int32 SimilarityTopK = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	TArray<EMemoryConflictOperation> EnabledOperations = {
		EMemoryConflictOperation::Add,
		EMemoryConflictOperation::Update,
		EMemoryConflictOperation::Merge,
		EMemoryConflictOperation::Contradict,
		EMemoryConflictOperation::Ignore
	};
};
