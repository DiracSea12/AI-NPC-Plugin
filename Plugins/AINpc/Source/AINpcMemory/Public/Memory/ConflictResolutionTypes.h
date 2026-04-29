#pragma once

#include "CoreMinimal.h"
#include "ConflictResolutionTypes.generated.h"

UENUM(BlueprintType)
enum class EConflictOperation : uint8
{
	ADD UMETA(DisplayName = "Add"),
	UPDATE UMETA(DisplayName = "Update"),
	MERGE UMETA(DisplayName = "Merge"),
	COEXIST UMETA(DisplayName = "Coexist"),
	SUPERSEDE UMETA(DisplayName = "Supersede")
};

USTRUCT(BlueprintType)
struct AINPCMEMORY_API FConflictResolutionResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Memory")
	EConflictOperation Operation = EConflictOperation::ADD;

	UPROPERTY(BlueprintReadWrite, Category = "Memory")
	int32 TargetIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadWrite, Category = "Memory")
	FString MergedContent;
};
