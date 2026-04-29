// RetryAttempt TimeoutFallback
#include "Components/AINpcComponent.h"
#include "AINpcCoreLog.h"
#include "Animation/AnimMontage.h"
#include "Async/Async.h"
#include "Controllers/AINpcController.h"
#include "Data/NpcPersonaDataAsset.h"
#include "Events/NpcEventPayloadTypes.h"
#include "Events/NpcEventSubsystem.h"
#include "GameFramework/Pawn.h"
#include "Engine/GameInstance.h"
#include "HAL/PlatformMisc.h"
#include "LLM/LLMConcurrencyManager.h"
#include "LLM/OpenAIProvider.h"
#include "Misc/Optional.h"
#include "Prompt/PromptBuilder.h"
#include "Settings/AINpcSettings.h"
#include "TimerManager.h"
#include "Engine/World.h"

#if defined(WITH_SMARTOBJECTS) && WITH_SMARTOBJECTS
#define AINPC_WITH_SMARTOBJECTS 1
#include "SmartObjectRequestTypes.h"
#include "SmartObjectRuntime.h"
#include "SmartObjectSubsystem.h"
#include "StructUtils/StructView.h"
#else
#define AINPC_WITH_SMARTOBJECTS 0
#endif

namespace
{
#if WITH_EDITOR
	bool GBypassDialogueRequestDispatchForTests = false;
	TOptional<TArray<FString>> GSmartObjectTargetsForPromptOverrideForTests;
#endif

	FString ResolveApiKeyFromEnvironment()
	{
		return FPlatformMisc::GetEnvironmentVariable(TEXT("AINPC_OPENAI_API_KEY")).TrimStartAndEnd();
	}
}

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
	const FString TrimmedInput = PlayerInput.TrimStartAndEnd();
	if (TrimmedInput.IsEmpty())
	{
		BroadcastError(TEXT("Player input is empty."));
		return false;
	}

	if (bIsRequestInFlight || QueuedDialogueRequestToken != 0)
	{
		BroadcastError(TEXT("A dialogue request is already pending."));
		return false;
	}

	EnsureNpcControllerAndStateTreeBinding();

	EnsureProvider();
	if (!Provider.IsValid())
	{
		BroadcastError(TEXT("Dialogue provider is unavailable."));
		return false;
	}

	const bool bStartNewSession = !bIsDialogueSessionActive;
	const int32 HistoryCountBeforeStart = ConversationHistory.Num();
	if (bStartNewSession)
	{
		bIsDialogueSessionActive = true;
		ConversationHistory.Reset();

		FLLMMessage SystemMessage;
		SystemMessage.Role = TEXT("system");
		SystemMessage.Content = BuildSystemPrompt();
		ConversationHistory.Add(MoveTemp(SystemMessage));
	}

	FLLMMessage UserMessage;
	UserMessage.Role = TEXT("user");
	UserMessage.Content = TrimmedInput;
	ConversationHistory.Add(MoveTemp(UserMessage));
	LastParsedResponse = FParsedLLMResponse();
	RetryAttemptCount = 0;
	CumulativeRetryTimeSeconds = 0.0f;
	ClearRetryTimer();

	if (!DispatchDialogueRequest())
	{
		BroadcastError(TEXT("Failed to dispatch dialogue request."));
		ClearDelayMaskingTimer();
		EndDelayMasking();
		ConversationHistory.SetNum(HistoryCountBeforeStart, EAllowShrinking::No);
		if (bStartNewSession)
		{
			bIsDialogueSessionActive = false;
		}
		SetDialogueState(ENpcDialogueState::Idle);
		return false;
	}

	if (bStartNewSession)
	{
		OnDialogueSessionStarted.Broadcast();
		DialogueSessionStartedNative.Broadcast();
	}

	return true;
}

void UAINpcComponent::EndDialogue()
{
	const bool bWasSessionActive = bIsDialogueSessionActive;
	bIsDialogueSessionActive = false;
	ClearRetryTimer();
	RetryAttemptCount = 0;
	CumulativeRetryTimeSeconds = 0.0f;
	CancelQueuedDialogueRequest();

	if (bIsRequestInFlight && Provider.IsValid() && ActiveRequestId.IsValid())
	{
		Provider->CancelRequest(ActiveRequestId);
	}

	ClearDelayMaskingTimer();
	EndDelayMasking();
	ClearActiveRequest();
	ConversationHistory.Reset();
	LastParsedResponse = FParsedLLMResponse();
	SetDialogueState(ENpcDialogueState::Idle);

	if (bWasSessionActive)
	{
		OnDialogueSessionEnded.Broadcast();
		DialogueSessionEndedNative.Broadcast();
	}
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

void UAINpcComponent::SetApiKey(const FString& NewApiKey)
{
	ApiKeyOverride = NewApiKey;
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
	for (const FNpcAction& Action : LastParsedResponse.Actions)
	{
		const FString TrimmedActionType = Action.ActionType.TrimStartAndEnd();
		if (!TrimmedActionType.IsEmpty() && !TrimmedActionType.Equals(TEXT("Action.DefaultTalk"), ESearchCase::CaseSensitive))
		{
			OutActionIntent = Action;
			OutActionIntent.ActionType = TrimmedActionType;
			OutActionIntent.Target = OutActionIntent.Target.TrimStartAndEnd();
			return true;
		}
	}

	OutActionIntent = FNpcAction();
	return false;
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

void UAINpcComponent::SetDialogueStateFromStateTree(const ENpcDialogueState NewState)
{
	SetDialogueState(NewState);
}

void UAINpcComponent::HandleStateTreeTimeoutFailure()
{
	if (!bIsDialogueSessionActive)
	{
		return;
	}

	FLLMResponse TimeoutResponse;
	TimeoutResponse.bSuccess = false;
	TimeoutResponse.RequestId = ActiveRequestId;
	TimeoutResponse.HttpStatusCode = 408;
	TimeoutResponse.ErrorMessage = TEXT("Dialogue request timed out before callback was processed.");

	ClearRetryTimer();
	ClearDelayMaskingTimer();
	EndDelayMasking();
	CancelQueuedDialogueRequest();

	if (bIsRequestInFlight && Provider.IsValid() && ActiveRequestId.IsValid())
	{
		Provider->CancelRequest(ActiveRequestId);
	}

	ClearActiveRequest();
	RetryAttemptCount = 0;
	CumulativeRetryTimeSeconds = 0.0f;
	LastParsedResponse = FParsedLLMResponse();
	if (TryHandleFailureWithFallback(TimeoutResponse))
	{
		return;
	}

	SetDialogueState(ENpcDialogueState::Idle);
	BroadcastError(TimeoutResponse.ErrorMessage);
}

bool UAINpcComponent::HasBeenInDialogueStateLongerThan(const float DurationSeconds) const
{
	if (DurationSeconds <= 0.0f)
	{
		return true;
	}

	return (FPlatformTime::Seconds() - DialogueStateEnterTimeSeconds) >= static_cast<double>(DurationSeconds);
}

bool UAINpcComponent::IsDelayMaskingActive() const
{
	return bDelayMaskingActive;
}

bool UAINpcComponent::TryStartMemoryMaintenance()
{
	return TryAcquireMemoryMaintenanceSlot();
}

void UAINpcComponent::EndMemoryMaintenance()
{
	CancelQueuedMemoryMaintenanceRequest();
	ReleaseMemoryMaintenanceSlot();
}

bool UAINpcComponent::IsMemoryMaintenanceActive() const
{
	return bOwnsMemoryMaintenanceSlot;
}

void UAINpcComponent::HandleDialogueResponseDynamicForTest(const FString& ResponseText)
{
	(void)ResponseText;
#if WITH_EDITOR
	++DynamicDialogueResponseCountForTest;
#endif
}

void UAINpcComponent::HandleDialogueErrorDynamicForTest(const FString& ErrorMessage)
{
	(void)ErrorMessage;
#if WITH_EDITOR
	++DynamicDialogueErrorCountForTest;
#endif
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

#if WITH_EDITOR
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

void UAINpcComponent::HandleRequestCompletedForTest(const FLLMResponse& Response)
{
	HandleRequestCompleted(Response);
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
	GSmartObjectTargetsForPromptOverrideForTests = InTargets;
}

void UAINpcComponent::ClearSmartObjectTargetsForPromptForTest()
{
	GSmartObjectTargetsForPromptOverrideForTests.Reset();
}

int32 UAINpcComponent::GetRetryAttemptCountForTest() const
{
	return RetryAttemptCount;
}

float UAINpcComponent::GetRetryDelaySecondsForTest(const int32 RetryAttemptIndex) const
{
	return GetRetryDelaySeconds(RetryAttemptIndex);
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
	GBypassDialogueRequestDispatchForTests = false;
	GSmartObjectTargetsForPromptOverrideForTests.Reset();
}

void UAINpcComponent::SetDialogueDispatchBypassForTest(const bool bBypassDispatch)
{
	GBypassDialogueRequestDispatchForTests = bBypassDispatch;
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
	PumpQueuedDialogueRequests();
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

	const UAINpcSettings* Settings = GetDefault<UAINpcSettings>();
	FString ApiKey = Settings ? Settings->GlobalApiKey.TrimStartAndEnd() : FString();
	if (ApiKey.IsEmpty())
	{
		ApiKey = ResolveApiKeyFromEnvironment();
	}

	FString BaseUrl = Settings ? Settings->GlobalBaseUrl.TrimStartAndEnd() : TEXT("https://api.openai.com/v1");
	FString Model = Settings ? Settings->GlobalModel.TrimStartAndEnd() : TEXT("gpt-4o-mini");

	Provider = MakeShared<FOpenAIProvider, ESPMode::ThreadSafe>(ApiKey, Model, BaseUrl);
}

FLLMRequest UAINpcComponent::BuildRequest() const
{
	FLLMRequest Request;
	Request.Messages = ConversationHistory;

	const UAINpcSettings* Settings = GetDefault<UAINpcSettings>();
	if (Settings)
	{
		Request.ApiKey = Settings->GlobalApiKey;
		Request.ApiKey = Request.ApiKey.TrimStartAndEnd();
		Request.BaseUrl = Settings->GlobalBaseUrl.TrimStartAndEnd();
		Request.Model = Settings->GlobalModel.TrimStartAndEnd();
		Request.TimeoutSeconds = FMath::Max(0.0f, Settings->RequestTimeoutSeconds);
	}

	if (PersonaDataAsset)
	{
		if (!PersonaDataAsset->ApiKey.TrimStartAndEnd().IsEmpty())
		{
			Request.ApiKey = PersonaDataAsset->ApiKey;
			Request.ApiKey = Request.ApiKey.TrimStartAndEnd();
		}
		if (!PersonaDataAsset->BaseUrl.TrimStartAndEnd().IsEmpty())
		{
			Request.BaseUrl = PersonaDataAsset->BaseUrl.TrimStartAndEnd();
		}
		if (!PersonaDataAsset->Model.TrimStartAndEnd().IsEmpty())
		{
			Request.Model = PersonaDataAsset->Model.TrimStartAndEnd();
		}
	}

	if (!ApiKeyOverride.TrimStartAndEnd().IsEmpty())
	{
		Request.ApiKey = ApiKeyOverride.TrimStartAndEnd();
	}
	if (!BaseUrlOverride.TrimStartAndEnd().IsEmpty())
	{
		Request.BaseUrl = BaseUrlOverride.TrimStartAndEnd();
	}
	if (!ModelOverride.TrimStartAndEnd().IsEmpty())
	{
		Request.Model = ModelOverride.TrimStartAndEnd();
	}

	if (Request.ApiKey.IsEmpty())
	{
		Request.ApiKey = ResolveApiKeyFromEnvironment();
	}

	return Request;
}

bool UAINpcComponent::DispatchDialogueRequest()
{
	check(IsInGameThread());
	if (bIsRequestInFlight || bOwnsDialogueDispatchSlot || QueuedDialogueRequestToken != 0)
	{
		return false;
	}

	uint64 QueueToken = 0;
	if (!FLLMConcurrencyManager::Get().TryAcquireDialogueSlot(this, QueueToken))
	{
		QueuedDialogueRequestToken = QueueToken;
		SetDialogueState(ENpcDialogueState::WaitingForLLM);
		return true;
	}

	bOwnsDialogueDispatchSlot = true;
	if (DispatchDialogueRequestNow())
	{
		return true;
	}

	ReleaseDialogueDispatchSlot();
	return false;
}

bool UAINpcComponent::DispatchDialogueRequestNow()
{
#if WITH_EDITOR
	if (GBypassDialogueRequestDispatchForTests)
	{
		ActiveRequestId = FGuid::NewGuid();
		bIsRequestInFlight = true;
		SetDialogueState(ENpcDialogueState::WaitingForLLM);
		ScheduleDelayMasking();
		return true;
	}
#endif

	EnsureProvider();
	if (!Provider.IsValid())
	{
		return false;
	}

	FLLMRequest Request = BuildRequest();
	Request.bUseStreaming = true;
	Request.StreamCallback = [WeakThis = TWeakObjectPtr<UAINpcComponent>(this)](const FLLMStreamChunk& Chunk)
	{
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Chunk]()
		{
			UAINpcComponent* Component = WeakThis.Get();
			if (!Component || !Component->bIsDialogueSessionActive)
			{
				return;
			}

			if (Chunk.bIsFinal || Chunk.Content.IsEmpty())
			{
				return;
			}

			Component->OnPartialResponse.Broadcast(Chunk.Content);
			Component->DialoguePartialResponseNative.Broadcast(Chunk.Content);
		});
	};
	ActiveRequestId = Provider->SendRequest(
		Request,
		[WeakThis = TWeakObjectPtr<UAINpcComponent>(this)](const FLLMResponse& Response)
		{
			AsyncTask(ENamedThreads::GameThread, [WeakThis, Response]()
			{
				if (UAINpcComponent* Component = WeakThis.Get())
				{
					Component->HandleRequestCompleted(Response);
				}
			});
		});

	bIsRequestInFlight = ActiveRequestId.IsValid();
	if (!bIsRequestInFlight)
	{
		return false;
	}

	SetDialogueState(ENpcDialogueState::WaitingForLLM);
	ScheduleDelayMasking();
	return true;
}

bool UAINpcComponent::TryDispatchQueuedDialogueRequest(const uint64 QueueToken)
{
	if (QueuedDialogueRequestToken != QueueToken)
	{
		return false;
	}

	QueuedDialogueRequestToken = 0;

	if (!bIsDialogueSessionActive || bIsRequestInFlight)
	{
		return false;
	}

	if (!DispatchDialogueRequestNow())
	{
		SetDialogueState(ENpcDialogueState::Idle);
		BroadcastError(TEXT("Failed to dispatch queued dialogue request."));
		return false;
	}

	bOwnsDialogueDispatchSlot = true;
	return true;
}

void UAINpcComponent::CancelQueuedDialogueRequest()
{
	if (QueuedDialogueRequestToken == 0)
	{
		return;
	}

	const uint64 QueueTokenToRemove = QueuedDialogueRequestToken;
	QueuedDialogueRequestToken = 0;
	FLLMConcurrencyManager::Get().CancelQueuedDialogueRequest(this, QueueTokenToRemove);
}

void UAINpcComponent::ReleaseDialogueDispatchSlot()
{
	check(IsInGameThread());
	if (!bOwnsDialogueDispatchSlot)
	{
		return;
	}

	bOwnsDialogueDispatchSlot = false;
	FLLMConcurrencyManager::Get().ReleaseDialogueSlot();
}

void UAINpcComponent::PumpQueuedDialogueRequests()
{
	FLLMConcurrencyManager::Get().PumpDialogueQueue();
}

int32 UAINpcComponent::GetDialogueRequestConcurrencyLimit()
{
	const UAINpcSettings* Settings = GetDefault<UAINpcSettings>();
	return FMath::Max(1, Settings ? Settings->DialogueRequestConcurrencyLimit : 1);
}

bool UAINpcComponent::TryAcquireMemoryMaintenanceSlot()
{
	check(IsInGameThread());
	if (bOwnsMemoryMaintenanceSlot || QueuedMemoryMaintenanceRequestToken != 0)
	{
		return false;
	}

	uint64 QueueToken = 0;
	if (!FLLMConcurrencyManager::Get().TryAcquireMemorySlot(this, QueueToken))
	{
		QueuedMemoryMaintenanceRequestToken = QueueToken;
		return false;
	}

	bOwnsMemoryMaintenanceSlot = true;
	return true;
}

bool UAINpcComponent::TryAcquireQueuedMemoryMaintenanceSlot(const uint64 QueueToken)
{
	if (QueuedMemoryMaintenanceRequestToken != QueueToken)
	{
		return false;
	}

	QueuedMemoryMaintenanceRequestToken = 0;
	bOwnsMemoryMaintenanceSlot = true;
	return true;
}

void UAINpcComponent::CancelQueuedMemoryMaintenanceRequest()
{
	if (QueuedMemoryMaintenanceRequestToken == 0)
	{
		return;
	}

	const uint64 QueueTokenToRemove = QueuedMemoryMaintenanceRequestToken;
	QueuedMemoryMaintenanceRequestToken = 0;
	FLLMConcurrencyManager::Get().CancelQueuedMemoryRequest(this, QueueTokenToRemove);
}

void UAINpcComponent::ReleaseMemoryMaintenanceSlot()
{
	check(IsInGameThread());
	if (!bOwnsMemoryMaintenanceSlot)
	{
		return;
	}

	bOwnsMemoryMaintenanceSlot = false;
	FLLMConcurrencyManager::Get().ReleaseMemorySlot();
}

void UAINpcComponent::PumpQueuedMemoryMaintenanceRequests()
{
	FLLMConcurrencyManager::Get().PumpMemoryQueue();
}

int32 UAINpcComponent::GetMemoryMaintenanceConcurrencyLimit()
{
	const UAINpcSettings* Settings = GetDefault<UAINpcSettings>();
	return FMath::Max(1, Settings ? Settings->MemoryMaintenanceConcurrencyLimit : 1);
}

FString UAINpcComponent::BuildSystemPrompt() const
{
	FPromptBuilderConfig BuilderConfig;
	BuilderConfig.MaxPromptTokens = 512;
	BuilderConfig.AvailableSmartObjectTargets = GetAvailableSmartObjectTargetsForPrompt();
	return FPromptBuilder::BuildSystemPrompt(PersonaDataAsset, BuilderConfig);
}

TArray<FString> UAINpcComponent::GetAvailableSmartObjectTargetsForPrompt() const
{
	TArray<FString> AvailableTargets;

#if WITH_EDITOR
	if (GSmartObjectTargetsForPromptOverrideForTests.IsSet())
	{
		AvailableTargets = GSmartObjectTargetsForPromptOverrideForTests.GetValue();
		AvailableTargets.Sort();
		return AvailableTargets;
	}
#endif

#if AINPC_WITH_SMARTOBJECTS
	AActor* OwnerActor = GetOwner();
	UWorld* World = GetWorld();
	if (!IsValid(OwnerActor) || !IsValid(World))
	{
		return AvailableTargets;
	}

	USmartObjectSubsystem* SmartObjectSubsystem = USmartObjectSubsystem::GetCurrent(World);
	if (!IsValid(SmartObjectSubsystem))
	{
		return AvailableTargets;
	}

	constexpr float PromptSmartObjectSearchRadius = 1000.0f;
	const FVector QueryExtent(PromptSmartObjectSearchRadius);
	const FBox QueryBox = FBox::BuildAABB(OwnerActor->GetActorLocation(), QueryExtent);
	const FSmartObjectRequestFilter RequestFilter;
	const FSmartObjectRequest Request(QueryBox, RequestFilter);

	TArray<FSmartObjectRequestResult> Results;
	const bool bFound = SmartObjectSubsystem->FindSmartObjects(
		Request,
		Results,
		FConstStructView::Make(FSmartObjectActorUserData(OwnerActor)));
	if (!bFound || Results.IsEmpty())
	{
		return AvailableTargets;
	}

	TSet<FString> UniqueTargets;
	constexpr int32 MaxPromptTargets = 12;

	for (const FSmartObjectRequestResult& Result : Results)
	{
		if (!Result.IsValid())
		{
			continue;
		}

		const FString SlotIdentifier = LexToString(Result.SlotHandle);
		if (!SlotIdentifier.IsEmpty())
		{
			UniqueTargets.Add(SlotIdentifier);
		}

		if (UniqueTargets.Num() >= MaxPromptTargets)
		{
			break;
		}
	}

	AvailableTargets = UniqueTargets.Array();
	AvailableTargets.Sort();
	if (AvailableTargets.Num() > MaxPromptTargets)
	{
		AvailableTargets.SetNum(MaxPromptTargets);
	}
#endif

	return AvailableTargets;
}

void UAINpcComponent::HandleRequestCompleted(const FLLMResponse& Response)
{
	if (!bIsDialogueSessionActive)
	{
		return;
	}

	if (!bIsRequestInFlight || ActiveRequestId != Response.RequestId)
	{
		return;
	}

	ClearRetryTimer();
	ClearDelayMaskingTimer();
	EndDelayMasking();

	if (Response.bSuccess)
	{
		ClearActiveRequest();
		RetryAttemptCount = 0;
		CumulativeRetryTimeSeconds = 0.0f;
		SetDialogueState(ENpcDialogueState::Speaking);
		LastParsedResponse = Response.ParsedResponse;

		FLLMMessage AssistantMessage;
		AssistantMessage.Role = TEXT("assistant");
		AssistantMessage.Content = Response.Content;
		ConversationHistory.Add(MoveTemp(AssistantMessage));

		OnDialogueResponse.Broadcast(Response.Content);
		DialogueResponseNative.Broadcast(Response.Content);
		return;
	}

	const int32 MaxRetryAttempts = GetMaxRetryAttempts();
	if (IsRetryableFailure(Response) && RetryAttemptCount < MaxRetryAttempts)
	{
		const float RetryDelaySeconds = GetRetryDelaySeconds(RetryAttemptCount);
		const UAINpcSettings* Settings = GetDefault<UAINpcSettings>();
		const float MaxTotalRetryTime = Settings ? Settings->MaxTotalRetryTimeSeconds : 30.0f;

		if (CumulativeRetryTimeSeconds + RetryDelaySeconds > MaxTotalRetryTime)
		{
			UE_LOG(LogAINpc, Warning, TEXT("Total retry time budget exceeded (%.2fs + %.2fs > %.2fs), aborting retries"),
				CumulativeRetryTimeSeconds, RetryDelaySeconds, MaxTotalRetryTime);
		}
		else
		{
			ClearActiveRequest();
			CumulativeRetryTimeSeconds += RetryDelaySeconds;
			++RetryAttemptCount;
			UE_LOG(LogAINpc, Log, TEXT("Retry attempt %d with exponential backoff delay %.2fs (cumulative: %.2fs)"),
				RetryAttemptCount, RetryDelaySeconds, CumulativeRetryTimeSeconds);
			ScheduleRetryRequest(RetryDelaySeconds);
			return;
		}
	}

	ClearActiveRequest();
	RetryAttemptCount = 0;
	CumulativeRetryTimeSeconds = 0.0f;
	LastParsedResponse = FParsedLLMResponse();
	if (TryHandleFailureWithFallback(Response))
	{
		return;
	}

	const FString ErrorText = Response.ErrorMessage.IsEmpty()
		? TEXT("Dialogue request failed.")
		: Response.ErrorMessage;
	SetDialogueState(ENpcDialogueState::Idle);
	BroadcastError(ErrorText);
}

bool UAINpcComponent::IsRetryableFailure(const FLLMResponse& Response) const
{
	if (Response.bSuccess)
	{
		return false;
	}

	if (Response.HttpStatusCode == 408 || Response.HttpStatusCode == 425 || Response.HttpStatusCode == 429)
	{
		return true;
	}

	if (Response.HttpStatusCode >= 500 && Response.HttpStatusCode <= 599)
	{
		return true;
	}

	return Response.ErrorMessage.Contains(TEXT("request timed out"), ESearchCase::IgnoreCase) ||
	       Response.ErrorMessage.Contains(TEXT("connection timed out"), ESearchCase::IgnoreCase);
}

int32 UAINpcComponent::GetMaxRetryAttempts() const
{
	const UAINpcSettings* Settings = GetDefault<UAINpcSettings>();
	return FMath::Max(0, Settings ? Settings->MaxRequestRetries : 0);
}

float UAINpcComponent::GetRetryBackoffBaseSeconds() const
{
	const UAINpcSettings* Settings = GetDefault<UAINpcSettings>();
	return FMath::Max(0.0f, Settings ? Settings->RetryBackoffBaseSeconds : 0.0f);
}

float UAINpcComponent::GetRetryDelaySeconds(const int32 RetryAttemptIndex) const
{
	const float BaseSeconds = GetRetryBackoffBaseSeconds();
	if (BaseSeconds <= 0.0f)
	{
		return 0.0f;
	}

	const int32 SafeAttemptIndex = FMath::Max(0, RetryAttemptIndex);
	const float Delay = BaseSeconds * FMath::Pow(2.0f, static_cast<float>(SafeAttemptIndex));

	const UAINpcSettings* Settings = GetDefault<UAINpcSettings>();
	const float MaxDelay = Settings ? Settings->MaxRetryDelaySeconds : 16.0f;
	return FMath::Min(Delay, MaxDelay);
}

void UAINpcComponent::ScheduleRetryRequest(const float DelaySeconds)
{
	if (!bIsDialogueSessionActive)
	{
		return;
	}

	ClearRetryTimer();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			RetryTimerHandle,
			this,
			&UAINpcComponent::HandleRetryRequestTimerElapsed,
			FMath::Max(0.0f, DelaySeconds),
			false);
	}
}

void UAINpcComponent::ClearRetryTimer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RetryTimerHandle);
	}

	RetryTimerHandle.Invalidate();
}

void UAINpcComponent::HandleRetryRequestTimerElapsed()
{
	if (!bIsDialogueSessionActive || bIsRequestInFlight)
	{
		return;
	}

	EnsureProvider();
	if (!Provider.IsValid())
	{
		FLLMResponse FailureResponse;
		FailureResponse.bSuccess = false;
		FailureResponse.ErrorMessage = TEXT("Dialogue provider is unavailable.");
		if (!TryHandleFailureWithFallback(FailureResponse))
		{
			SetDialogueState(ENpcDialogueState::Idle);
			BroadcastError(FailureResponse.ErrorMessage);
		}
		return;
	}

	if (!DispatchDialogueRequest())
	{
		FLLMResponse FailureResponse;
		FailureResponse.bSuccess = false;
		FailureResponse.ErrorMessage = TEXT("Failed to dispatch dialogue retry request.");
		if (!TryHandleFailureWithFallback(FailureResponse))
		{
			SetDialogueState(ENpcDialogueState::Idle);
			BroadcastError(FailureResponse.ErrorMessage);
		}
	}
}

bool UAINpcComponent::TryHandleFailureWithFallback(const FLLMResponse& Response)
{
	const FString FallbackResponseText = ResolveFallbackResponseText().TrimStartAndEnd();
	if (FallbackResponseText.IsEmpty())
	{
		return false;
	}

	UE_LOG(LogAINpc, Log, TEXT("Timeout fallback using template response, degradation notification sent"));

	SetDialogueState(ENpcDialogueState::Speaking);

	FLLMMessage AssistantMessage;
	AssistantMessage.Role = TEXT("assistant");
	AssistantMessage.Content = FallbackResponseText;
	ConversationHistory.Add(MoveTemp(AssistantMessage));

	const FString FailureReason = Response.ErrorMessage.IsEmpty()
		? TEXT("Dialogue request failed.")
		: Response.ErrorMessage;

	OnDialogueDegraded.Broadcast(FallbackResponseText, FailureReason);
	DialogueDegradedNative.Broadcast(FallbackResponseText, FailureReason);
	return true;
}

FString UAINpcComponent::ResolveFallbackResponseText() const
{
	if (PersonaDataAsset && !PersonaDataAsset->FailureFallbackResponse.IsEmptyOrWhitespace())
	{
		return PersonaDataAsset->FailureFallbackResponse.ToString();
	}

	const UAINpcSettings* Settings = GetDefault<UAINpcSettings>();
	if (Settings && !Settings->FallbackResponseTemplate.IsEmpty())
	{
		return Settings->FallbackResponseTemplate;
	}

	return TEXT("I need a moment to think. Could you ask me again?");
}

void UAINpcComponent::BroadcastError(const FString& ErrorMessage)
{
	OnDialogueError.Broadcast(ErrorMessage);
	DialogueErrorNative.Broadcast(ErrorMessage);
}

void UAINpcComponent::ClearActiveRequest()
{
	bIsRequestInFlight = false;
	ActiveRequestId.Invalidate();
	ClearDelayMaskingTimer();
	ReleaseDialogueDispatchSlot();
}

void UAINpcComponent::EnsureNpcControllerAndStateTreeBinding()
{
	APawn* PawnOwner = Cast<APawn>(GetOwner());
	if (!PawnOwner)
	{
		return;
	}

	AController* ActiveController = PawnOwner->GetController();

	if (!ActiveController && bAutoCreateNpcController && PawnOwner->HasAuthority())
	{
		PawnOwner->AIControllerClass = AAINpcController::StaticClass();
		PawnOwner->SpawnDefaultController();
		ActiveController = PawnOwner->GetController();
	}

	if (AAINpcController* NpcController = Cast<AAINpcController>(ActiveController))
	{
		NpcController->ConfigureFromComponent(this);
	}
}

void UAINpcComponent::SetDialogueState(const ENpcDialogueState NewState)
{
	if (CurrentDialogueState == NewState)
	{
		return;
	}

	if (NewState != ENpcDialogueState::WaitingForLLM)
	{
		ClearDelayMaskingTimer();
		EndDelayMasking();
	}

	CurrentDialogueState = NewState;
	DialogueStateEnterTimeSeconds = FPlatformTime::Seconds();
}

void UAINpcComponent::ScheduleDelayMasking()
{
	ClearDelayMaskingTimer();

	if (!bIsRequestInFlight || CurrentDialogueState != ENpcDialogueState::WaitingForLLM)
	{
		return;
	}

	const float DelayThresholdSeconds = GetDelayFillerThresholdSeconds();
	if (DelayThresholdSeconds <= UE_KINDA_SMALL_NUMBER)
	{
		HandleDelayMaskingThresholdReached();
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			DelayMaskingTimerHandle,
			this,
			&UAINpcComponent::HandleDelayMaskingThresholdReached,
			DelayThresholdSeconds,
			false);
	}
}

void UAINpcComponent::ClearDelayMaskingTimer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DelayMaskingTimerHandle);
	}

	DelayMaskingTimerHandle.Invalidate();
}

void UAINpcComponent::HandleDelayMaskingThresholdReached()
{
	if (!bIsRequestInFlight || CurrentDialogueState != ENpcDialogueState::WaitingForLLM)
	{
		return;
	}

	if (bDelayMaskingActive)
	{
		const FText FillerText = SelectDelayFillerText();
		if (!FillerText.IsEmptyOrWhitespace())
		{
			BroadcastDelayMaskingStart(nullptr, FillerText);
		}
		return;
	}

	StartDelayMasking();
}

void UAINpcComponent::BroadcastDelayMaskingStart(UAnimMontage* Montage, const FText& FillerText)
{
	OnDelayMaskingStart.Broadcast(Montage, FillerText);
	DelayMaskingStartNative.Broadcast(Montage, FillerText);
}

void UAINpcComponent::StartDelayMasking()
{
	if (bDelayMaskingActive)
	{
		return;
	}

	bDelayMaskingActive = true;
	UAnimMontage* Montage = SelectDelayMaskingMontage();
	const FText FillerText = SelectDelayFillerText();
	BroadcastDelayMaskingStart(Montage, FillerText);
}

void UAINpcComponent::EndDelayMasking()
{
	if (!bDelayMaskingActive)
	{
		return;
	}

	bDelayMaskingActive = false;
	OnDelayMaskingEnd.Broadcast();
	DelayMaskingEndNative.Broadcast();
}

float UAINpcComponent::GetDelayFillerThresholdSeconds() const
{
	if (!PersonaDataAsset)
	{
		return 3.0f;
	}

	return FMath::Max(0.0f, PersonaDataAsset->DelayFillerThreshold);
}

UAnimMontage* UAINpcComponent::SelectDelayMaskingMontage() const
{
	if (!PersonaDataAsset)
	{
		return nullptr;
	}

	return SelectRandomDelayMaskingMontage(PersonaDataAsset->DelayMaskingMontages);
}

UAnimMontage* UAINpcComponent::SelectRandomDelayMaskingMontage(const TArray<TSoftObjectPtr<UAnimMontage>>& MontageOptions) const
{
	if (MontageOptions.IsEmpty())
	{
		return nullptr;
	}

	TArray<int32> ValidIndices;
	ValidIndices.Reserve(MontageOptions.Num());
	for (int32 Index = 0; Index < MontageOptions.Num(); ++Index)
	{
		if (!MontageOptions[Index].IsNull())
		{
			ValidIndices.Add(Index);
		}
	}

	if (ValidIndices.IsEmpty())
	{
		return nullptr;
	}

	const int32 ChosenIndex = ValidIndices[FMath::RandHelper(ValidIndices.Num())];
	UAnimMontage* Montage = MontageOptions[ChosenIndex].LoadSynchronous();
	if (!Montage)
	{
		UE_LOG(LogAINpc, Warning, TEXT("SelectRandomDelayMaskingMontage: Synchronous load required for montage at index %d"), ChosenIndex);
	}
	return Montage;
}

UAnimMontage* UAINpcComponent::SelectEventDrivenDelayMaskingMontage(const FNpcEventMessage& EventMessage) const
{
	if (!PersonaDataAsset)
	{
		return nullptr;
	}

	if (!IsEventRelevantForImmediateDelayMasking(EventMessage))
	{
		return nullptr;
	}

	if (EventMessage.Payload.GetPtr<FNpcAttackEventPayload>())
	{
		if (PersonaDataAsset->HitReactionDelayMaskingMontages.IsEmpty())
		{
			UE_LOG(LogAINpc, Warning, TEXT("SelectEventDrivenDelayMaskingMontage: HitReactionDelayMaskingMontages is empty for attack event"));
			return nullptr;
		}
		return SelectRandomDelayMaskingMontage(PersonaDataAsset->HitReactionDelayMaskingMontages);
	}

	if (EventMessage.Payload.GetPtr<FNpcGiftEventPayload>())
	{
		if (PersonaDataAsset->InspectDelayMaskingMontages.IsEmpty())
		{
			UE_LOG(LogAINpc, Warning, TEXT("SelectEventDrivenDelayMaskingMontage: InspectDelayMaskingMontages is empty for gift event"));
			return nullptr;
		}
		return SelectRandomDelayMaskingMontage(PersonaDataAsset->InspectDelayMaskingMontages);
	}

	if (EventMessage.Payload.GetPtr<FNpcTradeEventPayload>())
	{
		if (PersonaDataAsset->InspectDelayMaskingMontages.IsEmpty())
		{
			UE_LOG(LogAINpc, Warning, TEXT("SelectEventDrivenDelayMaskingMontage: InspectDelayMaskingMontages is empty for trade event"));
			return nullptr;
		}
		return SelectRandomDelayMaskingMontage(PersonaDataAsset->InspectDelayMaskingMontages);
	}

	return nullptr;
}

bool UAINpcComponent::IsEventRelevantForImmediateDelayMasking(const FNpcEventMessage& EventMessage) const
{
	if (const FNpcAttackEventPayload* AttackPayload = EventMessage.Payload.GetPtr<FNpcAttackEventPayload>())
	{
		return !AttackPayload->TargetActor || AttackPayload->TargetActor == GetOwner();
	}

	if (const FNpcGiftEventPayload* GiftPayload = EventMessage.Payload.GetPtr<FNpcGiftEventPayload>())
	{
		return !GiftPayload->ReceiverActor || GiftPayload->ReceiverActor == GetOwner();
	}

	if (const FNpcTradeEventPayload* TradePayload = EventMessage.Payload.GetPtr<FNpcTradeEventPayload>())
	{
		const AActor* const OwnerActor = GetOwner();
		if (!OwnerActor)
		{
			UE_LOG(LogAINpc, Warning, TEXT("IsEventRelevantForImmediateDelayMasking: GetOwner() returned null for trade event"));
			return false;
		}
		return (TradePayload->InitiatorActor == OwnerActor)
			|| (TradePayload->CounterpartyActor == OwnerActor)
			|| (!TradePayload->InitiatorActor && !TradePayload->CounterpartyActor);
	}

	return false;
}

FText UAINpcComponent::SelectDelayFillerText() const
{
	if (!PersonaDataAsset || PersonaDataAsset->DelayFillerTexts.IsEmpty())
	{
		return FText::GetEmpty();
	}

	TArray<int32> ValidIndices;
	ValidIndices.Reserve(PersonaDataAsset->DelayFillerTexts.Num());
	for (int32 Index = 0; Index < PersonaDataAsset->DelayFillerTexts.Num(); ++Index)
	{
		if (!PersonaDataAsset->DelayFillerTexts[Index].IsEmptyOrWhitespace())
		{
			ValidIndices.Add(Index);
		}
	}

	if (ValidIndices.IsEmpty())
	{
		return FText::GetEmpty();
	}

	const int32 ChosenIndex = ValidIndices[FMath::RandHelper(ValidIndices.Num())];
	return PersonaDataAsset->DelayFillerTexts[ChosenIndex];
}

void UAINpcComponent::BindToNpcEventSubsystem()
{
	if (EventStageDispatchedHandle.IsValid())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	if (!GameInstance)
	{
		return;
	}

	UNpcEventSubsystem* EventSubsystem = GameInstance->GetSubsystem<UNpcEventSubsystem>();
	if (!EventSubsystem)
	{
		return;
	}

	BoundEventSubsystem = EventSubsystem;
	EventStageDispatchedHandle = EventSubsystem->OnEventStageDispatchedNative().AddUObject(this, &UAINpcComponent::HandleNpcEventStageDispatched);
}

void UAINpcComponent::UnbindFromNpcEventSubsystem()
{
	if (UNpcEventSubsystem* EventSubsystem = BoundEventSubsystem.Get())
	{
		if (EventStageDispatchedHandle.IsValid())
		{
			EventSubsystem->OnEventStageDispatchedNative().Remove(EventStageDispatchedHandle);
		}
	}

	BoundEventSubsystem.Reset();
	EventStageDispatchedHandle.Reset();
}

void UAINpcComponent::HandleNpcEventStageDispatched(const FNpcEventMessage& EventMessage, const ENpcEventDispatchStage DispatchStage)
{
	if (!ShouldProcessNpcEvent(EventMessage))
	{
		return;
	}

	switch (DispatchStage)
	{
	case ENpcEventDispatchStage::DelayMasking:
		ProcessNpcEventDelayMasking(EventMessage);
		break;
	case ENpcEventDispatchStage::EmotionAppraisal:
		ProcessNpcEventEmotionAppraisal(EventMessage);
		break;
	case ENpcEventDispatchStage::MemoryWrite:
		ProcessNpcEventMemoryWrite(EventMessage);
		break;
	case ENpcEventDispatchStage::PromptUpdate:
		ProcessNpcEventPromptUpdate(EventMessage);
		break;
	default:
		break;
	}
}

bool UAINpcComponent::ShouldProcessNpcEvent(const FNpcEventMessage& EventMessage) const
{
	if (EventSubscriptionTags.IsEmpty())
	{
		return true;
	}

	FGameplayTagContainer EffectiveRoutingTags = EventMessage.RoutingTags;
	if (EffectiveRoutingTags.IsEmpty() && EventMessage.EventTag.IsValid())
	{
		// EventTag is the routing fallback when explicit routing tags are not provided.
		EffectiveRoutingTags.AddTag(EventMessage.EventTag);
	}

	if (EffectiveRoutingTags.IsEmpty())
	{
		return true;
	}

	return EffectiveRoutingTags.HasAny(EventSubscriptionTags);
}

void UAINpcComponent::ProcessNpcEventDelayMasking(const FNpcEventMessage& EventMessage)
{
	if (!IsEventRelevantForImmediateDelayMasking(EventMessage))
	{
		return;
	}

	// Event-driven delay masking: SelectEventDrivenDelayMaskingMontage selects montage, then BroadcastDelayMaskingStart triggers immediately
	if (UAnimMontage* EventDrivenMontage = SelectEventDrivenDelayMaskingMontage(EventMessage))
	{
		UE_LOG(LogAINpc, Log, TEXT("Event-driven Montage Play triggered immediately (bypassing StateTree): %s for event %s"),
			*EventDrivenMontage->GetName(), *EventMessage.EventTag.ToString());

		if (bDelayMaskingActive)
		{
			ClearDelayMaskingTimer();
		}
		else
		{
			bDelayMaskingActive = true;
		}

		const FText FillerText = FText::GetEmpty();
		// Immediate broadcast: EventDrivenMontage triggers BroadcastDelayMaskingStart without delay
		BroadcastDelayMaskingStart(EventDrivenMontage, FillerText);
		return;
	}

	if (bIsRequestInFlight && CurrentDialogueState == ENpcDialogueState::WaitingForLLM)
	{
		StartDelayMasking();
	}
}

void UAINpcComponent::ProcessNpcEventEmotionAppraisal(const FNpcEventMessage& EventMessage)
{
	(void)EventMessage;
}

void UAINpcComponent::ProcessNpcEventMemoryWrite(const FNpcEventMessage& EventMessage)
{
	(void)EventMessage;
}

void UAINpcComponent::ProcessNpcEventPromptUpdate(const FNpcEventMessage& EventMessage)
{
	(void)EventMessage;

	if (!bIsDialogueSessionActive || ConversationHistory.IsEmpty())
	{
		return;
	}

	FLLMMessage& FirstMessage = ConversationHistory[0];
	if (FirstMessage.Role.Equals(TEXT("system"), ESearchCase::IgnoreCase))
	{
		FirstMessage.Content = BuildSystemPrompt();
	}
}

#undef AINPC_WITH_SMARTOBJECTS
