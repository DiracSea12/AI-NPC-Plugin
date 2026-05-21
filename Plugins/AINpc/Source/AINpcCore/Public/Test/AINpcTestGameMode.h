#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Test/AINpcVisualTest.h"
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

	FTimerHandle PollTimerHandle;
	FTimerHandle ExitTimerHandle;
	TUniquePtr<IAINpcVisualTest> ActiveTest;
	FString VisualTestId;
	FString VisualRunId;
	FString VisualResultPath;
	FDateTime VisualStartTimeUtc;
	const FAINpcVisualTestDescriptor* ActiveDescriptor = nullptr;
	bool bTerminalOutcomeRecorded = false;

	void StartHarness();
	bool SpawnFixture(EAINpcVisualTestFixtureKind FixtureKind, FString& OutFailureReason);
	bool SpawnNpc(FString& OutFailureReason);
	bool SpawnSmartObject(FString& OutFailureReason);
	bool PositionObserverCamera(FString& OutFailureReason);
	void StartSelectedTest();
	void PollActiveTest();
	void RecordFailure(const FString& Reason);
	void RecordSuccess(const FString& Summary);
	void WriteVisualResult(const FString& Status, const FString& ExitReason, const FString& FailureReason, const FString& DiagnosticSummary);
	FString ResolveVisualResultPath() const;
	void RequestHarnessExit();
	void ShowStatus(const FString& Message, const FColor& Color, float DurationSeconds) const;
};
