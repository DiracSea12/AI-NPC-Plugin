#include "StateTree/Tasks/StateTreeTask_UseSlot.h"

#include "Engine/GameInstance.h"
#include "SmartObjectBridge/SmartObjectBridgeContext.h"
#include "StateTree/Tasks/StateTreeTask_SmartObjectTaskUtils.h"
#include "StateTreeExecutionContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StateTreeTask_UseSlot)

namespace
{
AActor* ResolveUserActor(const FStateTreeExecutionContext& Context, TObjectPtr<AActor>& CachedUserActor)
{
	return AINpc::SmartObjectTaskUtils::ResolveUserActorFromOwnerObject(Context.GetOwner(), CachedUserActor);
}

USmartObjectBridgeContext* ResolveBridgeContext(const FStateTreeExecutionContext& Context, FStateTreeTask_UseSlotInstanceData& InstanceData)
{
	if (InstanceData.BridgeContext)
	{
		return InstanceData.BridgeContext.Get();
	}

	AActor* OwnerActor = ResolveUserActor(Context, InstanceData.UserActor);

	if (!IsValid(OwnerActor))
	{
		return nullptr;
	}

	UGameInstance* GameInstance = OwnerActor->GetGameInstance();
	if (!IsValid(GameInstance))
	{
		return nullptr;
	}

	USmartObjectBridgeContext* BridgeContext = GameInstance->GetSubsystem<USmartObjectBridgeContext>();
	InstanceData.BridgeContext = BridgeContext;
	return BridgeContext;
}
}

FStateTreeTask_UseSlot::FStateTreeTask_UseSlot()
{
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FStateTreeTask_UseSlot::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	USmartObjectBridgeContext* BridgeContext = ResolveBridgeContext(Context, InstanceData);
	if (!IsValid(BridgeContext))
	{
		return EStateTreeRunStatus::Failed;
	}

	return BridgeContext->UseSlot()
		? EStateTreeRunStatus::Succeeded
		: EStateTreeRunStatus::Failed;
}

void FStateTreeTask_UseSlot::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	USmartObjectBridgeContext* BridgeContext = ResolveBridgeContext(Context, InstanceData);
	if (IsValid(BridgeContext) && InstanceData.bReleaseOnExit && BridgeContext->HasActiveClaim())
	{
		BridgeContext->ReleaseSlot();
	}
}
