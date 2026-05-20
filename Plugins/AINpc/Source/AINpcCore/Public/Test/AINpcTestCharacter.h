#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AINpcTestCharacter.generated.h"

/**
 * Test Character with UAINpcComponent pre-attached.
 * Used for visual verification of AI-NPC plugin behavior.
 */
UCLASS()
class AINPCCORE_API AAINpcTestCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AAINpcTestCharacter();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

public:
	void BeginVisualActionMove(const FTransform& ClaimedSlotTransform, const FString& InTargetId);
	bool HasReachedVisualActionTarget() const;
	bool IsVisualActionMoveActive() const;
	float GetVisualActionTargetDistance() const;
	const FString& GetVisualActionTargetId() const;

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<class UAINpcComponent> NpcComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<class UStaticMeshComponent> PlaceholderMesh;

private:
	void UpdateDebugMaterialColor(const FLinearColor& NewColor);

	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> PlaceholderMaterial;

	bool bVisualActionMoveActive = false;
	bool bVisualActionTargetReached = false;
	FVector VisualActionTargetLocation = FVector::ZeroVector;
	FString VisualActionTargetId;
	float VisualActionMoveSpeed = 220.0f;
	float VisualActionAcceptanceDistance = 40.0f;
};
