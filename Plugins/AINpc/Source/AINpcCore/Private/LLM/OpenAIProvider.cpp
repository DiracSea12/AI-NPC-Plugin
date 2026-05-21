#include "LLM/OpenAIProvider.h"
#include "LLM/AINpcLLMDiagnostics.h"
#include "LLM/StructuredOutputPromptLibrary.h"
#include "LLM/StructuredOutputSchemaHelpers.h"
#include "LLM/SSEParser.h"
#include "AINpcCoreLog.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

using namespace StructuredOutputSchemaHelpers;

#if WITH_EDITOR
TAtomic<float> FOpenAIProvider::PreDispatchDelaySecondsForTest(0.0f);
TAtomic<float> FOpenAIProvider::PreProcessDelaySecondsForTest(0.0f);
TAtomic<bool> FOpenAIProvider::bReachedPostInitialCancelGateForTest(false);
#endif

struct FOpenAIStreamingState
{
	explicit FOpenAIStreamingState(const FGuid& InRequestId, FLLMStreamCallback InStreamCallback)
		: RequestId(InRequestId)
		, StreamCallback(MoveTemp(InStreamCallback))
	{
		Parser.OnData.BindRaw(this, &FOpenAIStreamingState::HandleData);
		Parser.OnDone.BindRaw(this, &FOpenAIStreamingState::HandleDone);
		Parser.OnError.BindRaw(this, &FOpenAIStreamingState::HandleError);
	}

	void AppendBytes(const void* Data, int64 Length)
	{
		if (!Data || Length <= 0)
		{
			return;
		}

		FScopeLock Lock(&Mutex);
		const FUTF8ToTCHAR ConvertedBytes(reinterpret_cast<const ANSICHAR*>(Data), static_cast<int32>(Length));
		const FString ChunkString(ConvertedBytes.Length(), ConvertedBytes.Get());
		RawResponse += ChunkString;
		Parser.ProcessChunk(ChunkString);
	}

	FString GetResponseBody() const
	{
		FScopeLock Lock(&Mutex);
		return RawResponse;
	}

	FString GetErrorMessage() const
	{
		FScopeLock Lock(&Mutex);
		return StreamErrorMessage;
	}

	FString GetAccumulatedContent() const
	{
		FScopeLock Lock(&Mutex);
		return AccumulatedContent;
	}

private:
	void HandleData(const FString& Data)
	{
		TSharedPtr<FJsonObject> JsonObject;
		const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(Data);
		if (!FJsonSerializer::Deserialize(JsonReader, JsonObject) || !JsonObject.IsValid())
		{
			HandleError(FString::Printf(TEXT("OpenAI stream chunk JSON parse failed: %s"), *Data.Left(256)));
			return;
		}

		const TSharedPtr<FJsonObject>* ErrorObject = nullptr;
		if (JsonObject->TryGetObjectField(TEXT("error"), ErrorObject) && ErrorObject && ErrorObject->IsValid())
		{
			FString Message;
			if (!(*ErrorObject)->TryGetStringField(TEXT("message"), Message) || Message.IsEmpty())
			{
				Message = TEXT("OpenAI stream returned an error object.");
			}
			HandleError(Message);
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* ChoicesArray = nullptr;
		if (!JsonObject->TryGetArrayField(TEXT("choices"), ChoicesArray) || !ChoicesArray || ChoicesArray->Num() == 0)
		{
			return;
		}

		const TSharedPtr<FJsonObject>* ChoiceObject = nullptr;
		if (!(*ChoicesArray)[0]->TryGetObject(ChoiceObject) || !ChoiceObject || !ChoiceObject->IsValid())
		{
			return;
		}

		const TSharedPtr<FJsonObject>* DeltaObject = nullptr;
		if ((*ChoiceObject)->TryGetObjectField(TEXT("delta"), DeltaObject) && DeltaObject && DeltaObject->IsValid())
		{
			FString Content;
			if ((*DeltaObject)->TryGetStringField(TEXT("content"), Content) && !Content.IsEmpty())
			{
				AccumulatedContent += Content;
				FLLMStreamChunk Chunk;
				Chunk.RequestId = RequestId;
				Chunk.Content = Content;
				StreamCallback(Chunk);
			}
		}

		FString FinishReason;
		if ((*ChoiceObject)->TryGetStringField(TEXT("finish_reason"), FinishReason) && !FinishReason.IsEmpty())
		{
			HandleDone();
		}
	}

	void HandleDone()
	{
		if (bFinalSent)
		{
			return;
		}
		bFinalSent = true;

		FLLMStreamChunk FinalChunk;
		FinalChunk.RequestId = RequestId;
		FinalChunk.Content = AccumulatedContent;
		FinalChunk.bIsFinal = true;
		StreamCallback(FinalChunk);
	}

	void HandleError(const FString& Error)
	{
		if (bErrorSent)
		{
			return;
		}
		bErrorSent = true;
		StreamErrorMessage = Error.IsEmpty() ? TEXT("OpenAI stream error.") : Error;

		FLLMStreamChunk ErrorChunk;
		ErrorChunk.RequestId = RequestId;
		ErrorChunk.ErrorMessage = StreamErrorMessage;
		ErrorChunk.bIsError = true;
		ErrorChunk.bIsFinal = true;
		StreamCallback(ErrorChunk);
	}

	FGuid RequestId;
	FLLMStreamCallback StreamCallback;
	FSSEParser Parser;
	FString RawResponse;
	FString AccumulatedContent;
	FString StreamErrorMessage;
	bool bFinalSent = false;
	bool bErrorSent = false;
	mutable FCriticalSection Mutex;
};

FOpenAIProvider::FOpenAIProvider(FString InDefaultApiKey, FString InDefaultModel, FString InBaseUrl)
	: DefaultApiKey(MoveTemp(InDefaultApiKey))
	, DefaultModel(MoveTemp(InDefaultModel))
	, DefaultBaseUrl(MoveTemp(InBaseUrl))
{
}

FGuid FOpenAIProvider::SendRequest(const FLLMRequest& Request, FLLMResponseCallback CompletionCallback)
{
	const FGuid RequestId = FGuid::NewGuid();
	const TWeakPtr<FOpenAIProvider, ESPMode::ThreadSafe> WeakProvider = AsShared();
	const FLLMRequest RequestCopy = Request;

	{
		FScopeLock Lock(&ActiveRequestsMutex);
		PendingRequests.Add(RequestId);
	}

	Async(EAsyncExecution::ThreadPool, [WeakProvider, RequestId, RequestCopy, CompletionCallback = MoveTemp(CompletionCallback)]() mutable
	{
		if (const TSharedPtr<FOpenAIProvider, ESPMode::ThreadSafe> Provider = WeakProvider.Pin())
		{
			Provider->DispatchRequest(RequestId, RequestCopy, MoveTemp(CompletionCallback), 0);
			return;
		}

		if (CompletionCallback)
		{
			FLLMResponse Response;
			Response.RequestId = RequestId;
			Response.ErrorMessage = TEXT("Provider no longer available.");
			CompletionCallback(Response);
		}
	});

	return RequestId;
}

bool FOpenAIProvider::CancelRequest(const FGuid& RequestId)
{
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> RequestToCancel;
	bool bCancelled = false;

	{
		FScopeLock Lock(&ActiveRequestsMutex);
		if (TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>* Found = ActiveRequests.Find(RequestId))
		{
			RequestToCancel = *Found;
			ActiveRequests.Remove(RequestId);
			PendingRequests.Remove(RequestId);
			CancelledPendingRequests.Remove(RequestId);
			bCancelled = true;
		}
		else if (PendingRequests.Contains(RequestId))
		{
			CancelledPendingRequests.Add(RequestId);
			bCancelled = true;
		}
	}

	if (RequestToCancel.IsValid())
	{
		RequestToCancel->CancelRequest();
	}

	return bCancelled;
}

FLLMProviderCapabilities FOpenAIProvider::GetCapabilities() const
{
	FLLMProviderCapabilities Capabilities;
	Capabilities.bSupportsStreaming = true;
	Capabilities.bSupportsFunctionCalling = true;
	Capabilities.bSupportsJsonMode = true;
	return Capabilities;
}

#if WITH_EDITOR
void FOpenAIProvider::SetPreDispatchDelaySecondsForTest(const float DelaySeconds)
{
	PreDispatchDelaySecondsForTest = FMath::Max(0.0f, DelaySeconds);
}

void FOpenAIProvider::SetPreProcessDelaySecondsForTest(const float DelaySeconds)
{
	PreProcessDelaySecondsForTest = FMath::Max(0.0f, DelaySeconds);
	bReachedPostInitialCancelGateForTest = false;
}

bool FOpenAIProvider::HasReachedPostInitialCancelGateForTest()
{
	return bReachedPostInitialCancelGateForTest.Load();
}

FString FOpenAIProvider::BuildRequestBodyForTest(const FLLMRequest& Request) const
{
	FString RequestBody;
	const TSharedRef<FJsonObject> JsonPayload = BuildJsonPayload(Request);
	const TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(JsonPayload, JsonWriter);
	return RequestBody;
}
#endif

void FOpenAIProvider::DispatchRequest(const FGuid& RequestId, FLLMRequest Request, FLLMResponseCallback CompletionCallback, int32 RetryCount)
{
#if WITH_EDITOR
	bReachedPostInitialCancelGateForTest = false;

	const float DispatchDelaySeconds = PreDispatchDelaySecondsForTest.Load();
	if (DispatchDelaySeconds > 0.0f)
	{
		FPlatformProcess::SleepNoStats(DispatchDelaySeconds);
	}
#endif

	bool bCancelledBeforeInitialGate = false;
	{
		FScopeLock Lock(&ActiveRequestsMutex);
		if (CancelledPendingRequests.Remove(RequestId) > 0)
		{
			PendingRequests.Remove(RequestId);
			bCancelledBeforeInitialGate = true;
		}
	}

	if (bCancelledBeforeInitialGate)
	{
		if (CompletionCallback)
		{
			FLLMResponse Response;
			Response.RequestId = RequestId;
			Response.ErrorMessage = TEXT("OpenAI request was cancelled.");
			CompletionCallback(Response);
		}
		return;
	}

#if WITH_EDITOR
	bReachedPostInitialCancelGateForTest = true;

	const float PreProcessDelaySeconds = PreProcessDelaySecondsForTest.Load();
	if (PreProcessDelaySeconds > 0.0f)
	{
		FPlatformProcess::SleepNoStats(PreProcessDelaySeconds);
	}
#endif

	const FString ApiKey = ResolveApiKey(Request);
	if (ApiKey.IsEmpty())
	{
		{
			FScopeLock Lock(&ActiveRequestsMutex);
			PendingRequests.Remove(RequestId);
		}

		if (CompletionCallback)
		{
			FLLMResponse Response;
			Response.RequestId = RequestId;
			Response.ErrorMessage = TEXT("OpenAI API key is empty.");
			CompletionCallback(Response);
		}
		return;
	}

	const FString Model = ResolveModel(Request);
	if (Model.IsEmpty())
	{
		{
			FScopeLock Lock(&ActiveRequestsMutex);
			PendingRequests.Remove(RequestId);
		}

		if (CompletionCallback)
		{
			FLLMResponse Response;
			Response.RequestId = RequestId;
			Response.ErrorMessage = TEXT("OpenAI model is empty.");
			CompletionCallback(Response);
		}
		return;
	}

	const TSharedRef<FJsonObject> JsonPayload = BuildJsonPayload(Request);

	FString RequestBody;
	const TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(JsonPayload, JsonWriter);

	FString BaseUrl = ResolveBaseUrl(Request).TrimStartAndEnd();
	FString Endpoint = BaseUrl;
	Endpoint.RemoveFromEnd(TEXT("/"));
	Endpoint += TEXT("/chat/completions");
	const float EffectiveTimeoutSeconds = Request.TimeoutSeconds > 0.0f ? Request.TimeoutSeconds : RequestTimeoutSeconds;
	UE_LOG(LogAINpc, Log, TEXT("LLM request dispatch provider=openai requestId=%s baseUrl=%s model=%s effortLevel=%s endpoint=%s timeout=%.1f apiKey=%s retry=%d"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*BaseUrl,
		*Model,
		Request.EffortLevel.IsEmpty() ? TEXT("<empty>") : *Request.EffortLevel,
		*Endpoint,
		EffectiveTimeoutSeconds,
		ApiKey.IsEmpty() ? TEXT("missing") : TEXT("present"),
		RetryCount);
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(Endpoint); // BaseURL per-request override: uses Request.BaseUrl if not empty
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
	HttpRequest->SetContentAsString(RequestBody);
	if (Request.TimeoutSeconds > 0.0f)
	{
		HttpRequest->SetTimeout(Request.TimeoutSeconds);
	}
	else
	{
		// Use default RequestTimeoutSeconds for reliability
		HttpRequest->SetTimeout(RequestTimeoutSeconds);
	}

	const TSharedRef<FLLMResponseCallback, ESPMode::ThreadSafe> CompletionCallbackRef =
		MakeShared<FLLMResponseCallback, ESPMode::ThreadSafe>(MoveTemp(CompletionCallback));
	TSharedPtr<FLLMStreamCallback, ESPMode::ThreadSafe> StreamCallbackPtr;
	if (Request.StreamCallback)
	{
		StreamCallbackPtr = MakeShared<FLLMStreamCallback, ESPMode::ThreadSafe>(Request.StreamCallback);
	}
	TSharedPtr<FOpenAIStreamingState, ESPMode::ThreadSafe> StreamingState;
	if (Request.bUseStreaming && StreamCallbackPtr)
	{
		ConfigureStreamingReceive(RequestId, HttpRequest, StreamCallbackPtr, StreamingState);
	}

	HttpRequest->OnProcessRequestComplete().BindLambda(
		[
			WeakProvider = AsWeak(),
			RequestId,
			CompletionCallbackRef,
			StreamCallbackPtr,
			bUseStreaming = Request.bUseStreaming,
			StreamingState,
			RequestCopy = Request,
			RetryCount
		](FHttpRequestPtr RequestPtr, FHttpResponsePtr ResponsePtr, bool bRequestSucceeded)
		{
			if (const TSharedPtr<FOpenAIProvider, ESPMode::ThreadSafe> Provider = WeakProvider.Pin())
			{
				if (bUseStreaming && StreamCallbackPtr && bRequestSucceeded && ResponsePtr.IsValid() && !StreamingState.IsValid())
				{
					Provider->ProcessStreamResponse(RequestId, ResponsePtr->GetContentAsString(), *StreamCallbackPtr);
				}
				Provider->CompleteRequest(RequestId, bRequestSucceeded, ResponsePtr, RequestPtr, CompletionCallbackRef, RequestCopy, RetryCount, StreamingState);
				return;
			}

			if (!*CompletionCallbackRef)
			{
				return;
			}

			FLLMResponse Response;
			Response.RequestId = RequestId;
			Response.bSuccess = false;
			Response.HttpStatusCode = ResponsePtr.IsValid() ? ResponsePtr->GetResponseCode() : 0;

			if (!bRequestSucceeded || !ResponsePtr.IsValid())
			{
				if (RequestPtr.IsValid())
				{
					switch (RequestPtr->GetFailureReason())
					{
					case EHttpFailureReason::ConnectionError:
						Response.ErrorMessage = TEXT("OpenAI request failed due to connection error.");
						break;
					case EHttpFailureReason::Cancelled:
						Response.ErrorMessage = TEXT("OpenAI request was cancelled.");
						break;
					case EHttpFailureReason::TimedOut:
						Response.ErrorMessage = TEXT("OpenAI request timed out.");
						break;
					default:
						Response.ErrorMessage = TEXT("Provider no longer available after request dispatch.");
						break;
					}
				}
				else
				{
					Response.ErrorMessage = TEXT("Provider no longer available after request dispatch.");
				}
			}
			else
			{
				Response.ErrorMessage = TEXT("Provider no longer available after request dispatch.");
			}

			Async(EAsyncExecution::ThreadPool, [CompletionCallbackRef, Response = MoveTemp(Response)]() mutable
			{
				if (*CompletionCallbackRef)
				{
					(*CompletionCallbackRef)(Response);
				}
			});
		});

	bool bCancelledBeforeSend = false;
	{
		FScopeLock Lock(&ActiveRequestsMutex);
		if (CancelledPendingRequests.Remove(RequestId) > 0)
		{
			PendingRequests.Remove(RequestId);
			bCancelledBeforeSend = true;
		}
		else
		{
			ActiveRequests.Add(RequestId, HttpRequest);
		}
	}

	if (bCancelledBeforeSend)
	{
		if (*CompletionCallbackRef)
		{
			FLLMResponse Response;
			Response.RequestId = RequestId;
			Response.ErrorMessage = TEXT("OpenAI request was cancelled.");
			(*CompletionCallbackRef)(Response);
		}
		return;
	}

	if (!HttpRequest->ProcessRequest())
	{
		{
			FScopeLock Lock(&ActiveRequestsMutex);
			ActiveRequests.Remove(RequestId);
			PendingRequests.Remove(RequestId);
		}

		if (*CompletionCallbackRef)
		{
			FLLMResponse Response;
			Response.RequestId = RequestId;
			Response.ErrorMessage = TEXT("Failed to dispatch OpenAI HTTP request.");
			(*CompletionCallbackRef)(Response);
		}
	}
}

void FOpenAIProvider::CompleteRequest(
	const FGuid& RequestId,
	bool bRequestSucceeded,
	const FHttpResponsePtr& HttpResponse,
	const FHttpRequestPtr& HttpRequest,
	const TSharedRef<FLLMResponseCallback, ESPMode::ThreadSafe>& CompletionCallback,
	const FLLMRequest& OriginalRequest,
	int32 RetryCount,
	const TSharedPtr<FOpenAIStreamingState, ESPMode::ThreadSafe>& StreamingState)
{
	{
		FScopeLock Lock(&ActiveRequestsMutex);
		ActiveRequests.Remove(RequestId);
		PendingRequests.Remove(RequestId);
		CancelledPendingRequests.Remove(RequestId);
	}

	FLLMResponse Response;
	Response.RequestId = RequestId;
	Response.bSuccess = false;
	Response.HttpStatusCode = HttpResponse.IsValid() ? HttpResponse->GetResponseCode() : 0;

	bool bShouldRetry = false;

	const double RequestDurationMs = HttpRequest.IsValid() ? HttpRequest->GetElapsedTime() * 1000.0 : 0.0;
	FString ResponseBody;
	FString SafeResponseSummary;
	if (HttpResponse.IsValid())
	{
		ResponseBody = StreamingState.IsValid() ? StreamingState->GetResponseBody() : HttpResponse->GetContentAsString();
		SafeResponseSummary = AINpc::LLMDiagnostics::BuildSafeResponseSummary(ResponseBody);
	}

	if (!bRequestSucceeded || !HttpResponse.IsValid())
	{
		// Retry loop for MaxRequestRetries with exponential backoff
		if (RetryCount < MaxRequestRetries)
		{
			bShouldRetry = true;
		}

		if (HttpRequest.IsValid())
		{
			switch (HttpRequest->GetFailureReason())
			{
			case EHttpFailureReason::ConnectionError:
				Response.ErrorMessage = TEXT("OpenAI request failed due to connection error.");
				break;
			case EHttpFailureReason::Cancelled:
				Response.ErrorMessage = TEXT("OpenAI request was cancelled.");
				bShouldRetry = false;
				break;
			case EHttpFailureReason::TimedOut:
				Response.ErrorMessage = TEXT("OpenAI request timed out.");
				break;
			default:
				Response.ErrorMessage = TEXT("OpenAI request failed before receiving a response.");
				break;
			}
		}
		else
		{
			Response.ErrorMessage = TEXT("OpenAI request failed before receiving a response.");
		}

		UE_LOG(LogAINpc, Warning, TEXT("LLM request failed provider=openai requestId=%s httpStatus=%d durationMs=%.1f requestSucceeded=%s failureReason=%d retry=%d error=%s responseSummary=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			Response.HttpStatusCode,
			RequestDurationMs,
			bRequestSucceeded ? TEXT("true") : TEXT("false"),
			HttpRequest.IsValid() ? static_cast<int32>(HttpRequest->GetFailureReason()) : -1,
			RetryCount,
			*Response.ErrorMessage,
			SafeResponseSummary.IsEmpty() ? TEXT("<empty>") : *SafeResponseSummary);

		if (bShouldRetry)
		{
			const float BackoffDelay = RetryBackoffBaseSeconds * FMath::Pow(2.0f, static_cast<float>(RetryCount));
			FPlatformProcess::SleepNoStats(BackoffDelay);
			DispatchRequest(RequestId, OriginalRequest, [CompletionCallback](const FLLMResponse& RetryResponse)
			{
				if (*CompletionCallback)
				{
					(*CompletionCallback)(RetryResponse);
				}
			}, RetryCount + 1);
			return;
		}

		if (RetryCount >= MaxRequestRetries)
		{
			const FString RetryExhaustedMessage = TEXT("OpenAI request failed after exhausting retries.");
			if (Response.ErrorMessage.IsEmpty())
			{
				Response.ErrorMessage = RetryExhaustedMessage;
			}
			else if (!Response.ErrorMessage.Contains(TEXT("exhausting retries"), ESearchCase::IgnoreCase) &&
			         !Response.ErrorMessage.Contains(TEXT("retries exhausted"), ESearchCase::IgnoreCase))
			{
				Response.ErrorMessage = FString::Printf(TEXT("%s Retries exhausted."), *Response.ErrorMessage);
			}
			OnDegradation.Broadcast(RequestId);
		}
	}
	else
	{
		FString ParsedContent;
		FParsedLLMResponse ParsedResponse;
		FString ParseError = StreamingState.IsValid() ? StreamingState->GetErrorMessage() : FString();

		bool bParsed = false;
		if (ParseError.IsEmpty() && !ResponseBody.IsEmpty())
		{
			bParsed = TryExtractContent(ResponseBody, ParsedContent, ParsedResponse, ParseError);
		}
		if (ParseError.IsEmpty() && StreamingState.IsValid() && !bParsed)
		{
			ParsedContent = StreamingState->GetAccumulatedContent();
			if (!ParsedContent.IsEmpty())
			{
				ParsedResponse.Dialogue = ParsedContent;
				ParsedResponse.bParsedAsJson = false;
				ParsedResponse.ParseTier = ELLMResponseParseTier::PlainText;
				bParsed = true;
			}
		}
		const bool bIsHttpSuccess = EHttpResponseCodes::IsOk(Response.HttpStatusCode);

		if (bParsed && bIsHttpSuccess)
		{
			Response.bSuccess = true;
			Response.Content = MoveTemp(ParsedContent);
			Response.ParsedResponse = MoveTemp(ParsedResponse);
		}
		else
		{
			Response.ErrorMessage = ParseError.IsEmpty()
				? FString::Printf(TEXT("OpenAI request failed with HTTP %d."), Response.HttpStatusCode)
				: MoveTemp(ParseError);
		}
	}

	if (Response.bSuccess)
	{
		UE_LOG(LogAINpc, Log, TEXT("LLM request completed provider=openai requestId=%s httpStatus=%d durationMs=%.1f parseTier=%s parsedAsJson=%s dialogueLen=%d actionCount=%d retry=%d"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			Response.HttpStatusCode,
			RequestDurationMs,
			AINpc::LLMDiagnostics::DescribeParseTier(Response.ParsedResponse.ParseTier),
			Response.ParsedResponse.bParsedAsJson ? TEXT("true") : TEXT("false"),
			Response.Content.Len(),
			Response.ParsedResponse.Actions.Num(),
			RetryCount);
	}
	else if (bRequestSucceeded && HttpResponse.IsValid())
	{
		UE_LOG(LogAINpc, Warning, TEXT("LLM response rejected provider=openai requestId=%s httpStatus=%d durationMs=%.1f parseTier=%s parsedAsJson=%s retry=%d error=%s responseSummary=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			Response.HttpStatusCode,
			RequestDurationMs,
			AINpc::LLMDiagnostics::DescribeParseTier(Response.ParsedResponse.ParseTier),
			Response.ParsedResponse.bParsedAsJson ? TEXT("true") : TEXT("false"),
			RetryCount,
			*Response.ErrorMessage,
			SafeResponseSummary.IsEmpty() ? TEXT("<empty>") : *SafeResponseSummary);
	}

	Async(EAsyncExecution::ThreadPool, [CompletionCallback, Response = MoveTemp(Response)]() mutable
	{
		if (*CompletionCallback)
		{
			(*CompletionCallback)(Response);
		}
	});
}

bool FOpenAIProvider::TryExtractContent(
	const FString& ResponseBody,
	FString& OutContent,
	FParsedLLMResponse& OutParsedResponse,
	FString& OutErrorMessage) const
{
	if (!FLLMResponseParser::ParseOpenAIChatCompletion(ResponseBody, OutParsedResponse, OutErrorMessage))
	{
		return false;
	}

	OutContent = OutParsedResponse.Dialogue;
	if (OutContent.IsEmpty())
	{
		OutErrorMessage = TEXT("OpenAI response parser did not produce dialogue text.");
		return false;
	}

	return true;
}

FString FOpenAIProvider::ResolveApiKey(const FLLMRequest& Request) const
{
	const FString RequestApiKey = Request.ApiKey.TrimStartAndEnd();
	if (!RequestApiKey.IsEmpty())
	{
		return RequestApiKey;
	}

	return DefaultApiKey.TrimStartAndEnd();
}

FString FOpenAIProvider::ResolveModel(const FLLMRequest& Request) const
{
	return Request.Model.IsEmpty() ? DefaultModel : Request.Model;
}

FString FOpenAIProvider::ResolveBaseUrl(const FLLMRequest& Request) const
{
	// Per-request override baseurl mechanism: Request.BaseUrl takes precedence
	return Request.BaseUrl.IsEmpty() ? DefaultBaseUrl : Request.BaseUrl;
}

TSharedRef<FJsonObject> FOpenAIProvider::BuildJsonPayload(const FLLMRequest& Request) const
{
	const TSharedRef<FJsonObject> JsonPayload = MakeShared<FJsonObject>();
	JsonPayload->SetStringField(TEXT("model"), ResolveModel(Request));
	JsonPayload->SetNumberField(TEXT("temperature"), Request.Temperature);
	if (!Request.EffortLevel.IsEmpty())
	{
		JsonPayload->SetStringField(TEXT("effortLevel"), Request.EffortLevel);
	}
	if (Request.MaxTokens > 0)
	{
		JsonPayload->SetNumberField(TEXT("max_tokens"), Request.MaxTokens);
	}
	if (Request.bUseStreaming)
	{
		JsonPayload->SetBoolField(TEXT("stream"), true);
	}

	TArray<TSharedPtr<FJsonValue>> MessageArray;
	MessageArray.Reserve(Request.Messages.Num());

	const FLLMProviderCapabilities Capabilities = GetCapabilities();

	if (Capabilities.bSupportsFunctionCalling)
	{
		for (const FLLMMessage& Message : Request.Messages)
		{
			const TSharedRef<FJsonObject> MessageObject = MakeShared<FJsonObject>();
			MessageObject->SetStringField(TEXT("role"), Message.Role);
			MessageObject->SetStringField(TEXT("content"), Message.Content);
			MessageArray.Add(MakeShared<FJsonValueObject>(MessageObject));
		}
		JsonPayload->SetArrayField(TEXT("messages"), MessageArray);

		TArray<TSharedPtr<FJsonValue>> ToolsArray;
		const TSharedRef<FJsonObject> FunctionDefinition = MakeShared<FJsonObject>();
		FunctionDefinition->SetStringField(TEXT("name"), StructuredOutputToolName);
		FunctionDefinition->SetStringField(
			TEXT("description"),
			AINpc::StructuredOutputPrompts::GetToolDescription());
		FunctionDefinition->SetObjectField(TEXT("parameters"), BuildStructuredOutputParametersSchema());

		const TSharedRef<FJsonObject> ToolDefinition = MakeShared<FJsonObject>();
		ToolDefinition->SetStringField(TEXT("type"), TEXT("function"));
		ToolDefinition->SetObjectField(TEXT("function"), FunctionDefinition);
		ToolsArray.Add(MakeShared<FJsonValueObject>(ToolDefinition));
		JsonPayload->SetArrayField(TEXT("tools"), ToolsArray);

		const TSharedRef<FJsonObject> ToolChoiceFunction = MakeShared<FJsonObject>();
		ToolChoiceFunction->SetStringField(TEXT("name"), StructuredOutputToolName);
		const TSharedRef<FJsonObject> ToolChoiceObject = MakeShared<FJsonObject>();
		ToolChoiceObject->SetStringField(TEXT("type"), TEXT("function"));
		ToolChoiceObject->SetObjectField(TEXT("function"), ToolChoiceFunction);
		JsonPayload->SetObjectField(TEXT("tool_choice"), ToolChoiceObject);
	}
	else if (Capabilities.bSupportsJsonMode)
	{
		for (const FLLMMessage& Message : Request.Messages)
		{
			const TSharedRef<FJsonObject> MessageObject = MakeShared<FJsonObject>();
			MessageObject->SetStringField(TEXT("role"), Message.Role);
			MessageObject->SetStringField(TEXT("content"), Message.Content);
			MessageArray.Add(MakeShared<FJsonValueObject>(MessageObject));
		}

		const TSharedRef<FJsonObject> SystemMessage = MakeShared<FJsonObject>();
		SystemMessage->SetStringField(TEXT("role"), TEXT("system"));
		SystemMessage->SetStringField(TEXT("content"), AINpc::StructuredOutputPrompts::GetJsonInstruction());
		MessageArray.Insert(MakeShared<FJsonValueObject>(SystemMessage), 0);

		JsonPayload->SetArrayField(TEXT("messages"), MessageArray);

		const TSharedRef<FJsonObject> ResponseFormat = MakeShared<FJsonObject>();
		ResponseFormat->SetStringField(TEXT("type"), TEXT("json_object"));
		JsonPayload->SetObjectField(TEXT("response_format"), ResponseFormat);
	}
	else
	{
		for (const FLLMMessage& Message : Request.Messages)
		{
			const TSharedRef<FJsonObject> MessageObject = MakeShared<FJsonObject>();
			MessageObject->SetStringField(TEXT("role"), Message.Role);
			MessageObject->SetStringField(TEXT("content"), Message.Content);
			MessageArray.Add(MakeShared<FJsonValueObject>(MessageObject));
		}

		const TSharedRef<FJsonObject> SystemMessage = MakeShared<FJsonObject>();
		SystemMessage->SetStringField(TEXT("role"), TEXT("system"));
		SystemMessage->SetStringField(TEXT("content"), AINpc::StructuredOutputPrompts::GetStrictJsonInstruction());
		MessageArray.Insert(MakeShared<FJsonValueObject>(SystemMessage), 0);

		JsonPayload->SetArrayField(TEXT("messages"), MessageArray);
	}

	return JsonPayload;
}

void FOpenAIProvider::ProcessStreamResponse(
	const FGuid& RequestId,
	const FString& ResponseBody,
	const FLLMStreamCallback& StreamCallback) const
{
	if (!StreamCallback)
	{
		return;
	}

	FSSEParser Parser;
	FString AccumulatedContent;

	Parser.OnData.BindLambda([&](const FString& Data)
	{
		TSharedPtr<FJsonObject> JsonObject;
		const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(Data);
		if (!FJsonSerializer::Deserialize(JsonReader, JsonObject) || !JsonObject.IsValid())
		{
			FLLMStreamChunk ErrorChunk;
			ErrorChunk.RequestId = RequestId;
			ErrorChunk.ErrorMessage = FString::Printf(TEXT("OpenAI stream chunk JSON parse failed: %s"), *Data.Left(256));
			ErrorChunk.bIsError = true;
			ErrorChunk.bIsFinal = true;
			StreamCallback(ErrorChunk);
			return;
		}

		const TSharedPtr<FJsonObject>* ErrorObject = nullptr;
		if (JsonObject->TryGetObjectField(TEXT("error"), ErrorObject) && ErrorObject && ErrorObject->IsValid())
		{
			FString Message;
			if (!(*ErrorObject)->TryGetStringField(TEXT("message"), Message) || Message.IsEmpty())
			{
				Message = TEXT("OpenAI stream returned an error object.");
			}
			FLLMStreamChunk ErrorChunk;
			ErrorChunk.RequestId = RequestId;
			ErrorChunk.ErrorMessage = Message;
			ErrorChunk.bIsError = true;
			ErrorChunk.bIsFinal = true;
			StreamCallback(ErrorChunk);
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* ChoicesArray;
		if (JsonObject->TryGetArrayField(TEXT("choices"), ChoicesArray) && ChoicesArray->Num() > 0)
		{
			const TSharedPtr<FJsonObject>* ChoiceObject = nullptr;
			if ((*ChoicesArray)[0]->TryGetObject(ChoiceObject) && ChoiceObject && ChoiceObject->IsValid())
			{
				const TSharedPtr<FJsonObject>* DeltaObject = nullptr;
				if ((*ChoiceObject)->TryGetObjectField(TEXT("delta"), DeltaObject) && DeltaObject && DeltaObject->IsValid())
				{
					FString Content;
					if ((*DeltaObject)->TryGetStringField(TEXT("content"), Content))
					{
						AccumulatedContent += Content;
						FLLMStreamChunk Chunk;
						Chunk.RequestId = RequestId;
						Chunk.Content = Content;
						Chunk.bIsFinal = false;
						StreamCallback(Chunk);
					}
				}
			}
		}
	});

	Parser.OnDone.BindLambda([&]()
	{
		FLLMStreamChunk FinalChunk;
		FinalChunk.RequestId = RequestId;
		FinalChunk.Content = AccumulatedContent;
		FinalChunk.bIsFinal = true;
		StreamCallback(FinalChunk);
	});

	Parser.OnError.BindLambda([&](const FString& ErrorMessage)
	{
		FLLMStreamChunk ErrorChunk;
		ErrorChunk.RequestId = RequestId;
		ErrorChunk.ErrorMessage = ErrorMessage.IsEmpty() ? TEXT("OpenAI stream error.") : ErrorMessage;
		ErrorChunk.bIsError = true;
		ErrorChunk.bIsFinal = true;
		StreamCallback(ErrorChunk);
	});

	Parser.ProcessChunk(ResponseBody);
}

void FOpenAIProvider::ConfigureStreamingReceive(
	const FGuid& RequestId,
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe>& HttpRequest,
	const TSharedPtr<FLLMStreamCallback, ESPMode::ThreadSafe>& StreamCallback,
	TSharedPtr<FOpenAIStreamingState, ESPMode::ThreadSafe>& OutStreamingState) const
{
	if (!StreamCallback || !*StreamCallback)
	{
		return;
	}

	const TSharedRef<FOpenAIStreamingState, ESPMode::ThreadSafe> StreamingState =
		MakeShared<FOpenAIStreamingState, ESPMode::ThreadSafe>(RequestId, *StreamCallback);

	const bool bStreamConfigured = HttpRequest->SetResponseBodyReceiveStreamDelegateV2(
		FHttpRequestStreamDelegateV2::CreateLambda(
			[StreamingState](void* Data, int64& InOutLength)
			{
				StreamingState->AppendBytes(Data, InOutLength);
			}));

	if (bStreamConfigured)
	{
		OutStreamingState = StreamingState;
	}
}
