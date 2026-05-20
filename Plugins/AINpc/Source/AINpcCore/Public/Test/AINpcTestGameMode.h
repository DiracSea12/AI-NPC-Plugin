#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "AINpcTestGameMode.generated.h"

UCLASS()
class AINPCCORE_API AAINpcTestGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AAINpcTestGameMode();

protected:
	virtual void BeginPlay() override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

private:
	UPROPERTY()
	TObjectPtr<class AAINpcTestCharacter> SpawnedNpc;

	UPROPERTY()
	TObjectPtr<class AAINpcTestSmartObjectActor> SpawnedSmartObject;

	FTimerHandle InitialDialogueTimerHandle;
	FTimerHandle VerificationTimeoutTimerHandle;
	FTimerHandle VisualVerificationTimerHandle;
	FTimerHandle ExitTimerHandle;
	bool bTerminalOutcomeRecorded = false;
	bool bActionExecutionObserved = false;

	void SpawnTestNpc();
	void ShowStatus(const FString& Message, const FColor& Color, float DurationSeconds) const;
	void StartInitialDialogue();
	void HandleVerificationTimeout();
	void PollVisualVerification();
	void RecordFailure(const FString& Reason);
	void RecordSuccess();
	void RequestHarnessExit();
	bool ValidateProviderConfiguration(const class UAINpcComponent& NpcComponent, FString& OutFailureReason) const;
	FString DescribeDialogueState(const class UAINpcComponent& NpcComponent) const;

	UFUNCTION()
	void OnNpcSessionStarted();

	UFUNCTION()
	void OnNpcResponse(const FString& Text);

	UFUNCTION()
	void OnNpcPartialResponse(const FString& Text);

	UFUNCTION()
	void OnNpcError(const FString& ErrorMessage);

	UFUNCTION()
	void OnNpcSessionEnded();

	UFUNCTION()
	void OnNpcDegraded(const FString& FallbackResponse, const FString& FailureReason);
};
