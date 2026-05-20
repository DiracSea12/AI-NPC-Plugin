#pragma once

#include "CoreMinimal.h"
#include "LLM/LLMProviderTypes.h"

class AActor;
class UAINpcComponent;
class USmartObjectBridgeContext;

struct FAINpcSmartObjectRuntimeExecutionResult
{
	FString ActionType;
	FString RequestedTarget;
	FTransform ClaimedSlotTransform = FTransform::Identity;
	FString FailureReason;
};

class FAINpcSmartObjectRuntimeExecutor
{
public:
	static bool TryExecuteLatestActionIntent(
		UAINpcComponent& NpcComponent,
		AActor& UserActor,
		USmartObjectBridgeContext& BridgeContext,
		float SearchRadius,
		int32 ClaimPriority,
		FAINpcSmartObjectRuntimeExecutionResult& OutResult);

	static bool ValidateSmartObjectActionIntent(
		const FNpcAction& ActionIntent,
		const TArray<FString>& LegalTargets,
		FString& OutRequestedTarget,
		FString& OutFailureReason);
};
