#include "StateTree/Tasks/StateTreeTask_LLMQuery.h"

#include "Components/AINpcComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Settings/AINpcSettings.h"
#include "StateTreeExecutionContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StateTreeTask_LLMQuery)

namespace
{
UAINpcComponent* ResolveNpcComponent(const FStateTreeExecutionContext& Context, FStateTreeTask_LLMQueryInstanceData& InstanceData)
{
	if (InstanceData.NpcComponent)
	{
		return InstanceData.NpcComponent.Get();
	}

	UObject* OwnerObject = Context.GetOwner();
	if (AActor* OwnerActor = Cast<AActor>(OwnerObject))
	{
		if (UAINpcComponent* FoundComponent = OwnerActor->FindComponentByClass<UAINpcComponent>())
		{
			InstanceData.NpcComponent = FoundComponent;
			return FoundComponent;
		}
	}

	if (AController* Controller = Cast<AController>(OwnerObject))
	{
		if (APawn* ControlledPawn = Controller->GetPawn())
		{
			if (UAINpcComponent* FoundComponent = ControlledPawn->FindComponentByClass<UAINpcComponent>())
			{
				InstanceData.NpcComponent = FoundComponent;
				return FoundComponent;
			}
		}
	}

	return nullptr;
}

float ComputeReliabilityTimeoutBudgetSeconds()
{
	const UAINpcSettings* Settings = GetDefault<UAINpcSettings>();
	if (!Settings)
	{
		return 0.0f;
	}

	const int32 MaxRetryAttempts = FMath::Max(0, Settings->MaxRequestRetries);
	const float RequestTimeoutSeconds = FMath::Max(0.0f, Settings->RequestTimeoutSeconds);
	const float RetryBackoffBaseSeconds = FMath::Max(0.0f, Settings->RetryBackoffBaseSeconds);

	float TotalRetryBackoffSeconds = 0.0f;
	for (int32 RetryIndex = 0; RetryIndex < MaxRetryAttempts; ++RetryIndex)
	{
		TotalRetryBackoffSeconds += RetryBackoffBaseSeconds * FMath::Pow(2.0f, static_cast<float>(RetryIndex));
	}

	constexpr float CallbackSchedulingGraceSeconds = 1.0f;
	return (RequestTimeoutSeconds * static_cast<float>(MaxRetryAttempts + 1)) + TotalRetryBackoffSeconds + CallbackSchedulingGraceSeconds;
}

void CleanupDialogueOnExit(UAINpcComponent* NpcComponent)
{
	if (!NpcComponent)
	{
		return;
	}

	const ENpcDialogueState DialogueState = NpcComponent->GetDialogueState();
	if (NpcComponent->IsRequestInFlight() || DialogueState == ENpcDialogueState::WaitingForLLM)
	{
		NpcComponent->EndDialogue();
	}
}

EStateTreeRunStatus TickWaitingForLlmState(
	FStateTreeTask_LLMQueryInstanceData& InstanceData,
	const float DeltaTime,
	const bool bIsRequestInFlight,
	const bool bIsDialogueRequestQueued,
	bool& bOutShouldForceFailureCleanup)
{
	bOutShouldForceFailureCleanup = false;

	if (!bIsRequestInFlight && bIsDialogueRequestQueued)
	{
		// Queued requests are not in-flight yet; only start timeout tracking after dispatch begins.
		InstanceData.ElapsedSeconds = 0.0f;
		InstanceData.bTimeoutExceeded = false;
		InstanceData.TimeoutExceededAtSeconds = 0.0f;
		return EStateTreeRunStatus::Running;
	}

	InstanceData.ElapsedSeconds += DeltaTime;

	const float TimeoutSeconds = FMath::Max(0.1f, InstanceData.TimeoutSeconds);
	if (InstanceData.ElapsedSeconds >= TimeoutSeconds)
	{
		if (!InstanceData.bTimeoutExceeded)
		{
			// Soft-timeout first: allow a slightly-late terminal callback to resolve fallback/degraded flow.
			InstanceData.bTimeoutExceeded = true;
			InstanceData.TimeoutExceededAtSeconds = InstanceData.ElapsedSeconds;
			return EStateTreeRunStatus::Running;
		}

		const float TimeoutOverrunSeconds = InstanceData.ElapsedSeconds - InstanceData.TimeoutExceededAtSeconds;
		if (TimeoutOverrunSeconds >= InstanceData.TimeoutGraceSeconds)
		{
			bOutShouldForceFailureCleanup = true;
			return EStateTreeRunStatus::Failed;
		}
	}

	return EStateTreeRunStatus::Running;
}
}

FStateTreeTask_LLMQuery::FStateTreeTask_LLMQuery()
{
	bShouldStateChangeOnReselect = false;
}

EStateTreeRunStatus FStateTreeTask_LLMQuery::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	InstanceData.TimeoutSeconds = FMath::Max(InstanceData.TimeoutSeconds, ComputeReliabilityTimeoutBudgetSeconds());
	InstanceData.TimeoutGraceSeconds = FMath::Max(0.0f, InstanceData.TimeoutGraceSeconds);
	InstanceData.ElapsedSeconds = 0.0f;
	InstanceData.SpeakingElapsedSeconds = 0.0f;
	InstanceData.CooldownElapsedSeconds = 0.0f;
	InstanceData.TimeoutExceededAtSeconds = 0.0f;
	InstanceData.bTimeoutExceeded = false;
	InstanceData.bStartedRequest = false;

	UAINpcComponent* NpcComponent = ResolveNpcComponent(Context, InstanceData);
	if (!NpcComponent)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (!NpcComponent->StartDialogue(InstanceData.PlayerInput))
	{
		if (!NpcComponent->IsRequestInFlight())
		{
			NpcComponent->SetDialogueStateFromStateTree(ENpcDialogueState::Idle);
		}
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.bStartedRequest = true;
	NpcComponent->SetDialogueStateFromStateTree(ENpcDialogueState::WaitingForLLM);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeTask_LLMQuery::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	UAINpcComponent* NpcComponent = ResolveNpcComponent(Context, InstanceData);
	if (!NpcComponent || !InstanceData.bStartedRequest)
	{
		return EStateTreeRunStatus::Failed;
	}

	ENpcDialogueState DialogueState = NpcComponent->GetDialogueState();
	if (DialogueState == ENpcDialogueState::WaitingForLLM)
	{
		bool bShouldForceFailureCleanup = false;
		const EStateTreeRunStatus WaitingStatus = TickWaitingForLlmState(
			InstanceData,
			DeltaTime,
			NpcComponent->IsRequestInFlight(),
			NpcComponent->IsDialogueRequestQueued(),
			bShouldForceFailureCleanup);
		if (bShouldForceFailureCleanup)
		{
			NpcComponent->HandleStateTreeTimeoutFailure();
		}
		return WaitingStatus;
	}

	InstanceData.ElapsedSeconds = 0.0f;
	InstanceData.bTimeoutExceeded = false;
	InstanceData.TimeoutExceededAtSeconds = 0.0f;

	if (DialogueState == ENpcDialogueState::Speaking)
	{
		InstanceData.SpeakingElapsedSeconds += DeltaTime;
		if (InstanceData.SpeakingElapsedSeconds < FMath::Max(0.0f, InstanceData.SpeakingDurationSeconds))
		{
			return EStateTreeRunStatus::Running;
		}

		NpcComponent->SetDialogueStateFromStateTree(ENpcDialogueState::Cooldown);
		DialogueState = ENpcDialogueState::Cooldown;
	}

	if (DialogueState == ENpcDialogueState::Cooldown)
	{
		InstanceData.CooldownElapsedSeconds += DeltaTime;
		if (InstanceData.CooldownElapsedSeconds < FMath::Max(0.0f, InstanceData.CooldownDurationSeconds))
		{
			return EStateTreeRunStatus::Running;
		}

		NpcComponent->SetDialogueStateFromStateTree(ENpcDialogueState::Idle);
		return EStateTreeRunStatus::Succeeded;
	}

	return (DialogueState == ENpcDialogueState::Idle) ? EStateTreeRunStatus::Failed : EStateTreeRunStatus::Running;
}

void FStateTreeTask_LLMQuery::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	CleanupDialogueOnExit(ResolveNpcComponent(Context, InstanceData));
}

#if WITH_EDITOR
void FStateTreeTask_LLMQuery::ApplyExitStateCleanupForTest(UAINpcComponent* NpcComponent)
{
	CleanupDialogueOnExit(NpcComponent);
}

EStateTreeRunStatus FStateTreeTask_LLMQuery::EvaluateWaitingStateForTest(
	FStateTreeTask_LLMQueryInstanceData& InstanceData,
	const float DeltaTime,
	const bool bIsRequestInFlight,
	const bool bIsDialogueRequestQueued,
	bool& bOutShouldForceFailureCleanup)
{
	return TickWaitingForLlmState(
		InstanceData,
		DeltaTime,
		bIsRequestInFlight,
		bIsDialogueRequestQueued,
		bOutShouldForceFailureCleanup);
}
#endif
