#pragma once

#include "CoreMinimal.h"

class AActor;
class UAINpcComponent;

struct FAINpcVisualSmartObjectActionExecution
{
	FString ActionType;
	FString RequestedTarget;
	FTransform ClaimedSlotTransform = FTransform::Identity;
};

namespace AINpcSmartObjectActionTestSupport
{
	bool TryExecuteLatestActionIntent(
		UAINpcComponent* NpcComponent,
		AActor& UserActor,
		float SearchRadius,
		int32 ClaimPriority,
		FAINpcVisualSmartObjectActionExecution& OutExecution,
		FString& OutFailureReason);
}
