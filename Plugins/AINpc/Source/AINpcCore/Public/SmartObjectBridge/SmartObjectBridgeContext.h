#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SmartObjectBridgeContext.generated.h"

class AActor;
class FSubsystemCollectionBase;
class UWorld;
struct FSmartObjectBridgeRuntimeData;

UCLASS()
class AINPCCORE_API USmartObjectBridgeContext : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "AI NPC|SmartObject")
	bool FindSlot(AActor* UserActor, float SearchRadius = 1000.0f);

	bool FindSlotWithPreferredTarget(AActor* UserActor, float SearchRadius, const FString& PreferredSlotId);

	UFUNCTION(BlueprintCallable, Category = "AI NPC|SmartObject")
	bool ClaimSlot(AActor* UserActor, int32 ClaimPriority = 2);

	UFUNCTION(BlueprintCallable, Category = "AI NPC|SmartObject")
	bool UseSlot();

	bool UseSlotForUser(AActor* UserActor);

	UFUNCTION(BlueprintCallable, Category = "AI NPC|SmartObject")
	bool ReleaseSlot();

	bool ReleaseSlotForUser(AActor* UserActor);

	UFUNCTION(BlueprintPure, Category = "AI NPC|SmartObject")
	bool HasActiveClaim() const;

	bool HasActiveClaimForUser(const AActor* UserActor) const;

	UFUNCTION(BlueprintPure, Category = "AI NPC|SmartObject")
	bool GetClaimedSlotTransform(FTransform& OutTransform) const;

	bool GetClaimedSlotTransformForUser(const AActor* UserActor, FTransform& OutTransform) const;

#if defined(WITH_DEV_AUTOMATION_TESTS) && WITH_DEV_AUTOMATION_TESTS
	void AddTrackedClaimForTest(UWorld* World);
	int32 GetTrackedClaimCountForTest() const;
	int32 GetTrackedClaimCountForWorldForTest(UWorld* World) const;
	void TriggerWorldCleanupForTest(UWorld* World);
#endif

private:
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	void ReleaseActiveClaimHandlesForWorld(UWorld* World);

	TUniquePtr<FSmartObjectBridgeRuntimeData> RuntimeData;
	FDelegateHandle WorldCleanupDelegateHandle;
};
