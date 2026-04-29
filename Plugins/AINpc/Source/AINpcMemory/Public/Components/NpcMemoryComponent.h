#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Memory/MemoryTypes.h"
#include "NpcMemoryComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class AINPCMEMORY_API UNpcMemoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNpcMemoryComponent();

	UFUNCTION(BlueprintCallable, Category = "Memory")
	void AddWorkingMemory(const FMemoryEntry& Entry);

	UFUNCTION(BlueprintCallable, Category = "Memory")
	void AddEpisodicMemory(const FMemoryEntry& Entry);

	UFUNCTION(BlueprintCallable, Category = "Memory")
	void ClearWorkingMemory();

	UFUNCTION(BlueprintPure, Category = "Memory")
	const TArray<FMemoryEntry>& GetWorkingMemory() const { return WorkingMemory; }

	UFUNCTION(BlueprintPure, Category = "Memory")
	const TArray<FMemoryEntry>& GetEpisodicMemory() const { return EpisodicMemory; }

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	int32 MaxWorkingMemorySize = 20;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory")
	int32 MaxEpisodicMemorySize = 200;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Memory", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ImportanceThreshold = 0.3f;

private:
	float CalculateEvictionScore(const FMemoryEntry& Entry) const;
	UPROPERTY()
	TArray<FMemoryEntry> WorkingMemory;

	UPROPERTY()
	TArray<FMemoryEntry> EpisodicMemory;
};
