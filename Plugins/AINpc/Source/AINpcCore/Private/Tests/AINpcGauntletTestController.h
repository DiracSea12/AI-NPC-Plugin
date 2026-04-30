#pragma once

#include "GauntletTestController.h"
#include "AINpcGauntletTestController.generated.h"

class UAINpcComponent;

UCLASS(MinimalAPI)
class UAINpcGauntletTestController : public UGauntletTestController
{
	GENERATED_BODY()

protected:
	virtual void OnInit() override;
	virtual void OnPostMapChange(UWorld* World) override;
	virtual void OnTick(float TimeDelta) override;

private:
	enum class ETestPhase : uint8
	{
		WaitingForWorld,
		Setup,
		DialogueInit,
		DialogueWait,
		EventTest,
		StateMachineTest,
		Cleanup,
		Done,
	};

	ETestPhase Phase = ETestPhase::WaitingForWorld;
	float PhaseElapsed = 0.0f;
	float TestTimeoutSeconds = 60.0f;
	float TotalTestTimeSeconds = 0.0f;

	int32 ChecksPassed = 0;
	int32 ChecksFailed = 0;

	UPROPERTY()
	TWeakObjectPtr<AActor> TestNpc;

	UPROPERTY()
	TWeakObjectPtr<UAINpcComponent> TestNpcComp;

	bool bDialogueResponseReceived = false;
	bool bDialogueErrorReceived = false;
	bool bSessionStarted = false;
	bool bSessionEnded = false;
	int32 PartialResponseCount = 0;

	bool SpawnTestNpc(UWorld* World);
	void ConfigureNpcComponent();
	void FailCheck(const FString& Reason);

	UFUNCTION()
	void OnSessionStarted();

	UFUNCTION()
	void OnDialogueResponse(const FString& Text);

	UFUNCTION()
	void OnPartialResponse(const FString& Text);

	UFUNCTION()
	void OnDialogueError(const FString& Msg);

	UFUNCTION()
	void OnSessionEnded();

	void EnterPhase(ETestPhase NewPhase);
	void LogSummary();

	void TickSetup(float DeltaTime);
	void TickDialogueInit(float DeltaTime);
	void TickDialogueWait(float DeltaTime);
	void TickEventTest(float DeltaTime);
	void TickStateMachineTest(float DeltaTime);
	void TickCleanup(float DeltaTime);
};
