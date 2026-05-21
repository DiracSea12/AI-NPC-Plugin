#include "LLM/LLMProviderChain.h"

#include "Async/Async.h"
#include "Misc/ScopeLock.h"
#include "LLM/LLMResponseParser.h"
#include "Events/NpcEventSubsystem.h"
#include "Events/NpcEventPayloadTypes.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"

namespace
{
FLLMRequest BuildChainStreamingRequest(
	const FLLMRequest& Request,
	const FGuid& ChainRequestId,
	const TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe>& bStreamOpen,
	TFunction<bool(const TOptional<FGuid>&)> ShouldForwardChunk)
{
	FLLMRequest WrappedRequest = Request;
	if (!Request.StreamCallback)
	{
		return WrappedRequest;
	}

	const FLLMStreamCallback OriginalStreamCallback = Request.StreamCallback;
	const TSharedRef<TOptional<FGuid>, ESPMode::ThreadSafe> ProviderRequestId =
		MakeShared<TOptional<FGuid>, ESPMode::ThreadSafe>();
	WrappedRequest.StreamCallback =
		[ChainRequestId, OriginalStreamCallback, ProviderRequestId, bStreamOpen, ShouldForwardChunk = MoveTemp(ShouldForwardChunk)](const FLLMStreamChunk& Chunk)
		{
			if (!bStreamOpen->Load())
			{
				return;
			}

			if (!Chunk.RequestId.IsValid())
			{
				return;
			}

			if (!ProviderRequestId->IsSet())
			{
				*ProviderRequestId = Chunk.RequestId;
			}

			if (ProviderRequestId->IsSet() && Chunk.RequestId != ProviderRequestId->GetValue())
			{
				return;
			}

			if (ShouldForwardChunk && !ShouldForwardChunk(*ProviderRequestId))
			{
				return;
			}
			FLLMStreamChunk ChainChunk = Chunk;
			ChainChunk.RequestId = ChainRequestId;
			OriginalStreamCallback(ChainChunk);
		};

	return WrappedRequest;
}
}

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
	const TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe> bPrimaryStreamOpen =
		MakeShared<TAtomic<bool>, ESPMode::ThreadSafe>(true);
	const TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe> bPrimaryRequestIdRecorded =
		MakeShared<TAtomic<bool>, ESPMode::ThreadSafe>(false);
	const FLLMRequest PrimaryRequest = BuildChainStreamingRequest(
		Request,
		RequestId,
		bPrimaryStreamOpen,
		[WeakChain, RequestId, bPrimaryRequestIdRecorded](const TOptional<FGuid>& ProviderRequestId)
		{
			const TSharedPtr<FLLMProviderChain, ESPMode::ThreadSafe> Chain = WeakChain.Pin();
			if (!Chain.IsValid())
			{
				return false;
			}

			if (!bPrimaryRequestIdRecorded->Load())
			{
				return true;
			}

			FScopeLock Lock(&Chain->RequestMutex);
			const FGuid* ActiveProviderRequestId = Chain->ActivePrimaryRequests.Find(RequestId);
			return ActiveProviderRequestId &&
				ProviderRequestId.IsSet() &&
				*ActiveProviderRequestId == ProviderRequestId.GetValue();
		});
	const TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe> bCompletedBeforeRequestIdWasRecorded =
		MakeShared<TAtomic<bool>, ESPMode::ThreadSafe>(false);

	const FGuid PrimaryRequestId = PrimaryProvider->SendRequest(PrimaryRequest,
		[WeakChain, RequestId, RequestCopy, CompletionCallback = MoveTemp(CompletionCallback), RetryCount, bCompletedBeforeRequestIdWasRecorded, bPrimaryStreamOpen](const FLLMResponse& Response) mutable
		{
			bPrimaryStreamOpen->Store(false);
			bCompletedBeforeRequestIdWasRecorded->Store(true);
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
		if (!bCompletedBeforeRequestIdWasRecorded->Load())
		{
			ActivePrimaryRequests.Add(RequestId, PrimaryRequestId);
			bPrimaryRequestIdRecorded->Store(true);
		}
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
	const TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe> bFallbackStreamOpen =
		MakeShared<TAtomic<bool>, ESPMode::ThreadSafe>(true);
	const TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe> bFallbackRequestIdRecorded =
		MakeShared<TAtomic<bool>, ESPMode::ThreadSafe>(false);
	const FLLMRequest FallbackRequest = BuildChainStreamingRequest(
		Request,
		RequestId,
		bFallbackStreamOpen,
		[WeakChain, RequestId, bFallbackRequestIdRecorded](const TOptional<FGuid>& ProviderRequestId)
		{
			const TSharedPtr<FLLMProviderChain, ESPMode::ThreadSafe> Chain = WeakChain.Pin();
			if (!Chain.IsValid())
			{
				return false;
			}

			if (!bFallbackRequestIdRecorded->Load())
			{
				return true;
			}

			FScopeLock Lock(&Chain->RequestMutex);
			const FGuid* ActiveProviderRequestId = Chain->ActiveFallbackRequests.Find(RequestId);
			return ActiveProviderRequestId &&
				ProviderRequestId.IsSet() &&
				*ActiveProviderRequestId == ProviderRequestId.GetValue();
		});
	const TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe> bCompletedBeforeRequestIdWasRecorded =
		MakeShared<TAtomic<bool>, ESPMode::ThreadSafe>(false);

	const FGuid FallbackRequestId = FallbackProvider->SendRequest(FallbackRequest,
		[WeakChain, RequestId, CompletionCallback = MoveTemp(CompletionCallback), RetryCount, bCompletedBeforeRequestIdWasRecorded, bFallbackStreamOpen](const FLLMResponse& Response) mutable
		{
			bFallbackStreamOpen->Store(false);
			bCompletedBeforeRequestIdWasRecorded->Store(true);
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
		if (!bCompletedBeforeRequestIdWasRecorded->Load())
		{
			ActiveFallbackRequests.Add(RequestId, FallbackRequestId);
			bFallbackRequestIdRecorded->Store(true);
		}
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
		Response.ParsedResponse.ParseTier = ELLMResponseParseTier::PlainText;
		Response.FallbackReason = ELLMFallbackReason::FallbackFailed;
	}
	else
	{
		Response.bSuccess = false;
		Response.Content = TEXT("");
		Response.ErrorMessage = TEXT("LLM providers failed and no fallback template is configured.");
		Response.FallbackReason = ELLMFallbackReason::NoTemplateAvailable;
	}

	BroadcastDegradationEvent(
		bHasTemplate
			? TEXT("LLM providers failed, using template response")
			: TEXT("LLM providers failed and no fallback template is configured"),
		RetryCount,
		bHasTemplate);

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
	EventMessage.EventTag = FGameplayTag::RequestGameplayTag(TEXT("NPC.Event.LLMDegradation"), false);
	EventMessage.Payload.InitializeAs<FNpcLLMDegradationEventPayload>(Payload);

	EventSubsystem->BroadcastEvent(EventMessage);
}
