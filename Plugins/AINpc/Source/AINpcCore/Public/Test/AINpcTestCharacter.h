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
	void SetVisibleDialogueText(const FString& InText);
	void SetVisibleDelayMaskingText(const FString& InText);
	void SetVisibleStateText(const FString& InText);
	bool HasReachedVisualActionTarget() const;
	bool IsVisualActionMoveActive() const;
	bool HasValidVisualMeshAndAnimation() const;
	float GetVisualActionTargetDistance() const;
	const FString& GetVisualActionTargetId() const;

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<class UAINpcComponent> NpcComponent;

private:
	bool bVisualActionMoveActive = false;
	bool bVisualActionTargetReached = false;
	bool bVisualMeshLoaded = false;
	bool bVisualAnimLoaded = false;
	FVector VisualActionTargetLocation = FVector::ZeroVector;
	FString VisualActionTargetId;
	FString VisibleDialogueText;
	FString VisibleDelayMaskingText;
	FString VisibleStateText;
	float VisualActionMoveSpeed = 220.0f;
	float VisualActionAcceptanceDistance = 40.0f;
};
