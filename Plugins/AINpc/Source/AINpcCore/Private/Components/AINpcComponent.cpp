// RetryAttempt TimeoutFallback
#include "Components/AINpcComponent.h"
#include "Components/AINpcComponentStateHandler.h"
#include "Components/AINpcDelayMaskingHandler.h"
#include "Components/AINpcDialogueFallbackHandler.h"
#include "Components/AINpcDialogueLifecycleHandler.h"
#include "Components/AINpcDialogueRequestBuilder.h"
#include "Components/AINpcEventRoutingHandler.h"
#include "Components/AINpcMemoryMaintenanceHandler.h"
#include "AINpcProviderConfigResolver.h"
#include "Components/AINpcSmartObjectPromptHandler.h"
#include "Data/NpcPersonaDataAsset.h"
#include "Events/NpcEventSubsystem.h"
#include "LLM/ILLMProvider.h"
#include "LLM/LLMConcurrencyManager.h"
#include "LLM/LLMReliabilityManager.h"
#include "Settings/AINpcSettings.h"

UAINpcComponent::UAINpcComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	DialogueStateEnterTimeSeconds = FPlatformTime::Seconds();
}

void UAINpcComponent::BeginPlay()
{
	Super::BeginPlay();
	EnsureNpcControllerAndStateTreeBinding();
	BindToNpcEventSubsystem();
}

void UAINpcComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindFromNpcEventSubsystem();
	EndDialogue();
	EndMemoryMaintenance();
	Super::EndPlay(EndPlayReason);
}

bool UAINpcComponent::StartDialogue(const FString& PlayerInput)
{
	return FAINpcDialogueLifecycleHandler::StartDialogue(*this, PlayerInput);
}

void UAINpcComponent::EndDialogue()
{
	FAINpcDialogueLifecycleHandler::EndDialogue(*this);
}

bool UAINpcComponent::SendMessage(const FString& PlayerInput)
{
	return StartDialogue(PlayerInput);
}

FString UAINpcComponent::GetNpcResponse() const
{
	return LastParsedResponse.Dialogue;
}

void UAINpcComponent::SetPersonaData(UNpcPersonaDataAsset* NewPersonaData)
{
	PersonaDataAsset = NewPersonaData;
}

bool UAINpcComponent::IsDialogueActive() const
{
	return bIsDialogueSessionActive;
}

bool UAINpcComponent::IsRequestInFlight() const
{
	return bIsRequestInFlight;
}

bool UAINpcComponent::TryGetLatestActionIntent(FNpcAction& OutActionIntent) const
{
	return FAINpcComponentStateHandler::TryGetLatestActionIntent(*this, OutActionIntent);
}

TArray<FString> UAINpcComponent::GetAvailableSmartObjectTargetsForExecution() const
{
	return GetAvailableSmartObjectTargetsForPrompt();
}

bool UAINpcComponent::IsDialogueRequestQueued() const
{
	return QueuedDialogueRequestToken != 0;
}

ENpcDialogueState UAINpcComponent::GetDialogueState() const
{
	return CurrentDialogueState;
}

bool UAINpcComponent::SupportsStateTreeAutoController() const
{
	return FAINpcComponentStateHandler::SupportsStateTreeAutoController(*this);
}

void UAINpcComponent::SetDialogueStateFromStateTree(const ENpcDialogueState NewState)
{
	SetDialogueState(NewState);
}

void UAINpcComponent::HandleStateTreeTimeoutFailure()
{
	FAINpcDialogueLifecycleHandler::HandleStateTreeTimeoutFailure(*this);
}

bool UAINpcComponent::HasBeenInDialogueStateLongerThan(const float DurationSeconds) const
{
	return FAINpcComponentStateHandler::HasBeenInDialogueStateLongerThan(*this, DurationSeconds);
}

bool UAINpcComponent::IsDelayMaskingActive() const
{
	return bDelayMaskingActive;
}

bool UAINpcComponent::TryStartMemoryMaintenance()
{
	return FAINpcMemoryMaintenanceHandler::TryStart(*this);
}

void UAINpcComponent::EndMemoryMaintenance()
{
	FAINpcMemoryMaintenanceHandler::End(*this);
}

bool UAINpcComponent::IsMemoryMaintenanceActive() const
{
	return FAINpcMemoryMaintenanceHandler::IsActive(*this);
}

FNpcDialogueSessionStartedNative& UAINpcComponent::OnDialogueSessionStartedNative()
{
	return DialogueSessionStartedNative;
}

FNpcDialogueResponseNative& UAINpcComponent::OnDialogueResponseNative()
{
	return DialogueResponseNative;
}

FNpcDialoguePartialResponseNative& UAINpcComponent::OnDialoguePartialResponseNative()
{
	return DialoguePartialResponseNative;
}

FNpcDialogueErrorNative& UAINpcComponent::OnDialogueErrorNative()
{
	return DialogueErrorNative;
}

FNpcDialogueSessionEndedNative& UAINpcComponent::OnDialogueSessionEndedNative()
{
	return DialogueSessionEndedNative;
}

FNpcDelayMaskingStartNative& UAINpcComponent::OnDelayMaskingStartNative()
{
	return DelayMaskingStartNative;
}

FNpcDelayMaskingEndNative& UAINpcComponent::OnDelayMaskingEndNative()
{
	return DelayMaskingEndNative;
}

FNpcDialogueDegradedNative& UAINpcComponent::OnDialogueDegradedNative()
{
	return DialogueDegradedNative;
}

void UAINpcComponent::HandleDialogueResponseDynamicForTest(const FString& ResponseText)
{
	(void)ResponseText;
#if defined(WITH_DEV_AUTOMATION_TESTS) && WITH_DEV_AUTOMATION_TESTS
	++DynamicDialogueResponseCountForTest;
#endif
}

void UAINpcComponent::HandleDialogueErrorDynamicForTest(const FString& ErrorMessage)
{
	(void)ErrorMessage;
#if defined(WITH_DEV_AUTOMATION_TESTS) && WITH_DEV_AUTOMATION_TESTS
	++DynamicDialogueErrorCountForTest;
#endif
}

#if defined(WITH_DEV_AUTOMATION_TESTS) && WITH_DEV_AUTOMATION_TESTS
void UAINpcComponent::SetDialogueTestState(
	const bool bSessionActive,
	const bool bRequestInFlightState,
	const FGuid& RequestId,
	const int32 InRetryAttemptCount,
	const ENpcDialogueState InDialogueState)
{
	bIsDialogueSessionActive = bSessionActive;
	bIsRequestInFlight = bRequestInFlightState;
	ActiveRequestId = RequestId;
	RetryAttemptCount = InRetryAttemptCount;
	CurrentDialogueState = InDialogueState;
	DialogueStateEnterTimeSeconds = FPlatformTime::Seconds();
}

void UAINpcComponent::SeedConversationHistoryForTest(const TArray<FLLMMessage>& Messages)
{
	ConversationHistory = Messages;
}

void UAINpcComponent::HandleRequestCompletedForTest(const FLLMResponse& Response)
{
	FAINpcDialogueLifecycleHandler::HandleRequestCompleted(*this, Response);
}

void UAINpcComponent::HandleStateTreeTimeoutFailureForTest()
{
	HandleStateTreeTimeoutFailure();
}

void UAINpcComponent::SetLatestParsedActionsForTest(const TArray<FNpcAction>& InActions)
{
	LastParsedResponse = FParsedLLMResponse();
	LastParsedResponse.Actions = InActions;
}

void UAINpcComponent::ClearLatestParsedActionsForTest()
{
	LastParsedResponse = FParsedLLMResponse();
}

void UAINpcComponent::HandleNpcEventStageDispatchedForTest(const FNpcEventMessage& EventMessage, const ENpcEventDispatchStage DispatchStage)
{
	HandleNpcEventStageDispatched(EventMessage, DispatchStage);
}

void UAINpcComponent::HandleDelayMaskingThresholdReachedForTest()
{
	HandleDelayMaskingThresholdReached();
}

TArray<FString> UAINpcComponent::GetAvailableSmartObjectTargetsForPromptForTest() const
{
	return GetAvailableSmartObjectTargetsForPrompt();
}

void UAINpcComponent::SetSmartObjectTargetsForPromptForTest(const TArray<FString>& InTargets)
{
	FAINpcSmartObjectPromptHandler::SetTargetsOverrideForTest(InTargets);
}

void UAINpcComponent::ClearSmartObjectTargetsForPromptForTest()
{
	FAINpcSmartObjectPromptHandler::ClearTargetsOverrideForTest();
}

int32 UAINpcComponent::GetRetryAttemptCountForTest() const
{
	return RetryAttemptCount;
}

float UAINpcComponent::GetRetryDelaySecondsForTest(const int32 RetryAttemptIndex) const
{
	return FLLMReliabilityManager::GetRetryDelaySeconds(GetDefault<UAINpcSettings>(), RetryAttemptIndex);
}

bool UAINpcComponent::HasQueuedDialogueRequestForTest() const
{
	return QueuedDialogueRequestToken != 0;
}

bool UAINpcComponent::HasQueuedMemoryMaintenanceRequestForTest() const
{
	return QueuedMemoryMaintenanceRequestToken != 0;
}

bool UAINpcComponent::IsMemoryMaintenanceActiveForTest() const
{
	return bOwnsMemoryMaintenanceSlot;
}

FLLMRequest UAINpcComponent::BuildRequestForTest() const
{
	return BuildRequest();
}

FLLMRequest UAINpcComponent::BuildStreamingRequestForTest()
{
	FLLMRequest Request = BuildRequest();
	FAINpcDialogueRequestBuilder::ConfigureStreamingRequest(*this, Request);
	return Request;
}

FGuid UAINpcComponent::GetActiveRequestIdForTest() const
{
	return ActiveRequestId;
}

void UAINpcComponent::ResetDynamicDelegateCountersForTest()
{
	DynamicDialogueResponseCountForTest = 0;
	DynamicDialogueErrorCountForTest = 0;
}

int32 UAINpcComponent::GetDynamicDialogueResponseCountForTest() const
{
	return DynamicDialogueResponseCountForTest;
}

int32 UAINpcComponent::GetDynamicDialogueErrorCountForTest() const
{
	return DynamicDialogueErrorCountForTest;
}

void UAINpcComponent::ResetConcurrencyStateForTest()
{
	FLLMConcurrencyManager::Get().ResetForTest();
	FAINpcDialogueLifecycleHandler::SetDispatchBypassForTest(false);
	FAINpcSmartObjectPromptHandler::ClearTargetsOverrideForTest();
}

void UAINpcComponent::SetDialogueDispatchBypassForTest(const bool bBypassDispatch)
{
	FAINpcDialogueLifecycleHandler::SetDispatchBypassForTest(bBypassDispatch);
}

void UAINpcComponent::SetActiveDialogueRequestSlotsForTest(const int32 ActiveSlots)
{
	FLLMConcurrencyManager::Get().SetActiveDialogueSlotsForTest(ActiveSlots);
}

int32 UAINpcComponent::GetActiveDialogueRequestSlotsForTest()
{
	return FLLMConcurrencyManager::Get().GetActiveDialogueSlots();
}

int32 UAINpcComponent::GetQueuedDialogueRequestCountForTest()
{
	return FLLMConcurrencyManager::Get().GetQueuedDialogueCount();
}

void UAINpcComponent::PumpQueuedDialogueRequestsForTest()
{
	FAINpcDialogueLifecycleHandler::PumpQueuedDialogueRequests();
}

void UAINpcComponent::SetActiveMemoryMaintenanceSlotsForTest(const int32 ActiveSlots)
{
	FLLMConcurrencyManager::Get().SetActiveMemorySlotsForTest(ActiveSlots);
}

int32 UAINpcComponent::GetActiveMemoryMaintenanceSlotsForTest()
{
	return FLLMConcurrencyManager::Get().GetActiveMemorySlots();
}

int32 UAINpcComponent::GetQueuedMemoryMaintenanceRequestCountForTest()
{
	return FLLMConcurrencyManager::Get().GetQueuedMemoryCount();
}

void UAINpcComponent::PumpQueuedMemoryMaintenanceRequestsForTest()
{
	PumpQueuedMemoryMaintenanceRequests();
}
#endif

void UAINpcComponent::EnsureProvider()
{
	if (Provider.IsValid())
	{
		return;
	}

	Provider = FAINpcProviderConfigResolver::CreateProvider(*this);
}

FLLMRequest UAINpcComponent::BuildRequest() const
{
	return FAINpcDialogueRequestBuilder::BuildRequest(*this);
}

bool UAINpcComponent::TryDispatchQueuedDialogueRequest(const uint64 QueueToken)
{
	return FAINpcDialogueLifecycleHandler::TryDispatchQueuedDialogueRequest(*this, QueueToken);
}

bool UAINpcComponent::TryAcquireMemoryMaintenanceSlot()
{
	return FAINpcMemoryMaintenanceHandler::TryAcquireSlot(*this);
}

bool UAINpcComponent::TryAcquireQueuedMemoryMaintenanceSlot(const uint64 QueueToken)
{
	return FAINpcMemoryMaintenanceHandler::TryAcquireQueuedSlot(*this, QueueToken);
}

void UAINpcComponent::CancelQueuedMemoryMaintenanceRequest()
{
	FAINpcMemoryMaintenanceHandler::CancelQueuedRequest(*this);
}

void UAINpcComponent::ReleaseMemoryMaintenanceSlot()
{
	FAINpcMemoryMaintenanceHandler::ReleaseSlot(*this);
}

void UAINpcComponent::PumpQueuedMemoryMaintenanceRequests()
{
	FAINpcMemoryMaintenanceHandler::PumpQueuedRequests();
}

FString UAINpcComponent::BuildSystemPrompt() const
{
	return FAINpcDialogueRequestBuilder::BuildSystemPrompt(*this);
}

TArray<FString> UAINpcComponent::GetAvailableSmartObjectTargetsForPrompt() const
{
	return FAINpcSmartObjectPromptHandler::GetAvailableTargets(*this);
}

void UAINpcComponent::HandleRetryRequestTimerElapsed()
{
	FAINpcDialogueLifecycleHandler::HandleRetryRequestTimerElapsed(*this);
}

bool UAINpcComponent::TryHandleFailureWithFallback(const FLLMResponse& Response)
{
	return FAINpcDialogueFallbackHandler::TryHandleFailure(*this, Response);
}

void UAINpcComponent::EnsureNpcControllerAndStateTreeBinding()
{
	FAINpcComponentStateHandler::EnsureControllerBinding(*this);
}

void UAINpcComponent::SetDialogueState(const ENpcDialogueState NewState)
{
	FAINpcComponentStateHandler::SetDialogueState(*this, NewState);
}

void UAINpcComponent::ScheduleDelayMasking()
{
	FAINpcDelayMaskingHandler::Schedule(*this);
}

void UAINpcComponent::ClearDelayMaskingTimer()
{
	FAINpcDelayMaskingHandler::ClearTimer(*this);
}

void UAINpcComponent::HandleDelayMaskingThresholdReached()
{
	FAINpcDelayMaskingHandler::HandleThresholdReached(*this);
}

void UAINpcComponent::BroadcastDelayMaskingStart(UAnimMontage* Montage, const FText& FillerText)
{
	FAINpcDelayMaskingHandler::BroadcastStart(*this, Montage, FillerText);
}

void UAINpcComponent::StartDelayMasking()
{
	FAINpcDelayMaskingHandler::Start(*this);
}

void UAINpcComponent::EndDelayMasking()
{
	FAINpcDelayMaskingHandler::End(*this);
}

float UAINpcComponent::GetDelayFillerThresholdSeconds() const
{
	return FAINpcDelayMaskingHandler::GetThresholdSeconds(*this);
}

UAnimMontage* UAINpcComponent::SelectDelayMaskingMontage() const
{
	return FAINpcDelayMaskingHandler::SelectMontage(*this);
}

UAnimMontage* UAINpcComponent::SelectRandomDelayMaskingMontage(const TArray<TSoftObjectPtr<UAnimMontage>>& MontageOptions) const
{
	return FAINpcDelayMaskingHandler::SelectRandomMontage(MontageOptions);
}

UAnimMontage* UAINpcComponent::SelectEventDrivenDelayMaskingMontage(const FNpcEventMessage& EventMessage) const
{
	return FAINpcDelayMaskingHandler::SelectEventDrivenMontage(*this, EventMessage);
}

bool UAINpcComponent::IsEventRelevantForImmediateDelayMasking(const FNpcEventMessage& EventMessage) const
{
	return FAINpcDelayMaskingHandler::IsEventRelevantForImmediate(*this, EventMessage);
}

FText UAINpcComponent::SelectDelayFillerText() const
{
	return FAINpcDelayMaskingHandler::SelectFillerText(*this);
}

void UAINpcComponent::BindToNpcEventSubsystem()
{
	FAINpcEventRoutingHandler::Bind(*this);
}

void UAINpcComponent::UnbindFromNpcEventSubsystem()
{
	FAINpcEventRoutingHandler::Unbind(*this);
}

void UAINpcComponent::HandleNpcEventStageDispatched(const FNpcEventMessage& EventMessage, const ENpcEventDispatchStage DispatchStage)
{
	FAINpcEventRoutingHandler::HandleStageDispatched(*this, EventMessage, DispatchStage);
}

bool UAINpcComponent::ShouldProcessNpcEvent(const FNpcEventMessage& EventMessage) const
{
	return FAINpcEventRoutingHandler::ShouldProcess(*this, EventMessage);
}

void UAINpcComponent::ProcessNpcEventDelayMasking(const FNpcEventMessage& EventMessage)
{
	FAINpcEventRoutingHandler::ProcessDelayMasking(*this, EventMessage);
}

void UAINpcComponent::ProcessNpcEventEmotionAppraisal(const FNpcEventMessage& EventMessage)
{
	FAINpcEventRoutingHandler::ProcessEmotionAppraisal(*this, EventMessage);
}

void UAINpcComponent::ProcessNpcEventMemoryWrite(const FNpcEventMessage& EventMessage)
{
	FAINpcEventRoutingHandler::ProcessMemoryWrite(*this, EventMessage);
}

void UAINpcComponent::ProcessNpcEventPromptUpdate(const FNpcEventMessage& EventMessage)
{
	FAINpcEventRoutingHandler::ProcessPromptUpdate(*this, EventMessage);
}
