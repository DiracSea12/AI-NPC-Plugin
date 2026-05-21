#include "Test/AINpcSmartObjectActionTestSupport.h"

#include "Components/AINpcComponent.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include "SmartObjectBridge/AINpcSmartObjectRuntimeExecutor.h"
#include "SmartObjectBridge/SmartObjectBridgeContext.h"

namespace AINpcSmartObjectActionTestSupport
{
	bool TryExecuteLatestActionIntent(
		UAINpcComponent* NpcComponent,
		AActor& UserActor,
		const float SearchRadius,
		const int32 ClaimPriority,
		FAINpcVisualSmartObjectActionExecution& OutExecution,
		FString& OutFailureReason)
	{
		if (!NpcComponent)
		{
			OutFailureReason = TEXT("SmartObject action execution failed because NPC component is null.");
			return false;
		}

		UGameInstance* GameInstance = UserActor.GetGameInstance();
		USmartObjectBridgeContext* BridgeContext = GameInstance ? GameInstance->GetSubsystem<USmartObjectBridgeContext>() : nullptr;
		if (!BridgeContext)
		{
			OutFailureReason = TEXT("SmartObject bridge subsystem was unavailable during action verification.");
			return false;
		}

		FAINpcSmartObjectRuntimeExecutionResult ExecutionResult;
		if (!FAINpcSmartObjectRuntimeExecutor::TryExecuteLatestActionIntent(*NpcComponent, UserActor, *BridgeContext, SearchRadius, ClaimPriority, ExecutionResult))
		{
			OutFailureReason = FString::Printf(TEXT("SmartObject action was not executable: %s"), *ExecutionResult.FailureReason);
			return false;
		}

		OutExecution.ActionType = MoveTemp(ExecutionResult.ActionType);
		OutExecution.RequestedTarget = MoveTemp(ExecutionResult.RequestedTarget);
		OutExecution.ClaimedSlotTransform = ExecutionResult.ClaimedSlotTransform;
		return true;
	}
}
