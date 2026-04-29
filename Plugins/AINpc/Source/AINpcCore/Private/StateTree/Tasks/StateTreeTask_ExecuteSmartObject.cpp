#include "StateTree/Tasks/StateTreeTask_ExecuteSmartObject.h"

#include "Components/AINpcComponent.h"
#include "Engine/GameInstance.h"
#include "SmartObjectBridge/SmartObjectBridgeContext.h"
#include "StateTree/Tasks/StateTreeTask_SmartObjectTaskUtils.h"
#include "StateTreeExecutionContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StateTreeTask_ExecuteSmartObject)

namespace
{
UAINpcComponent* ResolveNpcComponent(const FStateTreeExecutionContext& Context, FStateTreeTask_ExecuteSmartObjectInstanceData& InstanceData)
{
	if (InstanceData.NpcComponent)
	{
		return InstanceData.NpcComponent.Get();
	}

	if (AActor* OwnerActor = Cast<AActor>(Context.GetOwner()))
	{
		if (UAINpcComponent* FoundComponent = OwnerActor->FindComponentByClass<UAINpcComponent>())
		{
			InstanceData.NpcComponent = FoundComponent;
			return FoundComponent;
		}
	}

	if (AActor* UserActor = AINpc::SmartObjectTaskUtils::ResolveUserActorFromOwnerObject(Context.GetOwner(), InstanceData.UserActor))
	{
		if (UAINpcComponent* FoundComponent = UserActor->FindComponentByClass<UAINpcComponent>())
		{
			InstanceData.NpcComponent = FoundComponent;
			return FoundComponent;
		}
	}

	return nullptr;
}

AActor* ResolveUserActor(const FStateTreeExecutionContext& Context, FStateTreeTask_ExecuteSmartObjectInstanceData& InstanceData)
{
	return AINpc::SmartObjectTaskUtils::ResolveUserActorFromOwnerObject(Context.GetOwner(), InstanceData.UserActor);
}

USmartObjectBridgeContext* ResolveBridgeContext(const FStateTreeExecutionContext& Context, FStateTreeTask_ExecuteSmartObjectInstanceData& InstanceData)
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

bool ValidateSmartObjectAction(
	const FNpcAction& ActionIntent,
	const TArray<FString>& LegalTargets,
	FString& OutRequestedTarget,
	FString& OutFailureReason)
{
	OutRequestedTarget.Reset();
	const FString ActionType = ActionIntent.ActionType.TrimStartAndEnd();
	if (ActionType.IsEmpty() || ActionType.Equals(TEXT("Action.DefaultTalk"), ESearchCase::CaseSensitive))
	{
		OutFailureReason = TEXT("LLM action intent is missing or non-executable.");
		return false;
	}

	const FString RequestedTarget = ActionIntent.Target.TrimStartAndEnd();
	if (RequestedTarget.IsEmpty())
	{
		OutFailureReason = TEXT("SmartObject action intent is missing a target.");
		return false;
	}

	for (const FString& LegalTarget : LegalTargets)
	{
		if (RequestedTarget.Equals(LegalTarget.TrimStartAndEnd(), ESearchCase::CaseSensitive))
		{
			OutRequestedTarget = RequestedTarget;
			OutFailureReason.Reset();
			return true;
		}
	}

	OutFailureReason = FString::Printf(
		TEXT("SmartObject action target '%s' is not in the legal runtime whitelist."),
		*RequestedTarget);
	return false;
}
}

FStateTreeTask_ExecuteSmartObject::FStateTreeTask_ExecuteSmartObject()
{
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FStateTreeTask_ExecuteSmartObject::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	UAINpcComponent* NpcComponent = ResolveNpcComponent(Context, InstanceData);
	AActor* UserActor = ResolveUserActor(Context, InstanceData);
	USmartObjectBridgeContext* BridgeContext = ResolveBridgeContext(Context, InstanceData);
	if (!IsValid(NpcComponent) || !IsValid(UserActor) || !IsValid(BridgeContext))
	{
		return EStateTreeRunStatus::Failed;
	}

	FNpcAction ActionIntent;
	if (!NpcComponent->TryGetLatestActionIntent(ActionIntent))
	{
		return EStateTreeRunStatus::Failed;
	}

	const TArray<FString> LegalTargets = NpcComponent->GetAvailableSmartObjectTargetsForExecution();
	FString RequestedTarget;
	FString ValidationFailureReason;
	if (!ValidateSmartObjectAction(ActionIntent, LegalTargets, RequestedTarget, ValidationFailureReason))
	{
		return EStateTreeRunStatus::Failed;
	}

	if (!BridgeContext->FindSlotWithPreferredTarget(UserActor, InstanceData.SearchRadius, RequestedTarget))
	{
		return EStateTreeRunStatus::Failed;
	}

	if (!BridgeContext->ClaimSlot(UserActor, InstanceData.ClaimPriority))
	{
		return EStateTreeRunStatus::Failed;
	}

	if (!BridgeContext->UseSlotForUser(UserActor))
	{
		BridgeContext->ReleaseSlotForUser(UserActor);
		return EStateTreeRunStatus::Failed;
	}

	return EStateTreeRunStatus::Succeeded;
}

void FStateTreeTask_ExecuteSmartObject::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	USmartObjectBridgeContext* BridgeContext = ResolveBridgeContext(Context, InstanceData);
	AActor* UserActor = ResolveUserActor(Context, InstanceData);
	if (IsValid(UserActor) && IsValid(BridgeContext) && InstanceData.bReleaseOnExit && BridgeContext->HasActiveClaimForUser(UserActor))
	{
		BridgeContext->ReleaseSlotForUser(UserActor);
	}
}

#if WITH_EDITOR
bool FStateTreeTask_ExecuteSmartObject::ValidateSmartObjectActionForTest(
	const FNpcAction& ActionIntent,
	const TArray<FString>& LegalTargets,
	FString& OutFailureReason)
{
	FString RequestedTarget;
	return ValidateSmartObjectAction(ActionIntent, LegalTargets, RequestedTarget, OutFailureReason);
}
#endif
