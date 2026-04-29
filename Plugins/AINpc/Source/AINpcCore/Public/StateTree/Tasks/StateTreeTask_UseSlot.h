#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "StateTreeTask_UseSlot.generated.h"

#define UE_API AINPCCORE_API

class USmartObjectBridgeContext;
class AActor;
enum class EStateTreeRunStatus : uint8;
struct FStateTreeExecutionContext;
struct FStateTreeTransitionResult;

USTRUCT()
struct FStateTreeTask_UseSlotInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<USmartObjectBridgeContext> BridgeContext = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<AActor> UserActor = nullptr;

	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bReleaseOnExit = true;
};

USTRUCT(meta = (DisplayName = "Use SmartObject Slot", Category = "AI NPC|SmartObject"))
struct FStateTreeTask_UseSlot : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeTask_UseSlotInstanceData;

	UE_API FStateTreeTask_UseSlot();

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	UE_API virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	UE_API virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

#undef UE_API
