#include "LLM/OpenAIProvider.h"
#include "LLM/StructuredOutputSchemaHelpers.h"
#include "LLM/SSEParser.h"

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

	const TSharedRef<FJsonObject> JsonPayload = BuildJsonPayload(Request);

	FString RequestBody;
	const TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(JsonPayload, JsonWriter);

	FString Endpoint = ResolveBaseUrl(Request);
	Endpoint.RemoveFromEnd(TEXT("/"));
	Endpoint += TEXT("/chat/completions");
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

	HttpRequest->OnProcessRequestComplete().BindLambda(
		[
			WeakProvider = AsWeak(),
			RequestId,
			CompletionCallbackRef,
			StreamCallbackPtr,
			bUseStreaming = Request.bUseStreaming,
			RequestCopy = Request,
			RetryCount
		](FHttpRequestPtr RequestPtr, FHttpResponsePtr ResponsePtr, bool bRequestSucceeded)
		{
			if (const TSharedPtr<FOpenAIProvider, ESPMode::ThreadSafe> Provider = WeakProvider.Pin())
			{
				if (bUseStreaming && StreamCallbackPtr && bRequestSucceeded && ResponsePtr.IsValid())
				{
					Provider->ProcessStreamResponse(RequestId, ResponsePtr->GetContentAsString(), *StreamCallbackPtr);
				}
				Provider->CompleteRequest(RequestId, bRequestSucceeded, ResponsePtr, RequestPtr, CompletionCallbackRef, RequestCopy, RetryCount);
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
	int32 RetryCount)
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
			Response.Content = FallbackResponse;
			Response.bSuccess = true;
			OnDegradation.Broadcast(RequestId);
		}
	}
	else
	{
		const FString ResponseBody = HttpResponse->GetContentAsString();
		FString ParsedContent;
		FParsedLLMResponse ParsedResponse;
		FString ParseError;

		const bool bParsed = TryExtractContent(ResponseBody, ParsedContent, ParsedResponse, ParseError);
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
			TEXT("Return structured NPC dialogue, action intents, and state deltas."));
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
		SystemMessage->SetStringField(TEXT("content"),
			TEXT("You must respond with valid JSON matching this schema: "
			     "{\"dialogue\":string,\"actions\":[{\"type\":string,\"target\":string}],"
			     "\"emotion_delta\":{\"valence\":number,\"arousal\":number,\"dominance\":number},"
			     "\"relationship_delta\":{\"affinity\":number,\"trust\":number,\"familiarity\":number}}"));
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
		SystemMessage->SetStringField(TEXT("content"),
			TEXT("You must respond with valid JSON matching this schema: "
			     "{\"dialogue\":string,\"actions\":[{\"type\":string,\"target\":string}],"
			     "\"emotion_delta\":{\"valence\":number,\"arousal\":number,\"dominance\":number},"
			     "\"relationship_delta\":{\"affinity\":number,\"trust\":number,\"familiarity\":number}}. "
			     "Do not include any text outside the JSON object."));
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
		if (FJsonSerializer::Deserialize(JsonReader, JsonObject) && JsonObject.IsValid())
		{
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

	Parser.ProcessChunk(ResponseBody);
}
