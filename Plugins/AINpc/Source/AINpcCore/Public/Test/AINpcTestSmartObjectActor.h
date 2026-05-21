#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "SmartObjectDefinition.h"
#include "AINpcTestSmartObjectActor.generated.h"

class UMaterialInstanceDynamic;
class USceneComponent;
class UStaticMeshComponent;

UCLASS()
class AINPCCORE_API UAINpcTestSmartObjectBehaviorDefinition : public USmartObjectBehaviorDefinition
{
	GENERATED_BODY()
};

UCLASS()
class AINPCCORE_API AAINpcTestSmartObjectActor : public AActor
{
	GENERATED_BODY()

public:
	AAINpcTestSmartObjectActor();

	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	FVector GetSlotWorldLocation() const;
	void SetInteractionState(bool bInInteractionActive);

private:
	void InitializeRuntimeDefinition();
	void UpdateMaterialColors();

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> RootSceneComponent;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SmartObjectComponent;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> ObjectMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> SlotMarkerMesh;

	UPROPERTY(Transient)
	TObjectPtr<UObject> RuntimeDefinition;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ObjectMeshMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SlotMarkerMaterial;

	UPROPERTY(EditAnywhere, Category = "AI NPC|Test")
	FVector SlotOffset = FVector(0.0f, 220.0f, 0.0f);

	UPROPERTY(EditAnywhere, Category = "AI NPC|Test")
	FColor IdleColor = FColor(64, 160, 255);

	UPROPERTY(EditAnywhere, Category = "AI NPC|Test")
	FColor ActiveColor = FColor(255, 180, 0);

	bool bInteractionActive = false;
};
