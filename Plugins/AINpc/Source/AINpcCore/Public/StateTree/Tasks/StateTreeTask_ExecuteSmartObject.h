#pragma once

#include "CoreMinimal.h"
#include "LLM/LLMResponseParser.h"
#include "StateTreeTaskBase.h"
#include "StateTreeTask_ExecuteSmartObject.generated.h"

#define UE_API AINPCCORE_API

class UAINpcComponent;
class AActor;
class USmartObjectBridgeContext;
enum class EStateTreeRunStatus : uint8;
struct FStateTreeExecutionContext;
struct FStateTreeTransitionResult;

USTRUCT()
struct FStateTreeTask_ExecuteSmartObjectInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<UAINpcComponent> NpcComponent = nullptr;

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<USmartObjectBridgeContext> BridgeContext = nullptr;

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<AActor> UserActor = nullptr;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SearchRadius = 1000.0f;

	UPROPERTY(EditAnywhere, Category = Parameter)
	int32 ClaimPriority = 2;

	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bReleaseOnExit = true;
};

USTRUCT(meta = (DisplayName = "Execute SmartObject Action", Category = "AI NPC|SmartObject"))
struct FStateTreeTask_ExecuteSmartObject : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeTask_ExecuteSmartObjectInstanceData;

	UE_API FStateTreeTask_ExecuteSmartObject();

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	UE_API virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	UE_API virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	UE_API static bool ValidateSmartObjectActionForTest(
		const FNpcAction& ActionIntent,
		const TArray<FString>& LegalTargets,
		FString& OutFailureReason);
#endif
};

#undef UE_API
