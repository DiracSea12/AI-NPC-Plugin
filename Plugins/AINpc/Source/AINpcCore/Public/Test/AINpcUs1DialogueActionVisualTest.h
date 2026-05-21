#pragma once

#include "CoreMinimal.h"
#include "Components/AINpcComponent.h"
#include "Test/AINpcVisualTest.h"

class AAINpcTestCharacter;
class AAINpcTestSmartObjectActor;
class UAnimMontage;
class UNpcPersonaDataAsset;

class FAINpcUs1DialogueActionVisualTest final : public IAINpcVisualTest
{
public:
	FAINpcUs1DialogueActionVisualTest(AAINpcTestCharacter& InNpc, AAINpcTestSmartObjectActor& InSmartObject);
	~FAINpcUs1DialogueActionVisualTest();

	bool Start(FString& OutFailureReason) override;
	void Poll() override;
	bool IsComplete() const override;
	bool HasFailed() const override;
	const FString& GetFailureReason() const override;
	FString BuildSummary() const override;
	FAINpcVisualTestObservations BuildObservations() const override;

private:
	void StartInitialDialogue();
	void HandleTimeout();
	void MarkActionObservationHoldElapsed();
	void UpdateDialogueStateEvidence();
	bool HasRequiredEvidence() const;
	bool ConfigurePersona(FString& OutFailureReason);
	void Fail(const FString& Reason);
	bool LoadInitialPrompt(FString& OutPrompt, FString& OutFailureReason) const;
	bool LoadDelayFillerText(FString& OutText, FString& OutFailureReason) const;
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
	TObjectPtr<UNpcPersonaDataAsset> VisualHarnessPersona;
	FTimerHandle InitialDialogueTimerHandle;
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
	bool bActionExecutionAccepted = false;
	bool bActionTargetReached = false;
	bool bActionObservationHoldStarted = false;
	bool bActionObservationHoldElapsed = false;
	bool bDialogueSessionStartedObserved = false;
	bool bWaitingStateObserved = false;
	bool bSpeakingStateObserved = false;
	bool bDialogueResponseObserved = false;
	bool bDelayMaskingStartObserved = false;
	bool bDelayMaskingEndObserved = false;
	FString FailureReason;
	FString LastNpcResponseText;
	FString LastDelayFillerText;
	float ActionObservationHoldSeconds = 3.0f;
};
