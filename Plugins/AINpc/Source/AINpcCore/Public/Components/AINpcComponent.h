#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "LLM/LLMProviderTypes.h"
#include "TimerManager.h"
#include "AINpcComponent.generated.h"

enum class ENpcEventDispatchStage : uint8;
struct FNpcEventMessage;
class FOpenAIProvider;
class UStateTree;
class UAnimMontage;
class UNpcEventSubsystem;
class UNpcPersonaDataAsset;
class ULLMConcurrencyManager;

UENUM(BlueprintType)
enum class ENpcDialogueState : uint8
{
	Idle UMETA(DisplayName = "Idle"),
	WaitingForLLM UMETA(DisplayName = "Waiting For LLM"),
	Speaking UMETA(DisplayName = "Speaking"),
	Cooldown UMETA(DisplayName = "Cooldown")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FNpcDialogueSessionStartedDynamic);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNpcDialogueResponseDynamic, const FString&, ResponseText);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNpcDialoguePartialResponseDynamic, const FString&, PartialText);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNpcDialogueErrorDynamic, const FString&, ErrorMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FNpcDialogueSessionEndedDynamic);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNpcDelayMaskingStartDynamic, UAnimMontage*, Montage, const FText&, FillerText);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FNpcDelayMaskingEndDynamic);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNpcDialogueDegradedDynamic, const FString&, FallbackResponse, const FString&, FailureReason);

DECLARE_MULTICAST_DELEGATE(FNpcDialogueSessionStartedNative);
DECLARE_MULTICAST_DELEGATE_OneParam(FNpcDialogueResponseNative, const FString&);
DECLARE_MULTICAST_DELEGATE_OneParam(FNpcDialoguePartialResponseNative, const FString&);
DECLARE_MULTICAST_DELEGATE_OneParam(FNpcDialogueErrorNative, const FString&);
DECLARE_MULTICAST_DELEGATE(FNpcDialogueSessionEndedNative);
DECLARE_MULTICAST_DELEGATE_TwoParams(FNpcDelayMaskingStartNative, UAnimMontage*, const FText&);
DECLARE_MULTICAST_DELEGATE(FNpcDelayMaskingEndNative);
DECLARE_MULTICAST_DELEGATE_TwoParams(FNpcDialogueDegradedNative, const FString&, const FString&);

UCLASS(ClassGroup = (AI), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class AINPCCORE_API UAINpcComponent : public UActorComponent
{
	GENERATED_BODY()

	friend class FLLMConcurrencyManager;

public:
	UAINpcComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "AI NPC|Dialogue") bool StartDialogue(const FString& PlayerInput);
	UFUNCTION(BlueprintCallable, Category = "AI NPC|Dialogue") bool SendMessage(const FString& PlayerInput);
	UFUNCTION(BlueprintCallable, Category = "AI NPC|Dialogue") void EndDialogue();
	UFUNCTION(BlueprintCallable, Category = "AI NPC|Dialogue") FString GetNpcResponse() const;
	UFUNCTION(BlueprintCallable, Category = "AI NPC|Dialogue") void SetPersonaData(UNpcPersonaDataAsset* NewPersonaData);
	UFUNCTION(BlueprintCallable, Category = "AI NPC|Provider") void SetApiKey(const FString& NewApiKey);

	UFUNCTION(BlueprintPure, Category = "AI NPC|Dialogue")
	bool IsDialogueActive() const;

	UFUNCTION(BlueprintPure, Category = "AI NPC|Dialogue")
	bool IsRequestInFlight() const;

	UFUNCTION(BlueprintPure, Category = "AI NPC|Dialogue")
	bool TryGetLatestActionIntent(FNpcAction& OutActionIntent) const;

	UFUNCTION(BlueprintPure, Category = "AI NPC|SmartObject")
	TArray<FString> GetAvailableSmartObjectTargetsForExecution() const;

	bool IsDialogueRequestQueued() const;

	UFUNCTION(BlueprintPure, Category = "AI NPC|Dialogue")
	ENpcDialogueState GetDialogueState() const;

	UFUNCTION(BlueprintCallable, Category = "AI NPC|StateTree")
	void SetDialogueStateFromStateTree(ENpcDialogueState NewState);

	void HandleStateTreeTimeoutFailure();

	UFUNCTION(BlueprintPure, Category = "AI NPC|Dialogue")
	bool HasBeenInDialogueStateLongerThan(float DurationSeconds) const;

	UFUNCTION(BlueprintPure, Category = "AI NPC|Delay Masking")
	bool IsDelayMaskingActive() const;

	bool TryStartMemoryMaintenance();
	void EndMemoryMaintenance();
	bool IsMemoryMaintenanceActive() const;

	UFUNCTION()
	void HandleDialogueResponseDynamicForTest(const FString& ResponseText);

	UFUNCTION()
	void HandleDialogueErrorDynamicForTest(const FString& ErrorMessage);

	FNpcDialogueSessionStartedNative& OnDialogueSessionStartedNative();
	FNpcDialogueResponseNative& OnDialogueResponseNative();
	FNpcDialoguePartialResponseNative& OnDialoguePartialResponseNative();
	FNpcDialogueErrorNative& OnDialogueErrorNative();
	FNpcDialogueSessionEndedNative& OnDialogueSessionEndedNative();
	FNpcDelayMaskingStartNative& OnDelayMaskingStartNative();
	FNpcDelayMaskingEndNative& OnDelayMaskingEndNative();
	FNpcDialogueDegradedNative& OnDialogueDegradedNative();

#if WITH_EDITOR
	void SetDialogueTestState(bool bSessionActive, bool bRequestInFlight, const FGuid& RequestId, int32 InRetryAttemptCount, ENpcDialogueState InDialogueState);
	void HandleRequestCompletedForTest(const FLLMResponse& Response);
	void HandleStateTreeTimeoutFailureForTest();
	void SetLatestParsedActionsForTest(const TArray<FNpcAction>& InActions);
	void ClearLatestParsedActionsForTest();
	void HandleNpcEventStageDispatchedForTest(const FNpcEventMessage& EventMessage, ENpcEventDispatchStage DispatchStage);
	void HandleDelayMaskingThresholdReachedForTest();
	TArray<FString> GetAvailableSmartObjectTargetsForPromptForTest() const;
	static void SetSmartObjectTargetsForPromptForTest(const TArray<FString>& InTargets);
	static void ClearSmartObjectTargetsForPromptForTest();
	int32 GetRetryAttemptCountForTest() const;
	float GetRetryDelaySecondsForTest(int32 RetryAttemptIndex) const;
	bool HasQueuedDialogueRequestForTest() const;
	bool HasQueuedMemoryMaintenanceRequestForTest() const;
	bool IsMemoryMaintenanceActiveForTest() const;
	FLLMRequest BuildRequestForTest() const;
	FGuid GetActiveRequestIdForTest() const;
	void ResetDynamicDelegateCountersForTest();
	int32 GetDynamicDialogueResponseCountForTest() const;
	int32 GetDynamicDialogueErrorCountForTest() const;
	static void ResetConcurrencyStateForTest();
	static void SetDialogueDispatchBypassForTest(bool bBypassDispatch);
	static void SetActiveDialogueRequestSlotsForTest(int32 ActiveSlots);
	static int32 GetActiveDialogueRequestSlotsForTest();
	static int32 GetQueuedDialogueRequestCountForTest();
	static void PumpQueuedDialogueRequestsForTest();
	static void SetActiveMemoryMaintenanceSlotsForTest(int32 ActiveSlots);
	static int32 GetActiveMemoryMaintenanceSlotsForTest();
	static int32 GetQueuedMemoryMaintenanceRequestCountForTest();
	static void PumpQueuedMemoryMaintenanceRequestsForTest();
#endif

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI NPC")
	TObjectPtr<UNpcPersonaDataAsset> PersonaDataAsset = nullptr;

	// Override environment variable `AINPC_OPENAI_API_KEY` if set. Leave empty to use environment variable.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI NPC|Provider")
	FString ApiKeyOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI NPC|Provider")
	FString BaseUrlOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI NPC|Provider")
	FString ModelOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI NPC|StateTree")
	TObjectPtr<UStateTree> DefaultStateTreeAsset = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI NPC|StateTree")
	bool bAutoCreateNpcController = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI NPC|Events")
	// If incoming RoutingTags are empty, EventTag is treated as a single-tag routing fallback.
	FGameplayTagContainer EventSubscriptionTags;

	UPROPERTY(BlueprintAssignable, Category = "AI NPC|Dialogue") FNpcDialogueSessionStartedDynamic OnDialogueSessionStarted;
	UPROPERTY(BlueprintAssignable, Category = "AI NPC|Dialogue") FNpcDialogueResponseDynamic OnDialogueResponse;
	UPROPERTY(BlueprintAssignable, Category = "AI NPC|Dialogue") FNpcDialoguePartialResponseDynamic OnPartialResponse;
	UPROPERTY(BlueprintAssignable, Category = "AI NPC|Dialogue") FNpcDialogueErrorDynamic OnDialogueError;
	UPROPERTY(BlueprintAssignable, Category = "AI NPC|Dialogue") FNpcDialogueSessionEndedDynamic OnDialogueSessionEnded;
	UPROPERTY(BlueprintAssignable, Category = "AI NPC|Delay Masking") FNpcDelayMaskingStartDynamic OnDelayMaskingStart;
	UPROPERTY(BlueprintAssignable, Category = "AI NPC|Delay Masking") FNpcDelayMaskingEndDynamic OnDelayMaskingEnd;
	UPROPERTY(BlueprintAssignable, Category = "AI NPC|Dialogue") FNpcDialogueDegradedDynamic OnDialogueDegraded;

private:
	void EnsureProvider();
	FLLMRequest BuildRequest() const;
	FString BuildSystemPrompt() const;
	bool DispatchDialogueRequest();
	bool DispatchDialogueRequestNow();
	bool TryDispatchQueuedDialogueRequest(uint64 QueueToken);
	void CancelQueuedDialogueRequest();
	void ReleaseDialogueDispatchSlot();
	static void PumpQueuedDialogueRequests();
	static int32 GetDialogueRequestConcurrencyLimit();
	bool TryAcquireMemoryMaintenanceSlot();
	bool TryAcquireQueuedMemoryMaintenanceSlot(uint64 QueueToken);
	void CancelQueuedMemoryMaintenanceRequest();
	uint64 GetQueuedDialogueRequestToken() const { return QueuedDialogueRequestToken; }
	uint64 GetQueuedMemoryMaintenanceRequestToken() const { return QueuedMemoryMaintenanceRequestToken; }
	void ReleaseMemoryMaintenanceSlot();
	static void PumpQueuedMemoryMaintenanceRequests();
	static int32 GetMemoryMaintenanceConcurrencyLimit();
	TArray<FString> GetAvailableSmartObjectTargetsForPrompt() const;
	void HandleRequestCompleted(const FLLMResponse& Response);
	bool IsRetryableFailure(const FLLMResponse& Response) const;
	int32 GetMaxRetryAttempts() const;
	float GetRetryBackoffBaseSeconds() const;
	float GetRetryDelaySeconds(int32 RetryAttemptIndex) const;
	void ScheduleRetryRequest(float DelaySeconds);
	void ClearRetryTimer();
	void HandleRetryRequestTimerElapsed();
	bool TryHandleFailureWithFallback(const FLLMResponse& Response);
	FString ResolveFallbackResponseText() const;
	void BroadcastError(const FString& ErrorMessage);
	void ClearActiveRequest();
	void EnsureNpcControllerAndStateTreeBinding();
	void SetDialogueState(ENpcDialogueState NewState);
	void ScheduleDelayMasking();
	void ClearDelayMaskingTimer();
	void HandleDelayMaskingThresholdReached();
	void BroadcastDelayMaskingStart(UAnimMontage* Montage, const FText& FillerText);
	void StartDelayMasking();
	void EndDelayMasking();
	float GetDelayFillerThresholdSeconds() const;
	UAnimMontage* SelectRandomDelayMaskingMontage(const TArray<TSoftObjectPtr<UAnimMontage>>& MontageOptions) const;
	UAnimMontage* SelectDelayMaskingMontage() const;
	UAnimMontage* SelectEventDrivenDelayMaskingMontage(const FNpcEventMessage& EventMessage) const;
	bool IsEventRelevantForImmediateDelayMasking(const FNpcEventMessage& EventMessage) const;
	FText SelectDelayFillerText() const;
	void BindToNpcEventSubsystem();
	void UnbindFromNpcEventSubsystem();
	void HandleNpcEventStageDispatched(const FNpcEventMessage& EventMessage, ENpcEventDispatchStage DispatchStage);
	bool ShouldProcessNpcEvent(const FNpcEventMessage& EventMessage) const;
	void ProcessNpcEventDelayMasking(const FNpcEventMessage& EventMessage);
	void ProcessNpcEventEmotionAppraisal(const FNpcEventMessage& EventMessage);
	void ProcessNpcEventMemoryWrite(const FNpcEventMessage& EventMessage);
	void ProcessNpcEventPromptUpdate(const FNpcEventMessage& EventMessage);

private:
	TSharedPtr<FOpenAIProvider, ESPMode::ThreadSafe> Provider;
	TWeakObjectPtr<UNpcEventSubsystem> BoundEventSubsystem;
	FDelegateHandle EventStageDispatchedHandle;
	TArray<FLLMMessage> ConversationHistory;
	FParsedLLMResponse LastParsedResponse;
	FGuid ActiveRequestId;
	bool bIsDialogueSessionActive = false;
	bool bIsRequestInFlight = false;
	ENpcDialogueState CurrentDialogueState = ENpcDialogueState::Idle;
	double DialogueStateEnterTimeSeconds = 0.0;
	bool bDelayMaskingActive = false;
	FTimerHandle DelayMaskingTimerHandle;
	int32 RetryAttemptCount = 0; // ExponentialBackoff TimeoutFallback TemplateResponse
	float CumulativeRetryTimeSeconds = 0.0f;
	FTimerHandle RetryTimerHandle;
	bool bOwnsDialogueDispatchSlot = false;
	uint64 QueuedDialogueRequestToken = 0;
	bool bOwnsMemoryMaintenanceSlot = false;
	uint64 QueuedMemoryMaintenanceRequestToken = 0;

#if WITH_EDITOR
	int32 DynamicDialogueResponseCountForTest = 0;
	int32 DynamicDialogueErrorCountForTest = 0;
#endif

	FNpcDialogueSessionStartedNative DialogueSessionStartedNative;
	FNpcDialogueResponseNative DialogueResponseNative;
	FNpcDialoguePartialResponseNative DialoguePartialResponseNative;
	FNpcDialogueErrorNative DialogueErrorNative;
	FNpcDialogueSessionEndedNative DialogueSessionEndedNative;
	FNpcDelayMaskingStartNative DelayMaskingStartNative;
	FNpcDelayMaskingEndNative DelayMaskingEndNative;
	FNpcDialogueDegradedNative DialogueDegradedNative;
};
