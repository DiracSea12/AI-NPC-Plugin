#include "SmartObjectBridge/AINpcSmartObjectRuntimeExecutor.h"

#include "Components/AINpcComponent.h"
#include "LLM/LLMResponseParser.h"
#include "SmartObjectBridge/SmartObjectBridgeContext.h"

bool FAINpcSmartObjectRuntimeExecutor::TryExecuteLatestActionIntent(
	UAINpcComponent& NpcComponent,
	AActor& UserActor,
	USmartObjectBridgeContext& BridgeContext,
	const float SearchRadius,
	const int32 ClaimPriority,
	FAINpcSmartObjectRuntimeExecutionResult& OutResult)
{
	OutResult = FAINpcSmartObjectRuntimeExecutionResult();

	FNpcAction ActionIntent;
	if (!NpcComponent.TryGetLatestActionIntent(ActionIntent))
	{
		OutResult.FailureReason = TEXT("No executable SmartObject action intent was parsed from the real provider response.");
		return false;
	}

	OutResult.ActionType = ActionIntent.ActionType.TrimStartAndEnd();

	const TArray<FString> LegalTargets = NpcComponent.GetAvailableSmartObjectTargetsForExecution();
	if (LegalTargets.IsEmpty())
	{
		OutResult.FailureReason = TEXT("No legal SmartObject targets were available at runtime for execution.");
		return false;
	}

	FString RequestedTarget;
	if (!ValidateSmartObjectActionIntent(ActionIntent, LegalTargets, RequestedTarget, OutResult.FailureReason))
	{
		return false;
	}

	OutResult.RequestedTarget = RequestedTarget;

	if (!BridgeContext.FindSlotWithPreferredTarget(&UserActor, SearchRadius, RequestedTarget))
	{
		OutResult.FailureReason = FString::Printf(
			TEXT("Runtime SmartObject search could not resolve requested target '%s'."),
			*RequestedTarget);
		return false;
	}

	if (!BridgeContext.ClaimSlot(&UserActor, ClaimPriority))
	{
		OutResult.FailureReason = FString::Printf(
			TEXT("Runtime SmartObject claim failed for target '%s'."),
			*RequestedTarget);
		return false;
	}

	if (!BridgeContext.UseSlotForUser(&UserActor))
	{
		BridgeContext.ReleaseSlotForUser(&UserActor);
		OutResult.FailureReason = FString::Printf(
			TEXT("Runtime SmartObject use failed after claiming target '%s'."),
			*RequestedTarget);
		return false;
	}

	if (!BridgeContext.GetClaimedSlotTransformForUser(&UserActor, OutResult.ClaimedSlotTransform))
	{
		BridgeContext.ReleaseSlotForUser(&UserActor);
		OutResult.FailureReason = FString::Printf(
			TEXT("SmartObject target '%s' was occupied but no claimed slot transform could be resolved."),
			*RequestedTarget);
		return false;
	}

	return true;
}

bool FAINpcSmartObjectRuntimeExecutor::ValidateSmartObjectActionIntent(
	const FNpcAction& ActionIntent,
	const TArray<FString>& LegalTargets,
	FString& OutRequestedTarget,
	FString& OutFailureReason)
{
	OutRequestedTarget.Reset();

	const FString ActionType = ActionIntent.ActionType.TrimStartAndEnd();
	if (ActionType.IsEmpty() || ActionType.Equals(AINpc::Actions::DefaultTalkActionType, ESearchCase::CaseSensitive))
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
