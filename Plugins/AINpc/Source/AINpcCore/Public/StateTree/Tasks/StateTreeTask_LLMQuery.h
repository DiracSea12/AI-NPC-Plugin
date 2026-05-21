#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "StateTreeTask_LLMQuery.generated.h"

#define UE_API AINPCCORE_API

class UAINpcComponent;
enum class EStateTreeRunStatus : uint8;
struct FStateTreeExecutionContext;
struct FStateTreeTransitionResult;

USTRUCT()
struct FStateTreeTask_LLMQueryInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<UAINpcComponent> NpcComponent = nullptr;

	UPROPERTY(EditAnywhere, Category = Parameter)
	FString PlayerInput;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.1", UIMin = "0.1"))
	float TimeoutSeconds = 30.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0", UIMin = "0.0"))
	float TimeoutGraceSeconds = 0.5f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SpeakingDurationSeconds = 1.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0", UIMin = "0.0"))
	float CooldownDurationSeconds = 0.5f;

	UPROPERTY(Transient)
	float ElapsedSeconds = 0.0f;

	UPROPERTY(Transient)
	float SpeakingElapsedSeconds = 0.0f;

	UPROPERTY(Transient)
	float CooldownElapsedSeconds = 0.0f;

	UPROPERTY(Transient)
	float TimeoutExceededAtSeconds = 0.0f;

	UPROPERTY(Transient)
	bool bTimeoutExceeded = false;

	UPROPERTY(Transient)
	bool bStartedRequest = false;
};

/**
 * Phase-1 dialogue state flow contract:
 * Idle -> WaitingForLLM -> Speaking -> Cooldown -> Idle.
 * If waiting exceeds TimeoutSeconds, the task enters a soft-timeout grace window
 * before failing and resetting to Idle.
 */
USTRUCT(meta = (DisplayName = "LLM Query", Category = "AI NPC|Dialogue"))
struct FStateTreeTask_LLMQuery : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeTask_LLMQueryInstanceData;

	UE_API FStateTreeTask_LLMQuery();

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	UE_API virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	UE_API virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	UE_API virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	UE_API static void ApplyExitStateCleanupForTest(UAINpcComponent* NpcComponent);
	UE_API static EStateTreeRunStatus EvaluateWaitingStateForTest(
		FStateTreeTask_LLMQueryInstanceData& InstanceData,
		float DeltaTime,
		bool bIsRequestInFlight,
		bool bIsDialogueRequestQueued,
		bool& bOutShouldForceFailureCleanup);
	UE_API static EStateTreeRunStatus AdvanceDialogueStateForTest(
		FStateTreeTask_LLMQueryInstanceData& InstanceData,
		UAINpcComponent* NpcComponent,
		float DeltaTime);
#endif
};

#undef UE_API
