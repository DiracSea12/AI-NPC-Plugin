#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "StateTreeTask_ClaimSlot.generated.h"

#define UE_API AINPCCORE_API

class AActor;
class USmartObjectBridgeContext;
enum class EStateTreeRunStatus : uint8;
struct FStateTreeExecutionContext;
struct FStateTreeTransitionResult;

USTRUCT()
struct FStateTreeTask_ClaimSlotInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<USmartObjectBridgeContext> BridgeContext = nullptr;

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AActor> UserActor = nullptr;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0", ClampMax = "4", UIMin = "0", UIMax = "4"))
	int32 ClaimPriority = 2;
};

USTRUCT(meta = (DisplayName = "Claim SmartObject Slot", Category = "AI NPC|SmartObject"))
struct FStateTreeTask_ClaimSlot : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeTask_ClaimSlotInstanceData;

	UE_API FStateTreeTask_ClaimSlot();

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	UE_API virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

#undef UE_API
