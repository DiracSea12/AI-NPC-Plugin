#include "Controllers/AINpcController.h"

#include "AINpcCoreLog.h"
#include "Components/AINpcComponent.h"
#include "Components/StateTreeAIComponent.h"
#include "StateTree.h"

AAINpcController::AAINpcController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	StateTreeAIComponent = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTreeAIComponent"));
	BrainComponent = StateTreeAIComponent;
}

void AAINpcController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	ApplyStateTreeBinding();
}

void AAINpcController::OnUnPossess()
{
	if (StateTreeAIComponent && StateTreeAIComponent->IsRunning())
	{
		StateTreeAIComponent->StopLogic(TEXT("NPC unpossessed."));
	}

	Super::OnUnPossess();
}

void AAINpcController::ConfigureFromComponent(const UAINpcComponent* InNpcComponent)
{
	CachedNpcComponent = InNpcComponent;
	ApplyStateTreeBinding();
}

void AAINpcController::SetDefaultStateTreeAsset(UStateTree* InStateTreeAsset)
{
	DefaultStateTreeAsset = InStateTreeAsset;
	ApplyStateTreeBinding();
}

UStateTreeAIComponent* AAINpcController::GetStateTreeAIComponent() const
{
	return StateTreeAIComponent;
}

bool AAINpcController::HasValidStateTreeBinding() const
{
	return IsStateTreeAssetReady(ResolveStateTreeAsset());
}

FString AAINpcController::GetStateTreeBindingFailureReason() const
{
	return LastStateTreeBindingFailureReason;
}

UStateTree* AAINpcController::GetResolvedStateTreeAsset() const
{
	return ResolveStateTreeAsset();
}

void AAINpcController::ApplyStateTreeBinding()
{
	if (!StateTreeAIComponent)
	{
		LastStateTreeBindingFailureReason = TEXT("StateTreeAIComponent is missing on AAINpcController.");
		return;
	}

	UStateTree* StateTreeAsset = ResolveStateTreeAsset();
	if (!StateTreeAsset)
	{
		LastStateTreeBindingFailureReason = BuildMissingStateTreeDiagnostic();
		if (!GIsAutomationTesting)
		{
			UE_LOG(LogAINpc, Warning, TEXT("%s"), *LastStateTreeBindingFailureReason);
		}
		return;
	}

	if (!IsStateTreeAssetReady(StateTreeAsset))
	{
		LastStateTreeBindingFailureReason = BuildNotReadyStateTreeDiagnostic(StateTreeAsset);
		if (!GIsAutomationTesting)
		{
			UE_LOG(LogAINpc, Warning, TEXT("%s"), *LastStateTreeBindingFailureReason);
		}
		return;
	}

	LastStateTreeBindingFailureReason.Reset();

	if (StateTreeAIComponent->IsRunning())
	{
		StateTreeAIComponent->StopLogic(TEXT("Rebinding StateTree asset."));
	}

	StateTreeAIComponent->SetStateTree(StateTreeAsset);

	if (GetPawn() && HasActorBegunPlay() && !StateTreeAIComponent->IsRunning())
	{
		StateTreeAIComponent->StartLogic();
	}
}

UStateTree* AAINpcController::ResolveStateTreeAsset() const
{
	if (DefaultStateTreeAsset)
	{
		return DefaultStateTreeAsset;
	}

	const UAINpcComponent* NpcComponent = CachedNpcComponent.Get();
	return NpcComponent ? NpcComponent->DefaultStateTreeAsset : nullptr;
}

bool AAINpcController::IsStateTreeAssetReady(const UStateTree* StateTreeAsset) const
{
	return StateTreeAsset && StateTreeAsset->IsReadyToRun();
}

FString AAINpcController::BuildNotReadyStateTreeDiagnostic(const UStateTree* StateTreeAsset) const
{
	return FString::Printf(
		TEXT("AAINpcController '%s' resolved StateTree asset '%s', but the asset is not ready to run. Compile/save a valid StateTree asset before claiming the Phase 1 StateTree dialogue path is configured."),
		*GetNameSafe(this),
		*GetNameSafe(StateTreeAsset));
}

FString AAINpcController::BuildMissingStateTreeDiagnostic() const
{
	const UAINpcComponent* NpcComponent = CachedNpcComponent.Get();
	const FString ControllerName = GetNameSafe(this);
	const FString PawnName = GetNameSafe(GetPawn());
	const FString ComponentName = GetNameSafe(NpcComponent);

	return FString::Printf(
		TEXT("AAINpcController '%s' could not bind a StateTree for pawn '%s'. No DefaultStateTreeAsset was set on the controller or bound UAINpcComponent '%s'. Assign a valid StateTree asset to UAINpcComponent.DefaultStateTreeAsset or call SetDefaultStateTreeAsset before claiming the Phase 1 StateTree dialogue path is configured."),
		*ControllerName,
		*PawnName,
		*ComponentName);
}
