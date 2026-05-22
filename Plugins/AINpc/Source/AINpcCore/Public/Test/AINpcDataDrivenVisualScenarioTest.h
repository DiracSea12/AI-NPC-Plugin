#pragma once

#include "CoreMinimal.h"
#include "Test/AINpcVisualTest.h"

class AAINpcTestCharacter;
class AAINpcTestSmartObjectActor;
class UNpcPersonaDataAsset;

class FAINpcDataDrivenVisualScenarioTest final : public IAINpcVisualTest
{
public:
	FAINpcDataDrivenVisualScenarioTest(AAINpcTestCharacter& InNpc, AAINpcTestSmartObjectActor& InSmartObject, FAINpcVisualScenarioConfig InConfig);
	~FAINpcDataDrivenVisualScenarioTest();

	bool Start(FString& OutFailureReason) override;
	void Poll() override;
	bool IsComplete() const override;
	bool HasFailed() const override;
	const FString& GetFailureReason() const override;
	FString BuildSummary() const override;
	FAINpcVisualTestObservations BuildObservations() const override;

private:
	void StartDialogue();
	void HandleTimeout();
	void MarkActionObservationHoldElapsed();
	void UpdateDialogueStateEvidence();
	bool HasRequiredEvidence() const;
	bool ConfigurePersona(FString& OutFailureReason);
	void BroadcastEventTrigger();
	void Fail(const FString& Reason);
	void ShowStatus(const FString& Message, const FColor& Color, float DurationSeconds) const;

	void OnNpcSessionStarted();
	void OnNpcResponse(const FString& Text);
	void OnNpcPartialResponse(const FString& Text);
	void OnNpcError(const FString& ErrorMessage);
	void OnNpcSessionEnded();
	void OnNpcDegraded(const FString& FallbackResponse, const FString& FailureReason);
	void OnNpcDelayMaskingStart(UAnimMontage* Montage, const FText& FillerText);
	void OnNpcDelayMaskingEnd();

private:
	AAINpcTestCharacter& Npc;
	AAINpcTestSmartObjectActor& SmartObject;
	FAINpcVisualScenarioConfig Config;
	TObjectPtr<UNpcPersonaDataAsset> VisualHarnessPersona;
	FTimerHandle DialogueTimerHandle;
	FTimerHandle TimeoutTimerHandle;
	FTimerHandle ActionObservationHoldTimerHandle;
	FDelegateHandle SessionStartedHandle;
	FDelegateHandle ResponseHandle;
	FDelegateHandle PartialResponseHandle;
	FDelegateHandle ErrorHandle;
	FDelegateHandle SessionEndedHandle;
	FDelegateHandle DegradedHandle;
	FDelegateHandle DelayMaskingStartHandle;
	FDelegateHandle DelayMaskingEndHandle;
	bool bComplete = false;
	bool bFailed = false;
	bool bStarted = false;
	bool bDialogueSessionStartedObserved = false;
	bool bWaitingStateObserved = false;
	bool bSpeakingStateObserved = false;
	bool bDialogueResponseObserved = false;
	bool bPartialResponseObserved = false;
	bool bStructuredResponseObserved = false;
	bool bActionIntentObserved = false;
	bool bEventTriggerBroadcastObserved = false;
	bool bEventDelayMaskingStartObserved = false;
	bool bDelayMaskingStartObserved = false;
	bool bDelayMaskingEndObserved = false;
	bool bActionExecutionAccepted = false;
	bool bActionRejectedVisible = false;
	bool bActionTargetReached = false;
	bool bActionObservationHoldStarted = false;
	bool bActionObservationHoldElapsed = false;
	float ActionObservationHoldSeconds = 3.0f;
	FString FailureReason;
	FString LastNpcResponseText;
	FString LastPartialResponseText;
	FString LastDelayFillerText;
	FString LastActionFailureReason;
};
