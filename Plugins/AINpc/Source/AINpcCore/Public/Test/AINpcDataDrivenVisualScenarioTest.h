#pragma once

#include "CoreMinimal.h"
#include "Test/AINpcVisualTest.h"

class AAINpcTestCharacter;
class AAINpcTestSmartObjectActor;
class UNpcPersonaDataAsset;
struct FAINpcVisualScenarioRuntimeView;

class FAINpcDataDrivenVisualScenarioTest final : public IAINpcVisualTest
{
public:
	FAINpcDataDrivenVisualScenarioTest(const FAINpcVisualTestContext& Context, FAINpcVisualScenarioConfig InConfig);
	~FAINpcDataDrivenVisualScenarioTest();

	bool Start(FString& OutFailureReason) override;
	void Poll() override;
	bool IsComplete() const override;
	bool HasFailed() const override;
	const FString& GetFailureReason() const override;
	FString BuildSummary() const override;
	FAINpcVisualTestObservations BuildObservations() const override;
	TArray<FAINpcVisualTestStepDiagnostic> BuildStepDiagnostics() const override;

private:
	bool StartDialogueObservation(FString& OutFailureReason);
	void StartNextStep();
	void PollActiveStep();
	bool RunStep(const FAINpcVisualScenarioStep& Step, FString& OutFailureReason);
	bool RunDialogueStart(const FAINpcVisualScenarioStep& Step, FString& OutFailureReason);
	bool RunWorldEvent(const FAINpcVisualScenarioStep& Step, FString& OutFailureReason);
	bool RunActionExecuteLatestIntent(const FAINpcVisualScenarioStep& Step, FString& OutFailureReason);
	bool RunObserveHold(const FAINpcVisualScenarioStep& Step, FString& OutFailureReason);
	bool IsAssertionSatisfied(const FAINpcVisualScenarioAssertion& Assertion) const;
	bool TryGetObservationBool(const FString& Name, bool& OutValue) const;
	bool TryGetObservationString(const FString& Name, FString& OutValue) const;
	bool HasObservation(const FString& Name) const;
	FString BuildPrompt(FString& OutFailureReason) const;
	void HandleTimeout();
	void UpdateDialogueStateEvidence();
	bool ConfigurePersona(FString& OutFailureReason);
	void Fail(const FString& Reason);
	void ShowStatus(const FString& Message, const FColor& Color, float DurationSeconds) const;
	void RecordBoolObservation(const FString& Name, bool bValue);
	void RefreshActionTargetObservation();
	void CompleteCurrentStep();
	void FinalizeActiveStepDiagnostic(const FString& Status, const FString& Reason);

private:
	TUniquePtr<FAINpcVisualScenarioRuntimeView> Runtime;
	FAINpcVisualScenarioConfig Config;
	TObjectPtr<UNpcPersonaDataAsset> VisualHarnessPersona;
	FTimerHandle StepTimerHandle;
	FTimerHandle TimeoutTimerHandle;
	TMap<FString, bool> BoolObservations;
	TMap<FString, int32> IntegerObservations;
	TMap<FString, double> NumberObservations;
	TMap<FString, FString> StringObservations;
	TArray<FAINpcVisualTestStepDiagnostic> StepDiagnostics;
	int32 ActiveStepIndex = INDEX_NONE;
	double ActiveStepStartSeconds = 0.0;
	bool bComplete = false;
	bool bFailed = false;
	bool bStarted = false;
	FString FailureReason;
	FString LastActionFailureReason;
};
