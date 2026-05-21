#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "AINpcController.generated.h"

class UAINpcComponent;
class UStateTree;
class UStateTreeAIComponent;

UCLASS(BlueprintType, Blueprintable)
class AINPCCORE_API AAINpcController : public AAIController
{
	GENERATED_BODY()

public:
	AAINpcController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

	UFUNCTION(BlueprintCallable, Category = "AI NPC|StateTree")
	void ConfigureFromComponent(const UAINpcComponent* InNpcComponent);

	UFUNCTION(BlueprintCallable, Category = "AI NPC|StateTree")
	void SetDefaultStateTreeAsset(UStateTree* InStateTreeAsset);

	UFUNCTION(BlueprintPure, Category = "AI NPC|StateTree")
	UStateTreeAIComponent* GetStateTreeAIComponent() const;

	UFUNCTION(BlueprintPure, Category = "AI NPC|Diagnostics")
	bool HasValidStateTreeBinding() const;

	UFUNCTION(BlueprintPure, Category = "AI NPC|Diagnostics")
	FString GetStateTreeBindingFailureReason() const;

	UFUNCTION(BlueprintPure, Category = "AI NPC|Diagnostics")
	UStateTree* GetResolvedStateTreeAsset() const;

private:
	void ApplyStateTreeBinding();
	UStateTree* ResolveStateTreeAsset() const;
	bool IsStateTreeAssetReady(const UStateTree* StateTreeAsset) const;
	FString BuildNotReadyStateTreeDiagnostic(const UStateTree* StateTreeAsset) const;
	FString BuildMissingStateTreeDiagnostic() const;

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI NPC|StateTree", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStateTreeAIComponent> StateTreeAIComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI NPC|StateTree", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStateTree> DefaultStateTreeAsset = nullptr;

	TWeakObjectPtr<const UAINpcComponent> CachedNpcComponent;
	FString LastStateTreeBindingFailureReason;
};
