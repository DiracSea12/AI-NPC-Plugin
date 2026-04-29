#pragma once

#include "CoreMinimal.h"
#include "MemoryTypes.generated.h"

USTRUCT(BlueprintType)
struct AINPCMEMORY_API FMemoryEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Memory")
	FString Content;

	UPROPERTY(BlueprintReadWrite, Category = "Memory")
	FDateTime Timestamp;

	UPROPERTY(BlueprintReadWrite, Category = "Memory")
	float Importance = 0.5f;

	UPROPERTY(BlueprintReadWrite, Category = "Memory")
	int32 AccessCount = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Memory")
	int32 SchemaVersion = 1;

	UPROPERTY(BlueprintReadWrite, Category = "Memory")
	bool bContradicted = false;

	UPROPERTY(BlueprintReadWrite, Category = "Memory")
	TArray<float> Embedding;

	FMemoryEntry()
		: Timestamp(FDateTime::Now())
	{
	}
};
