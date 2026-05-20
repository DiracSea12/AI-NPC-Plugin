// RetryAttempt TimeoutFallback
#include "Components/AINpcDialogueLifecycleHandler.h"
#include "Components/AINpcComponent.h"
#include "Components/AINpcDialogueRequestBuilder.h"
#include "AINpcCoreLog.h"
#include "Async/Async.h"
#include "LLM/ILLMProvider.h"
#include "LLM/LLMConcurrencyManager.h"
#include "LLM/LLMReliabilityManager.h"
#include "Settings/AINpcSettings.h"
#include "TimerManager.h"
#include "Engine/World.h"

namespace
{
#if !UE_BUILD_SHIPPING
	bool GBypassDialogueRequestDispatchForTests = false;
#endif
}

bool FAINpcDialogueLifecycleHandler::StartDialogue(UAINpcComponent& Component, const FString& PlayerInput)
{
	const FString TrimmedInput = PlayerInput.TrimStartAndEnd();
	if (TrimmedInput.IsEmpty())
	{
		BroadcastError(Component, TEXT("Player input is empty."));
		return false;
	}

	if (Component.bIsRequestInFlight || Component.QueuedDialogueRequestToken != 0)
	{
		BroadcastError(Component, TEXT("A dialogue request is already pending."));
		return false;
	}

	Component.EnsureNpcControllerAndStateTreeBinding();

	Component.EnsureProvider();
	if (!Component.Provider.IsValid())
	{
		BroadcastError(Component, TEXT("Dialogue provider is unavailable."));
		return false;
	}

	const bool bStartNewSession = !Component.bIsDialogueSessionActive;
	const int32 HistoryCountBeforeStart = Component.ConversationHistory.Num();
	if (bStartNewSession)
	{
		Component.bIsDialogueSessionActive = true;
		Component.ConversationHistory.Reset();

		FLLMMessage SystemMessage;
		SystemMessage.Role = TEXT("system");
		SystemMessage.Content = Component.BuildSystemPrompt();
		Component.ConversationHistory.Add(MoveTemp(SystemMessage));
	}

	FLLMMessage UserMessage;
	UserMessage.Role = TEXT("user");
	UserMessage.Content = TrimmedInput;
	Component.ConversationHistory.Add(MoveTemp(UserMessage));
	Component.LastParsedResponse = FParsedLLMResponse();
	Component.RetryAttemptCount = 0;
	Component.CumulativeRetryTimeSeconds = 0.0f;
	ClearRetryTimer(Component);

	if (!DispatchDialogueRequest(Component))
	{
		BroadcastError(Component, TEXT("Failed to dispatch dialogue request."));
		Component.ClearDelayMaskingTimer();
		Component.EndDelayMasking();
		Component.ConversationHistory.SetNum(HistoryCountBeforeStart, EAllowShrinking::No);
		if (bStartNewSession)
		{
			Component.bIsDialogueSessionActive = false;
		}
		Component.SetDialogueState(ENpcDialogueState::Idle);
		return false;
	}

	if (bStartNewSession)
	{
		Component.OnDialogueSessionStarted.Broadcast();
		Component.DialogueSessionStartedNative.Broadcast();
	}

	return true;
}

void FAINpcDialogueLifecycleHandler::EndDialogue(UAINpcComponent& Component)
{
	const bool bWasSessionActive = Component.bIsDialogueSessionActive;
	Component.bIsDialogueSessionActive = false;
	ClearRetryTimer(Component);
	Component.RetryAttemptCount = 0;
	Component.CumulativeRetryTimeSeconds = 0.0f;
	CancelQueuedDialogueRequest(Component);

	if (Component.bIsRequestInFlight && Component.Provider.IsValid() && Component.ActiveRequestId.IsValid())
	{
		Component.Provider->CancelRequest(Component.ActiveRequestId);
	}

	Component.ClearDelayMaskingTimer();
	Component.EndDelayMasking();
	ClearActiveRequest(Component);
	Component.ConversationHistory.Reset();
	Component.LastParsedResponse = FParsedLLMResponse();
	Component.SetDialogueState(ENpcDialogueState::Idle);

	if (bWasSessionActive)
	{
		Component.OnDialogueSessionEnded.Broadcast();
		Component.DialogueSessionEndedNative.Broadcast();
	}
}

bool FAINpcDialogueLifecycleHandler::DispatchDialogueRequest(UAINpcComponent& Component)
{
	check(IsInGameThread());
	if (Component.bIsRequestInFlight || Component.bOwnsDialogueDispatchSlot || Component.QueuedDialogueRequestToken != 0)
	{
		return false;
	}

	uint64 QueueToken = 0;
	if (!FLLMConcurrencyManager::Get().TryAcquireDialogueSlot(&Component, QueueToken))
	{
		Component.QueuedDialogueRequestToken = QueueToken;
		Component.SetDialogueState(ENpcDialogueState::WaitingForLLM);
		return true;
	}

	Component.bOwnsDialogueDispatchSlot = true;
	if (DispatchDialogueRequestNow(Component))
	{
		return true;
	}

	ReleaseDialogueDispatchSlot(Component);
	return false;
}

bool FAINpcDialogueLifecycleHandler::DispatchDialogueRequestNow(UAINpcComponent& Component)
{
#if !UE_BUILD_SHIPPING
	if (GBypassDialogueRequestDispatchForTests)
	{
		Component.ActiveRequestId = FGuid::NewGuid();
		Component.bIsRequestInFlight = true;
		Component.SetDialogueState(ENpcDialogueState::WaitingForLLM);
		Component.ScheduleDelayMasking();
		return true;
	}
#endif

	Component.EnsureProvider();
	if (!Component.Provider.IsValid())
	{
		return false;
	}

	FLLMRequest Request = Component.BuildRequest();
	FAINpcDialogueRequestBuilder::ConfigureStreamingRequest(Component, Request);
	Component.ActiveRequestId = Component.Provider->SendRequest(
		Request,
		[WeakThis = TWeakObjectPtr<UAINpcComponent>(&Component)](const FLLMResponse& Response)
		{
			AsyncTask(ENamedThreads::GameThread, [WeakThis, Response]()
			{
				if (UAINpcComponent* Comp = WeakThis.Get())
				{
					FAINpcDialogueLifecycleHandler::HandleRequestCompleted(*Comp, Response);
				}
			});
		});

	Component.bIsRequestInFlight = Component.ActiveRequestId.IsValid();
	if (!Component.bIsRequestInFlight)
	{
		return false;
	}

	Component.SetDialogueState(ENpcDialogueState::WaitingForLLM);
	Component.ScheduleDelayMasking();
	return true;
}

bool FAINpcDialogueLifecycleHandler::TryDispatchQueuedDialogueRequest(UAINpcComponent& Component, const uint64 QueueToken)
{
	if (Component.QueuedDialogueRequestToken != QueueToken)
	{
		return false;
	}

	Component.QueuedDialogueRequestToken = 0;

	if (!Component.bIsDialogueSessionActive || Component.bIsRequestInFlight)
	{
		return false;
	}

	if (!DispatchDialogueRequestNow(Component))
	{
		Component.SetDialogueState(ENpcDialogueState::Idle);
		BroadcastError(Component, TEXT("Failed to dispatch queued dialogue request."));
		return false;
	}

	Component.bOwnsDialogueDispatchSlot = true;
	return true;
}

void FAINpcDialogueLifecycleHandler::CancelQueuedDialogueRequest(UAINpcComponent& Component)
{
	if (Component.QueuedDialogueRequestToken == 0)
	{
		return;
	}

	const uint64 QueueTokenToRemove = Component.QueuedDialogueRequestToken;
	Component.QueuedDialogueRequestToken = 0;
	FLLMConcurrencyManager::Get().CancelQueuedDialogueRequest(&Component, QueueTokenToRemove);
}

void FAINpcDialogueLifecycleHandler::ReleaseDialogueDispatchSlot(UAINpcComponent& Component)
{
	check(IsInGameThread());
	if (!Component.bOwnsDialogueDispatchSlot)
	{
		return;
	}

	Component.bOwnsDialogueDispatchSlot = false;
	FLLMConcurrencyManager::Get().ReleaseDialogueSlot();
}

void FAINpcDialogueLifecycleHandler::PumpQueuedDialogueRequests()
{
	FLLMConcurrencyManager::Get().PumpDialogueQueue();
}

void FAINpcDialogueLifecycleHandler::ClearActiveRequest(UAINpcComponent& Component)
{
	Component.bIsRequestInFlight = false;
	Component.ActiveRequestId.Invalidate();
	Component.ClearDelayMaskingTimer();
	ReleaseDialogueDispatchSlot(Component);
}

void FAINpcDialogueLifecycleHandler::HandleRequestCompleted(UAINpcComponent& Component, const FLLMResponse& Response)
{
	if (!Component.bIsDialogueSessionActive)
	{
		return;
	}

	if (!Component.bIsRequestInFlight || Component.ActiveRequestId != Response.RequestId)
	{
		return;
	}

	ClearRetryTimer(Component);
	Component.ClearDelayMaskingTimer();
	Component.EndDelayMasking();

	if (Response.bSuccess)
	{
		ClearActiveRequest(Component);
		Component.RetryAttemptCount = 0;
		Component.CumulativeRetryTimeSeconds = 0.0f;
		Component.SetDialogueState(ENpcDialogueState::Speaking);
		Component.LastParsedResponse = Response.ParsedResponse;

		FLLMMessage AssistantMessage;
		AssistantMessage.Role = TEXT("assistant");
		AssistantMessage.Content = Response.Content;
		Component.ConversationHistory.Add(MoveTemp(AssistantMessage));

		Component.OnDialogueResponse.Broadcast(Response.Content);
		Component.DialogueResponseNative.Broadcast(Response.Content);
		return;
	}

	const UAINpcSettings* Settings = GetDefault<UAINpcSettings>();
	const int32 MaxRetryAttempts = FLLMReliabilityManager::GetMaxRetryAttempts(Settings);
	if (FLLMReliabilityManager::IsRetryableFailure(Response) && Component.RetryAttemptCount < MaxRetryAttempts)
	{
		const float RetryDelaySeconds = FLLMReliabilityManager::GetRetryDelaySeconds(Settings, Component.RetryAttemptCount);
		const float MaxTotalRetryTime = Settings ? Settings->MaxTotalRetryTimeSeconds : 30.0f;

		if (Component.CumulativeRetryTimeSeconds + RetryDelaySeconds > MaxTotalRetryTime)
		{
			UE_LOG(LogAINpc, Warning, TEXT("Total retry time budget exceeded (%.2fs + %.2fs > %.2fs), aborting retries"),
				Component.CumulativeRetryTimeSeconds, RetryDelaySeconds, MaxTotalRetryTime);
		}
		else
		{
			ClearActiveRequest(Component);
			Component.CumulativeRetryTimeSeconds += RetryDelaySeconds;
			++Component.RetryAttemptCount;
			UE_LOG(LogAINpc, Log, TEXT("Retry attempt %d with exponential backoff delay %.2fs (cumulative: %.2fs)"),
				Component.RetryAttemptCount, RetryDelaySeconds, Component.CumulativeRetryTimeSeconds);
			ScheduleRetryRequest(Component, RetryDelaySeconds);
			return;
		}
	}

	ClearActiveRequest(Component);
	Component.RetryAttemptCount = 0;
	Component.CumulativeRetryTimeSeconds = 0.0f;
	Component.LastParsedResponse = FParsedLLMResponse();
	if (Component.TryHandleFailureWithFallback(Response))
	{
		return;
	}

	const FString ErrorText = Response.ErrorMessage.IsEmpty()
		? TEXT("Dialogue request failed.")
		: Response.ErrorMessage;
	Component.SetDialogueState(ENpcDialogueState::Idle);
	BroadcastError(Component, ErrorText);
}

void FAINpcDialogueLifecycleHandler::HandleStateTreeTimeoutFailure(UAINpcComponent& Component)
{
	if (!Component.bIsDialogueSessionActive)
	{
		return;
	}

	FLLMResponse TimeoutResponse;
	TimeoutResponse.bSuccess = false;
	TimeoutResponse.RequestId = Component.ActiveRequestId;
	TimeoutResponse.HttpStatusCode = 408;
	TimeoutResponse.ErrorMessage = TEXT("Dialogue request timed out before callback was processed.");

	ClearRetryTimer(Component);
	Component.ClearDelayMaskingTimer();
	Component.EndDelayMasking();
	CancelQueuedDialogueRequest(Component);

	if (Component.bIsRequestInFlight && Component.Provider.IsValid() && Component.ActiveRequestId.IsValid())
	{
		Component.Provider->CancelRequest(Component.ActiveRequestId);
	}

	ClearActiveRequest(Component);
	Component.RetryAttemptCount = 0;
	Component.CumulativeRetryTimeSeconds = 0.0f;
	Component.LastParsedResponse = FParsedLLMResponse();
	if (Component.TryHandleFailureWithFallback(TimeoutResponse))
	{
		return;
	}

	Component.SetDialogueState(ENpcDialogueState::Idle);
	BroadcastError(Component, TimeoutResponse.ErrorMessage);
}

void FAINpcDialogueLifecycleHandler::HandleRetryRequestTimerElapsed(UAINpcComponent& Component)
{
	if (!Component.bIsDialogueSessionActive || Component.bIsRequestInFlight)
	{
		return;
	}

	Component.EnsureProvider();
	if (!Component.Provider.IsValid())
	{
		FLLMResponse FailureResponse;
		FailureResponse.bSuccess = false;
		FailureResponse.ErrorMessage = TEXT("Dialogue provider is unavailable.");
		if (!Component.TryHandleFailureWithFallback(FailureResponse))
		{
			Component.SetDialogueState(ENpcDialogueState::Idle);
			BroadcastError(Component, FailureResponse.ErrorMessage);
		}
		return;
	}

	if (!DispatchDialogueRequest(Component))
	{
		FLLMResponse FailureResponse;
		FailureResponse.bSuccess = false;
		FailureResponse.ErrorMessage = TEXT("Failed to dispatch dialogue retry request.");
		if (!Component.TryHandleFailureWithFallback(FailureResponse))
		{
			Component.SetDialogueState(ENpcDialogueState::Idle);
			BroadcastError(Component, FailureResponse.ErrorMessage);
		}
	}
}

void FAINpcDialogueLifecycleHandler::ScheduleRetryRequest(UAINpcComponent& Component, const float DelaySeconds)
{
	if (!Component.bIsDialogueSessionActive)
	{
		return;
	}

	ClearRetryTimer(Component);
	if (UWorld* World = Component.GetWorld())
	{
		World->GetTimerManager().SetTimer(
			Component.RetryTimerHandle,
			&Component,
			&UAINpcComponent::HandleRetryRequestTimerElapsed,
			FMath::Max(0.0f, DelaySeconds),
			false);
	}
}

void FAINpcDialogueLifecycleHandler::ClearRetryTimer(UAINpcComponent& Component)
{
	if (UWorld* World = Component.GetWorld())
	{
		World->GetTimerManager().ClearTimer(Component.RetryTimerHandle);
	}

	Component.RetryTimerHandle.Invalidate();
}

void FAINpcDialogueLifecycleHandler::BroadcastError(UAINpcComponent& Component, const FString& ErrorMessage)
{
	Component.OnDialogueError.Broadcast(ErrorMessage);
	Component.DialogueErrorNative.Broadcast(ErrorMessage);
}

#if !UE_BUILD_SHIPPING
void FAINpcDialogueLifecycleHandler::SetDispatchBypassForTest(const bool bBypass)
{
	GBypassDialogueRequestDispatchForTests = bBypass;
}
#endif
