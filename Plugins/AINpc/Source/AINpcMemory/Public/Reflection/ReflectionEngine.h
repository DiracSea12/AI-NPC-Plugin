#pragma once

#include "CoreMinimal.h"
#include "Memory/MemoryTypes.h"
#include "ReflectionEngine.generated.h"

UCLASS()
class AINPCMEMORY_API UReflectionEngine : public UObject
{
	GENERATED_BODY()

public:
	bool ShouldTriggerReflection(const TArray<FMemoryEntry>& RecentMemories, float Threshold = 150.0f) const;
	FMemoryEntry GenerateInsight(const TArray<FMemoryEntry>& SourceMemories);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reflection")
	float ReflectionThreshold = 150.0f;

private:
	float CalculateCumulativeImportance(const TArray<FMemoryEntry>& Memories) const;
};
