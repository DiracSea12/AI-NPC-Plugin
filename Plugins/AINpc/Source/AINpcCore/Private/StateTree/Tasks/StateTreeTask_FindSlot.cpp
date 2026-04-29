#include "StateTree/Tasks/StateTreeTask_FindSlot.h"

#include "Engine/GameInstance.h"
#include "SmartObjectBridge/SmartObjectBridgeContext.h"
#include "StateTree/Tasks/StateTreeTask_SmartObjectTaskUtils.h"
#include "StateTreeExecutionContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StateTreeTask_FindSlot)

namespace
{
AActor* ResolveUserActor(const FStateTreeExecutionContext& Context, FStateTreeTask_FindSlotInstanceData& InstanceData)
{
	return AINpc::SmartObjectTaskUtils::ResolveUserActorFromOwnerObject(Context.GetOwner(), InstanceData.UserActor);
}

USmartObjectBridgeContext* ResolveBridgeContext(const FStateTreeExecutionContext& Context, FStateTreeTask_FindSlotInstanceData& InstanceData)
{
	if (InstanceData.BridgeContext)
	{
		return InstanceData.BridgeContext.Get();
	}

	AActor* UserActor = ResolveUserActor(Context, InstanceData);
	if (!IsValid(UserActor))
	{
		return nullptr;
	}

	UGameInstance* GameInstance = UserActor->GetGameInstance();
	if (!IsValid(GameInstance))
	{
		return nullptr;
	}

	USmartObjectBridgeContext* BridgeContext = GameInstance->GetSubsystem<USmartObjectBridgeContext>();
	InstanceData.BridgeContext = BridgeContext;
	return BridgeContext;
}
}

FStateTreeTask_FindSlot::FStateTreeTask_FindSlot()
{
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FStateTreeTask_FindSlot::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	USmartObjectBridgeContext* BridgeContext = ResolveBridgeContext(Context, InstanceData);
	AActor* UserActor = ResolveUserActor(Context, InstanceData);
	if (!IsValid(BridgeContext) || !IsValid(UserActor))
	{
		return EStateTreeRunStatus::Failed;
	}

	return BridgeContext->FindSlot(UserActor, InstanceData.SearchRadius)
		? EStateTreeRunStatus::Succeeded
		: EStateTreeRunStatus::Failed;
}
