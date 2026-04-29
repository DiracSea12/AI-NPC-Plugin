#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "StateTreeTask_FindSlot.generated.h"

#define UE_API AINPCCORE_API

class AActor;
class USmartObjectBridgeContext;
enum class EStateTreeRunStatus : uint8;
struct FStateTreeExecutionContext;
struct FStateTreeTransitionResult;

USTRUCT()
struct FStateTreeTask_FindSlotInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<USmartObjectBridgeContext> BridgeContext = nullptr;

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AActor> UserActor = nullptr;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SearchRadius = 1000.0f;
};

USTRUCT(meta = (DisplayName = "Find SmartObject Slot", Category = "AI NPC|SmartObject"))
struct FStateTreeTask_FindSlot : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeTask_FindSlotInstanceData;

	UE_API FStateTreeTask_FindSlot();

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	UE_API virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

#undef UE_API
