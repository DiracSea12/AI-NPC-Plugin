#include "LLM/AnthropicProvider.h"
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
TAtomic<float> FAnthropicProvider::PreDispatchDelaySecondsForTest(0.0f);
TAtomic<float> FAnthropicProvider::PreProcessDelaySecondsForTest(0.0f);
TAtomic<bool> FAnthropicProvider::bReachedPostInitialCancelGateForTest(false);
#endif

namespace
{
bool TryExtractJsonStringFieldPrefix(const FString& PartialJson, const FString& FieldName, FString& OutValue)
{
	OutValue.Reset();

	FString NormalizedJson = PartialJson;
	NormalizedJson.ReplaceInline(TEXT("\\\\\""), TEXT("\""));
	NormalizedJson.ReplaceInline(TEXT("\\\""), TEXT("\""));

	const FString FieldToken = FString(TEXT("\"")) + FieldName + TEXT("\"");
	int32 Cursor = NormalizedJson.Find(FieldToken, ESearchCase::CaseSensitive);
	if (Cursor == INDEX_NONE)
	{
		return false;
	}

	Cursor += FieldToken.Len();
	while (Cursor < NormalizedJson.Len() && FChar::IsWhitespace(NormalizedJson[Cursor]))
	{
		++Cursor;
	}
	if (Cursor >= NormalizedJson.Len() || NormalizedJson[Cursor] != TEXT(':'))
	{
		return false;
	}

	++Cursor;
	while (Cursor < NormalizedJson.Len() && FChar::IsWhitespace(NormalizedJson[Cursor]))
	{
		++Cursor;
	}
	if (Cursor >= NormalizedJson.Len() || NormalizedJson[Cursor] != TEXT('"'))
	{
		return false;
	}

	++Cursor;
	bool bEscaped = false;
	for (; Cursor < NormalizedJson.Len(); ++Cursor)
	{
		const TCHAR Current = NormalizedJson[Cursor];
		if (bEscaped)
		{
			OutValue.AppendChar(Current);
			bEscaped = false;
			continue;
		}
		if (Current == TEXT('\\'))
		{
			bEscaped = true;
			continue;
		}
		if (Current == TEXT('"'))
		{
			return true;
		}
		OutValue.AppendChar(Current);
	}

	return !OutValue.IsEmpty();
}

bool TryBuildDialogueDeltaFromToolInputJson(
	FString& InOutAccumulatedToolInputJson,
	FString& InOutLastEmittedDialogue,
	const FString& PartialJson,
	FString& OutDialogueDelta)
{
	OutDialogueDelta.Reset();

	FString ReadablePartialJson = PartialJson;
	ReadablePartialJson.ReplaceInline(TEXT("\\\\\""), TEXT("\""));
	ReadablePartialJson.ReplaceInline(TEXT("\\\""), TEXT("\""));
	InOutAccumulatedToolInputJson += ReadablePartialJson;

	FString DialoguePrefix;
	if (!TryExtractJsonStringFieldPrefix(InOutAccumulatedToolInputJson, TEXT("dialogue"), DialoguePrefix))
	{
		return false;
	}

	if (DialoguePrefix.Len() <= InOutLastEmittedDialogue.Len() ||
		(!InOutLastEmittedDialogue.IsEmpty() && !DialoguePrefix.StartsWith(InOutLastEmittedDialogue, ESearchCase::CaseSensitive)))
	{
		return false;
	}

	OutDialogueDelta = DialoguePrefix.RightChop(InOutLastEmittedDialogue.Len());
	InOutLastEmittedDialogue = DialoguePrefix;
	return !OutDialogueDelta.IsEmpty();
}
}

struct FAnthropicStreamingState
{
	explicit FAnthropicStreamingState(const FGuid& InRequestId, FLLMStreamCallback InStreamCallback)
		: RequestId(InRequestId)
		, StreamCallback(MoveTemp(InStreamCallback))
	{
		Parser.OnData.BindRaw(this, &FAnthropicStreamingState::HandleData);
		Parser.OnDone.BindRaw(this, &FAnthropicStreamingState::HandleDone);
		Parser.OnError.BindRaw(this, &FAnthropicStreamingState::HandleError);
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
			HandleError(FString::Printf(TEXT("Anthropic stream chunk JSON parse failed: %s"), *Data.Left(256)));
			return;
		}

		FString EventType;
		if (JsonObject->TryGetStringField(TEXT("type"), EventType))
		{
			if (EventType.Equals(TEXT("content_block_delta"), ESearchCase::CaseSensitive))
			{
				const TSharedPtr<FJsonObject>* DeltaObject;
				if (JsonObject->TryGetObjectField(TEXT("delta"), DeltaObject) && DeltaObject && DeltaObject->IsValid())
				{
					FString Content;
					FString DeltaType;
					FString PartialJson;
					(*DeltaObject)->TryGetStringField(TEXT("type"), DeltaType);
					if (DeltaType.Equals(TEXT("input_json_delta"), ESearchCase::CaseSensitive) &&
						(*DeltaObject)->TryGetStringField(TEXT("partial_json"), PartialJson))
					{
						if (!TryBuildDialogueDeltaFromToolInputJson(AccumulatedToolInputJson, LastEmittedToolDialogue, PartialJson, Content))
						{
							return;
						}
					}
					else if (!DeltaType.Equals(TEXT("text_delta"), ESearchCase::CaseSensitive) ||
						!(*DeltaObject)->TryGetStringField(TEXT("text"), Content) ||
						Content.IsEmpty())
					{
						return;
					}
					if (!Content.IsEmpty())
					{
						AccumulatedContent += Content;
						FLLMStreamChunk Chunk;
						Chunk.RequestId = RequestId;
						Chunk.Content = Content;
						StreamCallback(Chunk);
					}
				}
			}
			else if (EventType.Equals(TEXT("message_stop"), ESearchCase::CaseSensitive))
			{
				HandleDone();
			}
			else if (EventType.Equals(TEXT("error"), ESearchCase::CaseSensitive))
			{
				const TSharedPtr<FJsonObject>* ErrorObject = nullptr;
				FString Message;
				if (JsonObject->TryGetObjectField(TEXT("error"), ErrorObject) && ErrorObject && ErrorObject->IsValid())
				{
					(*ErrorObject)->TryGetStringField(TEXT("message"), Message);
				}
				HandleError(Message.IsEmpty() ? TEXT("Anthropic stream returned an error event.") : Message);
			}
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
		StreamErrorMessage = Error.IsEmpty() ? TEXT("Anthropic stream error.") : Error;

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
	FString AccumulatedToolInputJson;
	FString LastEmittedToolDialogue;
	FString StreamErrorMessage;
	bool bFinalSent = false;
	bool bErrorSent = false;
	mutable FCriticalSection Mutex;
};

FAnthropicProvider::FAnthropicProvider(FString InDefaultApiKey, FString InDefaultModel, FString InBaseUrl)
	: DefaultApiKey(MoveTemp(InDefaultApiKey))
	, DefaultModel(MoveTemp(InDefaultModel))
	, DefaultBaseUrl(MoveTemp(InBaseUrl))
{
}

FGuid FAnthropicProvider::SendRequest(const FLLMRequest& Request, FLLMResponseCallback CompletionCallback)
{
	const FGuid RequestId = FGuid::NewGuid();
	const TWeakPtr<FAnthropicProvider, ESPMode::ThreadSafe> WeakProvider = AsShared();
	const FLLMRequest RequestCopy = Request;

	{
		FScopeLock Lock(&ActiveRequestsMutex);
		PendingRequests.Add(RequestId);
	}

	Async(EAsyncExecution::ThreadPool, [WeakProvider, RequestId, RequestCopy, CompletionCallback = MoveTemp(CompletionCallback)]() mutable
	{
		if (const TSharedPtr<FAnthropicProvider, ESPMode::ThreadSafe> Provider = WeakProvider.Pin())
		{
			Provider->DispatchRequest(RequestId, RequestCopy, MoveTemp(CompletionCallback));
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

bool FAnthropicProvider::CancelRequest(const FGuid& RequestId)
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

FLLMProviderCapabilities FAnthropicProvider::GetCapabilities() const
{
	FLLMProviderCapabilities Capabilities;
	Capabilities.bSupportsStreaming = true;
	Capabilities.bSupportsToolCalling = true;
	Capabilities.bSupportsJsonMode = false;
	return Capabilities;
}

#if WITH_EDITOR
void FAnthropicProvider::SetPreDispatchDelaySecondsForTest(const float DelaySeconds)
{
	PreDispatchDelaySecondsForTest = FMath::Max(0.0f, DelaySeconds);
}

void FAnthropicProvider::SetPreProcessDelaySecondsForTest(const float DelaySeconds)
{
	PreProcessDelaySecondsForTest = FMath::Max(0.0f, DelaySeconds);
	bReachedPostInitialCancelGateForTest = false;
}

bool FAnthropicProvider::HasReachedPostInitialCancelGateForTest()
{
	return bReachedPostInitialCancelGateForTest.Load();
}

FString FAnthropicProvider::BuildRequestBodyForTest(const FLLMRequest& Request) const
{
	FString RequestBody;
	const TSharedRef<FJsonObject> JsonPayload = BuildAnthropicMessagesPayload(Request);
	const TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(JsonPayload, JsonWriter);
	return RequestBody;
}

FString FAnthropicProvider::ResolveMessagesEndpointForTest(const FLLMRequest& Request) const
{
	return ResolveMessagesEndpoint(Request);
}
#endif

void FAnthropicProvider::DispatchRequest(const FGuid& RequestId, FLLMRequest Request, FLLMResponseCallback CompletionCallback)
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
			Response.ErrorMessage = TEXT("Anthropic request was cancelled.");
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
			Response.ErrorMessage = TEXT("Anthropic API key is empty.");
			CompletionCallback(Response);
		}
		return;
	}

	const TSharedRef<FJsonObject> JsonPayload = BuildAnthropicMessagesPayload(Request);

	FString RequestBody;
	const TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(JsonPayload, JsonWriter);

	const FString BaseUrl = ResolveBaseUrl(Request).TrimStartAndEnd();
	const FString Endpoint = ResolveMessagesEndpoint(Request);
	UE_LOG(LogAINpc, Log, TEXT("LLM request dispatch provider=anthropic requestId=%s baseUrl=%s model=%s effortLevel=%s endpoint=%s timeout=%.1f apiKey=%s"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*BaseUrl,
		*ResolveModel(Request),
		Request.EffortLevel.IsEmpty() ? TEXT("<empty>") : *Request.EffortLevel,
		*Endpoint,
		Request.TimeoutSeconds,
		ApiKey.IsEmpty() ? TEXT("missing") : TEXT("present"));
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(Endpoint);
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("x-api-key"), ApiKey);
	HttpRequest->SetHeader(TEXT("anthropic-version"), TEXT("2023-06-01"));
	HttpRequest->SetContentAsString(RequestBody);
	if (Request.TimeoutSeconds > 0.0f)
	{
		HttpRequest->SetTimeout(Request.TimeoutSeconds);
	}

	const TSharedRef<FLLMResponseCallback, ESPMode::ThreadSafe> CompletionCallbackRef =
		MakeShared<FLLMResponseCallback, ESPMode::ThreadSafe>(MoveTemp(CompletionCallback));
	TSharedPtr<FLLMStreamCallback, ESPMode::ThreadSafe> StreamCallbackPtr;
	if (Request.StreamCallback)
	{
		StreamCallbackPtr = MakeShared<FLLMStreamCallback, ESPMode::ThreadSafe>(Request.StreamCallback);
	}
	TSharedPtr<FAnthropicStreamingState, ESPMode::ThreadSafe> StreamingState;
	if (Request.bUseStreaming && StreamCallbackPtr)
	{
		ConfigureStreamingReceive(RequestId, HttpRequest, StreamCallbackPtr, StreamingState);
	}

	HttpRequest->OnProcessRequestComplete().BindLambda(
		[
			WeakProvider = TWeakPtr<FAnthropicProvider, ESPMode::ThreadSafe>(AsShared()),
			RequestId,
			CompletionCallbackRef,
			StreamCallbackPtr,
			bUseStreaming = Request.bUseStreaming,
			StreamingState
		](FHttpRequestPtr RequestPtr, FHttpResponsePtr ResponsePtr, bool bRequestSucceeded)
		{
			if (const TSharedPtr<FAnthropicProvider, ESPMode::ThreadSafe> Provider = WeakProvider.Pin())
			{
				if (bUseStreaming && StreamCallbackPtr && bRequestSucceeded && ResponsePtr.IsValid() && !StreamingState.IsValid())
				{
					Provider->ProcessStreamResponse(RequestId, ResponsePtr->GetContentAsString(), *StreamCallbackPtr);
				}
				Provider->CompleteRequest(RequestId, bRequestSucceeded, ResponsePtr, RequestPtr, CompletionCallbackRef, StreamingState);
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
						Response.ErrorMessage = TEXT("Anthropic request failed due to connection error.");
						break;
					case EHttpFailureReason::Cancelled:
						Response.ErrorMessage = TEXT("Anthropic request was cancelled.");
						break;
					case EHttpFailureReason::TimedOut:
						Response.ErrorMessage = TEXT("Anthropic request timed out.");
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
			Response.ErrorMessage = TEXT("Anthropic request was cancelled.");
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
			Response.ErrorMessage = TEXT("Failed to dispatch Anthropic HTTP request.");
			(*CompletionCallbackRef)(Response);
		}
	}
}

void FAnthropicProvider::CompleteRequest(
	const FGuid& RequestId,
	bool bRequestSucceeded,
	const FHttpResponsePtr& HttpResponse,
	const FHttpRequestPtr& HttpRequest,
	const TSharedRef<FLLMResponseCallback, ESPMode::ThreadSafe>& CompletionCallback,
	const TSharedPtr<FAnthropicStreamingState, ESPMode::ThreadSafe>& StreamingState)
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
		if (HttpRequest.IsValid())
		{
			switch (HttpRequest->GetFailureReason())
			{
			case EHttpFailureReason::ConnectionError:
				Response.ErrorMessage = TEXT("Anthropic request failed due to connection error.");
				break;
			case EHttpFailureReason::Cancelled:
				Response.ErrorMessage = TEXT("Anthropic request was cancelled.");
				break;
			case EHttpFailureReason::TimedOut:
				Response.ErrorMessage = TEXT("Anthropic request timed out.");
				break;
			default:
				Response.ErrorMessage = TEXT("Anthropic request failed before receiving a response.");
				break;
			}
		}
		else
		{
			Response.ErrorMessage = TEXT("Anthropic request failed before receiving a response.");
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
				? FString::Printf(TEXT("Anthropic request failed with HTTP %d."), Response.HttpStatusCode)
				: MoveTemp(ParseError);
		}
	}

	if (Response.bSuccess)
	{
		UE_LOG(LogAINpc, Log, TEXT("LLM request completed provider=anthropic requestId=%s httpStatus=%d durationMs=%.1f parseTier=%s parsedAsJson=%s dialogueLen=%d actionCount=%d"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			Response.HttpStatusCode,
			RequestDurationMs,
			AINpc::LLMDiagnostics::DescribeParseTier(Response.ParsedResponse.ParseTier),
			Response.ParsedResponse.bParsedAsJson ? TEXT("true") : TEXT("false"),
			Response.Content.Len(),
			Response.ParsedResponse.Actions.Num());
	}
	else
	{
		UE_LOG(LogAINpc, Warning, TEXT("LLM request failed provider=anthropic requestId=%s httpStatus=%d durationMs=%.1f requestSucceeded=%s failureReason=%d parseTier=%s parsedAsJson=%s error=%s responseSummary=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			Response.HttpStatusCode,
			RequestDurationMs,
			bRequestSucceeded ? TEXT("true") : TEXT("false"),
			HttpRequest.IsValid() ? static_cast<int32>(HttpRequest->GetFailureReason()) : -1,
			AINpc::LLMDiagnostics::DescribeParseTier(Response.ParsedResponse.ParseTier),
			Response.ParsedResponse.bParsedAsJson ? TEXT("true") : TEXT("false"),
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

bool FAnthropicProvider::TryExtractContent(
	const FString& ResponseBody,
	FString& OutContent,
	FParsedLLMResponse& OutParsedResponse,
	FString& OutErrorMessage) const
{
	if (!FLLMResponseParser::ParseAnthropicMessages(ResponseBody, OutParsedResponse, OutErrorMessage))
	{
		return false;
	}

	OutContent = OutParsedResponse.Dialogue;
	if (OutContent.IsEmpty())
	{
		OutErrorMessage = TEXT("Anthropic response parser did not produce dialogue text.");
		return false;
	}

	return true;
}

FString FAnthropicProvider::ResolveApiKey(const FLLMRequest& Request) const
{
	const FString RequestApiKey = Request.ApiKey.TrimStartAndEnd();
	if (!RequestApiKey.IsEmpty())
	{
		return RequestApiKey;
	}

	return DefaultApiKey.TrimStartAndEnd();
}

FString FAnthropicProvider::ResolveModel(const FLLMRequest& Request) const
{
	return Request.Model.IsEmpty() ? DefaultModel : Request.Model;
}

FString FAnthropicProvider::ResolveBaseUrl(const FLLMRequest& Request) const
{
	return Request.BaseUrl.IsEmpty() ? DefaultBaseUrl : Request.BaseUrl;
}

FString FAnthropicProvider::ResolveMessagesEndpoint(const FLLMRequest& Request) const
{
	FString Endpoint = ResolveBaseUrl(Request).TrimStartAndEnd();
	Endpoint.RemoveFromEnd(TEXT("/"));

	if (Endpoint.EndsWith(TEXT("/messages"), ESearchCase::IgnoreCase))
	{
		return Endpoint;
	}

	if (Endpoint.EndsWith(TEXT("/v1"), ESearchCase::IgnoreCase))
	{
		return Endpoint + TEXT("/messages");
	}

	return Endpoint + TEXT("/v1/messages");
}

TSharedRef<FJsonObject> FAnthropicProvider::BuildAnthropicMessagesPayload(const FLLMRequest& Request) const
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
	else
	{
		JsonPayload->SetNumberField(TEXT("max_tokens"), 4096);
	}

	TArray<TSharedPtr<FJsonValue>> MessageArray;
	MessageArray.Reserve(Request.Messages.Num());

	const FLLMProviderCapabilities Capabilities = GetCapabilities();
	const bool bUsesStructuredToolOutput = Capabilities.bSupportsToolCalling;

	if (bUsesStructuredToolOutput)
	{
		FString SystemText;
		for (const FLLMMessage& Message : Request.Messages)
		{
			const FString Role = Message.Role.TrimStartAndEnd().ToLower();
			if (Role == TEXT("system"))
			{
				if (!SystemText.IsEmpty())
				{
					SystemText += TEXT("\n\n");
				}
				SystemText += Message.Content;
				continue;
			}

			const TSharedRef<FJsonObject> MessageObject = MakeShared<FJsonObject>();
			MessageObject->SetStringField(TEXT("role"), Role == TEXT("assistant") ? TEXT("assistant") : TEXT("user"));
			MessageObject->SetStringField(TEXT("content"), Message.Content);
			MessageArray.Add(MakeShared<FJsonValueObject>(MessageObject));
		}
		if (!SystemText.IsEmpty())
		{
			JsonPayload->SetStringField(TEXT("system"), SystemText);
		}
		JsonPayload->SetArrayField(TEXT("messages"), MessageArray);

		TArray<TSharedPtr<FJsonValue>> ToolsArray;
		const TSharedRef<FJsonObject> ToolDefinition = MakeShared<FJsonObject>();
		ToolDefinition->SetStringField(TEXT("name"), StructuredOutputToolName);
		ToolDefinition->SetStringField(
			TEXT("description"),
			AINpc::StructuredOutputPrompts::GetToolDescription());
		ToolDefinition->SetObjectField(TEXT("input_schema"), BuildStructuredOutputInputSchema());
		ToolsArray.Add(MakeShared<FJsonValueObject>(ToolDefinition));
		JsonPayload->SetArrayField(TEXT("tools"), ToolsArray);

	}
	else
	{
		FString SystemText = AINpc::StructuredOutputPrompts::GetStrictJsonInstruction();
		for (const FLLMMessage& Message : Request.Messages)
		{
			const FString Role = Message.Role.TrimStartAndEnd().ToLower();
			if (Role == TEXT("system"))
			{
				if (!SystemText.IsEmpty())
				{
					SystemText += TEXT("\n\n");
				}
				SystemText += Message.Content;
				continue;
			}

			const TSharedRef<FJsonObject> MessageObject = MakeShared<FJsonObject>();
			MessageObject->SetStringField(TEXT("role"), Role == TEXT("assistant") ? TEXT("assistant") : TEXT("user"));
			MessageObject->SetStringField(TEXT("content"), Message.Content);
			MessageArray.Add(MakeShared<FJsonValueObject>(MessageObject));
		}
		JsonPayload->SetStringField(TEXT("system"), SystemText);
		JsonPayload->SetArrayField(TEXT("messages"), MessageArray);
	}

	if (Request.bUseStreaming)
	{
		JsonPayload->SetBoolField(TEXT("stream"), true);
	}

	return JsonPayload;
}

void FAnthropicProvider::ProcessStreamResponse(
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
	FString AccumulatedToolInputJson;
	FString LastEmittedToolDialogue;

	Parser.OnData.BindLambda([&](const FString& Data)
	{
		TSharedPtr<FJsonObject> JsonObject;
		const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(Data);
		if (!FJsonSerializer::Deserialize(JsonReader, JsonObject) || !JsonObject.IsValid())
		{
			FLLMStreamChunk ErrorChunk;
			ErrorChunk.RequestId = RequestId;
			ErrorChunk.ErrorMessage = FString::Printf(TEXT("Anthropic stream chunk JSON parse failed: %s"), *Data.Left(256));
			ErrorChunk.bIsError = true;
			ErrorChunk.bIsFinal = true;
			StreamCallback(ErrorChunk);
			return;
		}

		FString EventType;
		if (JsonObject->TryGetStringField(TEXT("type"), EventType))
		{
			if (EventType.Equals(TEXT("content_block_delta"), ESearchCase::CaseSensitive))
			{
				const TSharedPtr<FJsonObject>* DeltaObject;
				if (JsonObject->TryGetObjectField(TEXT("delta"), DeltaObject))
				{
					FString Content;
					FString DeltaType;
					FString PartialJson;
					(*DeltaObject)->TryGetStringField(TEXT("type"), DeltaType);
					if (DeltaType.Equals(TEXT("input_json_delta"), ESearchCase::CaseSensitive) &&
						(*DeltaObject)->TryGetStringField(TEXT("partial_json"), PartialJson))
					{
						if (!TryBuildDialogueDeltaFromToolInputJson(AccumulatedToolInputJson, LastEmittedToolDialogue, PartialJson, Content))
						{
							return;
						}
					}
					else if (!DeltaType.Equals(TEXT("text_delta"), ESearchCase::CaseSensitive) ||
						!(*DeltaObject)->TryGetStringField(TEXT("text"), Content) ||
						Content.IsEmpty())
					{
						return;
					}
					if (!Content.IsEmpty())
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
			else if (EventType.Equals(TEXT("message_stop"), ESearchCase::CaseSensitive))
			{
				FLLMStreamChunk FinalChunk;
				FinalChunk.RequestId = RequestId;
				FinalChunk.Content = AccumulatedContent;
				FinalChunk.bIsFinal = true;
				StreamCallback(FinalChunk);
			}
			else if (EventType.Equals(TEXT("error"), ESearchCase::CaseSensitive))
			{
				const TSharedPtr<FJsonObject>* ErrorObject = nullptr;
				FString Message;
				if (JsonObject->TryGetObjectField(TEXT("error"), ErrorObject) && ErrorObject && ErrorObject->IsValid())
				{
					(*ErrorObject)->TryGetStringField(TEXT("message"), Message);
				}
				FLLMStreamChunk ErrorChunk;
				ErrorChunk.RequestId = RequestId;
				ErrorChunk.ErrorMessage = Message.IsEmpty() ? TEXT("Anthropic stream returned an error event.") : Message;
				ErrorChunk.bIsError = true;
				ErrorChunk.bIsFinal = true;
				StreamCallback(ErrorChunk);
			}
		}
	});

	Parser.OnError.BindLambda([&](const FString& ErrorMessage)
	{
		FLLMStreamChunk ErrorChunk;
		ErrorChunk.RequestId = RequestId;
		ErrorChunk.ErrorMessage = ErrorMessage.IsEmpty() ? TEXT("Anthropic stream error.") : ErrorMessage;
		ErrorChunk.bIsError = true;
		ErrorChunk.bIsFinal = true;
		StreamCallback(ErrorChunk);
	});

	Parser.ProcessChunk(ResponseBody);
}

void FAnthropicProvider::ConfigureStreamingReceive(
	const FGuid& RequestId,
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe>& HttpRequest,
	const TSharedPtr<FLLMStreamCallback, ESPMode::ThreadSafe>& StreamCallback,
	TSharedPtr<FAnthropicStreamingState, ESPMode::ThreadSafe>& OutStreamingState) const
{
	if (!StreamCallback || !*StreamCallback)
	{
		return;
	}

	const TSharedRef<FAnthropicStreamingState, ESPMode::ThreadSafe> StreamingState =
		MakeShared<FAnthropicStreamingState, ESPMode::ThreadSafe>(RequestId, *StreamCallback);

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
