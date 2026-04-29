#include "LLM/LLMProviderChain.h"

#include "Async/Async.h"
#include "Misc/ScopeLock.h"
#include "LLM/LLMResponseParser.h"
#include "Events/NpcEventSubsystem.h"
#include "Events/NpcEventPayloadTypes.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"

FLLMProviderChain::FLLMProviderChain(
	TSharedPtr<ILLMProvider> InPrimaryProvider,
	TSharedPtr<ILLMProvider> InFallbackProvider,
	TArray<FString> InFallbackResponses,
	int32 InMaxRetries,
	float InBaseRetryDelaySeconds)
	: PrimaryProvider(MoveTemp(InPrimaryProvider))
	, FallbackProvider(MoveTemp(InFallbackProvider))
	, FallbackResponses(MoveTemp(InFallbackResponses))
	, MaxRetries(InMaxRetries)
	, BaseRetryDelaySeconds(InBaseRetryDelaySeconds)
{
}

FGuid FLLMProviderChain::SendRequest(const FLLMRequest& Request, FLLMResponseCallback CompletionCallback)
{
	const FGuid ChainRequestId = FGuid::NewGuid();
	TryPrimaryWithRetry(ChainRequestId, Request, MoveTemp(CompletionCallback), 0);
	return ChainRequestId;
}

void FLLMProviderChain::TryPrimaryWithRetry(const FGuid& RequestId, const FLLMRequest& Request, FLLMResponseCallback CompletionCallback, int32 RetryCount)
{
	if (!PrimaryProvider.IsValid())
	{
		TryFallbackProvider(RequestId, Request, MoveTemp(CompletionCallback), RetryCount);
		return;
	}

	const TWeakPtr<FLLMProviderChain, ESPMode::ThreadSafe> WeakChain = AsWeak();
	const FLLMRequest RequestCopy = Request;

	const FGuid PrimaryRequestId = PrimaryProvider->SendRequest(Request,
		[WeakChain, RequestId, RequestCopy, CompletionCallback = MoveTemp(CompletionCallback), RetryCount](const FLLMResponse& Response) mutable
		{
			if (Response.bSuccess)
			{
				if (const TSharedPtr<FLLMProviderChain, ESPMode::ThreadSafe> Chain = WeakChain.Pin())
				{
					FScopeLock Lock(&Chain->RequestMutex);
					Chain->ActivePrimaryRequests.Remove(RequestId);
					Chain->ActiveFallbackRequests.Remove(RequestId);
				}

				FLLMResponse ChainResponse = Response;
				ChainResponse.RequestId = RequestId;
				if (CompletionCallback)
				{
					CompletionCallback(ChainResponse);
				}
				return;
			}

			const TSharedPtr<FLLMProviderChain, ESPMode::ThreadSafe> Chain = WeakChain.Pin();
			if (!Chain.IsValid())
			{
				if (CompletionCallback)
				{
					FLLMResponse FailResponse;
					FailResponse.RequestId = RequestId;
					FailResponse.ErrorMessage = TEXT("Provider chain no longer available.");
					CompletionCallback(FailResponse);
				}
				return;
			}

			{
				FScopeLock Lock(&Chain->RequestMutex);
				Chain->ActivePrimaryRequests.Remove(RequestId);
			}

			if (RetryCount < Chain->MaxRetries)
			{
				const float DelaySeconds = Chain->BaseRetryDelaySeconds * FMath::Pow(2.0f, RetryCount);
				Async(EAsyncExecution::ThreadPool, [WeakChain, RequestId, RequestCopy, CompletionCallback = MoveTemp(CompletionCallback), RetryCount, DelaySeconds]() mutable
				{
					FPlatformProcess::SleepNoStats(FMath::Max(0.0f, DelaySeconds));
					AsyncTask(ENamedThreads::GameThread, [WeakChain, RequestId, RequestCopy, CompletionCallback = MoveTemp(CompletionCallback), RetryCount]() mutable
					{
						if (const TSharedPtr<FLLMProviderChain, ESPMode::ThreadSafe> ChainPtr = WeakChain.Pin())
						{
							ChainPtr->TryPrimaryWithRetry(RequestId, RequestCopy, MoveTemp(CompletionCallback), RetryCount + 1);
						}
					});
				});
			}
			else
			{
				Chain->TryFallbackProvider(RequestId, RequestCopy, MoveTemp(CompletionCallback), RetryCount);
			}
		});

	{
		FScopeLock Lock(&RequestMutex);
		ActivePrimaryRequests.Add(RequestId, PrimaryRequestId);
	}
}

bool FLLMProviderChain::CancelRequest(const FGuid& RequestId)
{
	FScopeLock Lock(&RequestMutex);

	if (const FGuid* PrimaryRequestId = ActivePrimaryRequests.Find(RequestId))
	{
		ActivePrimaryRequests.Remove(RequestId);
		if (PrimaryProvider.IsValid())
		{
			return PrimaryProvider->CancelRequest(*PrimaryRequestId);
		}
	}

	if (const FGuid* FallbackRequestId = ActiveFallbackRequests.Find(RequestId))
	{
		ActiveFallbackRequests.Remove(RequestId);
		if (FallbackProvider.IsValid())
		{
			return FallbackProvider->CancelRequest(*FallbackRequestId);
		}
	}

	return false;
}

FLLMProviderCapabilities FLLMProviderChain::GetCapabilities() const
{
	if (PrimaryProvider.IsValid())
	{
		return PrimaryProvider->GetCapabilities();
	}

	if (FallbackProvider.IsValid())
	{
		return FallbackProvider->GetCapabilities();
	}

	FLLMProviderCapabilities Capabilities;
	Capabilities.bSupportsStreaming = false;
	Capabilities.bSupportsFunctionCalling = false;
	Capabilities.bSupportsJsonMode = false;
	return Capabilities;
}

void FLLMProviderChain::TryFallbackProvider(const FGuid& RequestId, const FLLMRequest& Request, FLLMResponseCallback CompletionCallback, int32 RetryCount)
{
	if (!FallbackProvider.IsValid())
	{
		UseFallbackTemplate(RequestId, MoveTemp(CompletionCallback), RetryCount);
		return;
	}

	const TWeakPtr<FLLMProviderChain, ESPMode::ThreadSafe> WeakChain = AsWeak();

	const FGuid FallbackRequestId = FallbackProvider->SendRequest(Request,
		[WeakChain, RequestId, CompletionCallback = MoveTemp(CompletionCallback), RetryCount](const FLLMResponse& Response) mutable
		{
			if (Response.bSuccess)
			{
				if (const TSharedPtr<FLLMProviderChain, ESPMode::ThreadSafe> Chain = WeakChain.Pin())
				{
					FScopeLock Lock(&Chain->RequestMutex);
					Chain->ActivePrimaryRequests.Remove(RequestId);
					Chain->ActiveFallbackRequests.Remove(RequestId);
				}

				FLLMResponse ChainResponse = Response;
				ChainResponse.RequestId = RequestId;

				if (CompletionCallback)
				{
					CompletionCallback(ChainResponse);
				}
				return;
			}

			const TSharedPtr<FLLMProviderChain, ESPMode::ThreadSafe> Chain = WeakChain.Pin();
			if (!Chain.IsValid())
			{
				if (CompletionCallback)
				{
					FLLMResponse FailResponse;
					FailResponse.RequestId = RequestId;
					FailResponse.ErrorMessage = TEXT("Provider chain no longer available.");
					CompletionCallback(FailResponse);
				}
				return;
			}

			{
				FScopeLock Lock(&Chain->RequestMutex);
				Chain->ActiveFallbackRequests.Remove(RequestId);
			}

			Chain->UseFallbackTemplate(RequestId, MoveTemp(CompletionCallback), RetryCount);
		});

	{
		FScopeLock Lock(&RequestMutex);
		ActiveFallbackRequests.Add(RequestId, FallbackRequestId);
	}
}

void FLLMProviderChain::UseFallbackTemplate(const FGuid& RequestId, FLLMResponseCallback CompletionCallback, int32 RetryCount)
{
	FLLMResponse Response;
	Response.RequestId = RequestId;
	Response.bIsFallback = true;

	const bool bHasTemplate = FallbackResponses.Num() > 0;
	if (bHasTemplate)
	{
		const int32 RandomIndex = FMath::RandRange(0, FallbackResponses.Num() - 1);
		Response.bSuccess = true;
		Response.Content = FallbackResponses[RandomIndex];
		Response.ParsedResponse.Dialogue = Response.Content;
		Response.ParsedResponse.Actions.Add(FNpcAction());
		Response.ParsedResponse.Actions[0].ActionType = TEXT("Action.DefaultTalk");
		Response.ParsedResponse.ParseTier = ELLMResponseParseTier::PlainText;
		Response.FallbackReason = ELLMFallbackReason::FallbackFailed;
	}
	else
	{
		Response.bSuccess = true;
		Response.Content = TEXT("");
		Response.FallbackReason = ELLMFallbackReason::NoTemplateAvailable;
	}

	BroadcastDegradationEvent(TEXT("LLM providers failed, using template response"), RetryCount, bHasTemplate);

	Async(EAsyncExecution::ThreadPool, [CompletionCallback = MoveTemp(CompletionCallback), Response = MoveTemp(Response)]() mutable
	{
		if (CompletionCallback)
		{
			CompletionCallback(Response);
		}
	});
}

void FLLMProviderChain::BroadcastDegradationEvent(const FString& Reason, int32 RetryCount, bool bUsedTemplate)
{
	if (!NpcActor.IsValid())
	{
		return;
	}

	UWorld* World = NpcActor->GetWorld();
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

	FNpcLLMDegradationEventPayload Payload;
	Payload.NpcActor = NpcActor.Get();
	Payload.Reason = Reason;
	Payload.RetryCount = RetryCount;
	Payload.bUsedTemplate = bUsedTemplate;

	FNpcEventMessage EventMessage;
	EventMessage.EventTag = FGameplayTag::RequestGameplayTag(TEXT("NPC.Event.LLMDegradation"));
	EventMessage.Payload.InitializeAs<FNpcLLMDegradationEventPayload>(Payload);

	EventSubsystem->BroadcastEvent(EventMessage);
}
