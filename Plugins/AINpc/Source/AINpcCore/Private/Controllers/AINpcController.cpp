#include "Controllers/AINpcController.h"

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

void AAINpcController::ApplyStateTreeBinding()
{
	if (!StateTreeAIComponent)
	{
		return;
	}

	UStateTree* StateTreeAsset = ResolveStateTreeAsset();
	if (!StateTreeAsset)
	{
		return;
	}

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
