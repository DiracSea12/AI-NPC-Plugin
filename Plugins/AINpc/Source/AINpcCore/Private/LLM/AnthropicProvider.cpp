#include "LLM/AnthropicProvider.h"
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

namespace
{
TSharedRef<FJsonObject> BuildStructuredOutputInputSchema()
{
	const TSharedRef<FJsonObject> RootSchema = MakeShared<FJsonObject>();
	RootSchema->SetStringField(TEXT("type"), TEXT("object"));

	const TSharedRef<FJsonObject> RootProperties = MakeShared<FJsonObject>();
	RootProperties->SetField(TEXT("dialogue"), MakeTypeOnlySchemaField(TEXT("string")));

	const TSharedRef<FJsonObject> ActionItemSchema = MakeShared<FJsonObject>();
	ActionItemSchema->SetStringField(TEXT("type"), TEXT("object"));
	const TSharedRef<FJsonObject> ActionItemProperties = MakeShared<FJsonObject>();
	ActionItemProperties->SetField(TEXT("type"), MakeTypeOnlySchemaField(TEXT("string")));
	ActionItemProperties->SetField(TEXT("target"), MakeTypeOnlySchemaField(TEXT("string")));
	ActionItemSchema->SetObjectField(TEXT("properties"), ActionItemProperties);
	AddRequiredFieldList(*ActionItemSchema, { TEXT("type") });

	const TSharedRef<FJsonObject> ActionsSchema = MakeShared<FJsonObject>();
	ActionsSchema->SetStringField(TEXT("type"), TEXT("array"));
	ActionsSchema->SetObjectField(TEXT("items"), ActionItemSchema);
	RootProperties->SetObjectField(TEXT("actions"), ActionsSchema);

	const TSharedRef<FJsonObject> EmotionDeltaSchema = MakeShared<FJsonObject>();
	EmotionDeltaSchema->SetStringField(TEXT("type"), TEXT("object"));
	const TSharedRef<FJsonObject> EmotionDeltaProperties = MakeShared<FJsonObject>();
	EmotionDeltaProperties->SetField(TEXT("valence"), MakeTypeOnlySchemaField(TEXT("number")));
	EmotionDeltaProperties->SetField(TEXT("arousal"), MakeTypeOnlySchemaField(TEXT("number")));
	EmotionDeltaProperties->SetField(TEXT("dominance"), MakeTypeOnlySchemaField(TEXT("number")));
	EmotionDeltaSchema->SetObjectField(TEXT("properties"), EmotionDeltaProperties);
	AddRequiredFieldList(*EmotionDeltaSchema, { TEXT("valence"), TEXT("arousal"), TEXT("dominance") });
	RootProperties->SetObjectField(TEXT("emotion_delta"), EmotionDeltaSchema);

	const TSharedRef<FJsonObject> RelationshipDeltaSchema = MakeShared<FJsonObject>();
	RelationshipDeltaSchema->SetStringField(TEXT("type"), TEXT("object"));
	const TSharedRef<FJsonObject> RelationshipDeltaProperties = MakeShared<FJsonObject>();
	RelationshipDeltaProperties->SetField(TEXT("affinity"), MakeTypeOnlySchemaField(TEXT("number")));
	RelationshipDeltaProperties->SetField(TEXT("trust"), MakeTypeOnlySchemaField(TEXT("number")));
	RelationshipDeltaProperties->SetField(TEXT("familiarity"), MakeTypeOnlySchemaField(TEXT("number")));
	RelationshipDeltaSchema->SetObjectField(TEXT("properties"), RelationshipDeltaProperties);
	AddRequiredFieldList(*RelationshipDeltaSchema, { TEXT("affinity"), TEXT("trust"), TEXT("familiarity") });
	RootProperties->SetObjectField(TEXT("relationship_delta"), RelationshipDeltaSchema);

	RootSchema->SetObjectField(TEXT("properties"), RootProperties);
	AddRequiredFieldList(*RootSchema, { TEXT("dialogue"), TEXT("actions"), TEXT("emotion_delta"), TEXT("relationship_delta") });

	return RootSchema;
}
} // namespace

#if WITH_EDITOR
TAtomic<float> FAnthropicProvider::PreDispatchDelaySecondsForTest(0.0f);
TAtomic<float> FAnthropicProvider::PreProcessDelaySecondsForTest(0.0f);
TAtomic<bool> FAnthropicProvider::bReachedPostInitialCancelGateForTest(false);
#endif

// Default endpoint: https://api.anthropic.com/v1
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

	FString Endpoint = ResolveBaseUrl(Request);
	Endpoint.RemoveFromEnd(TEXT("/"));
	Endpoint += TEXT("/messages");
	UE_LOG(LogAINpc, Log, TEXT("Anthropic endpoint constructed: %s"), *Endpoint);
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

	HttpRequest->OnProcessRequestComplete().BindLambda(
		[
			WeakProvider = TWeakPtr<FAnthropicProvider, ESPMode::ThreadSafe>(AsShared()),
			RequestId,
			CompletionCallbackRef,
			StreamCallbackPtr,
			bUseStreaming = Request.bUseStreaming
		](FHttpRequestPtr RequestPtr, FHttpResponsePtr ResponsePtr, bool bRequestSucceeded)
		{
			if (const TSharedPtr<FAnthropicProvider, ESPMode::ThreadSafe> Provider = WeakProvider.Pin())
			{
				if (bUseStreaming && StreamCallbackPtr && bRequestSucceeded && ResponsePtr.IsValid())
				{
					Provider->ProcessStreamResponse(RequestId, ResponsePtr->GetContentAsString(), *StreamCallbackPtr);
				}
				Provider->CompleteRequest(RequestId, bRequestSucceeded, ResponsePtr, RequestPtr, CompletionCallbackRef);
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
	const TSharedRef<FLLMResponseCallback, ESPMode::ThreadSafe>& CompletionCallback)
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
				? FString::Printf(TEXT("Anthropic request failed with HTTP %d."), Response.HttpStatusCode)
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

TSharedRef<FJsonObject> FAnthropicProvider::BuildAnthropicMessagesPayload(const FLLMRequest& Request) const
{
	const TSharedRef<FJsonObject> JsonPayload = MakeShared<FJsonObject>();
	JsonPayload->SetStringField(TEXT("model"), ResolveModel(Request));
	JsonPayload->SetNumberField(TEXT("temperature"), Request.Temperature);
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

	if (Capabilities.bSupportsToolCalling)
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
		const TSharedRef<FJsonObject> ToolDefinition = MakeShared<FJsonObject>();
		ToolDefinition->SetStringField(TEXT("name"), StructuredOutputToolName);
		ToolDefinition->SetStringField(
			TEXT("description"),
			TEXT("Return structured NPC dialogue, action intents, and state deltas."));
		ToolDefinition->SetObjectField(TEXT("input_schema"), BuildStructuredOutputInputSchema());
		ToolsArray.Add(MakeShared<FJsonValueObject>(ToolDefinition));
		JsonPayload->SetArrayField(TEXT("tools"), ToolsArray);

		const TSharedRef<FJsonObject> ToolChoiceObject = MakeShared<FJsonObject>();
		ToolChoiceObject->SetStringField(TEXT("type"), TEXT("tool"));
		ToolChoiceObject->SetStringField(TEXT("name"), StructuredOutputToolName);
		JsonPayload->SetObjectField(TEXT("tool_choice"), ToolChoiceObject);
	}
	else
	{
		const TSharedRef<FJsonObject> SystemMessage = MakeShared<FJsonObject>();
		SystemMessage->SetStringField(TEXT("role"), TEXT("system"));
		SystemMessage->SetStringField(TEXT("content"),
			TEXT("You must respond with valid JSON matching this schema: "
			     "{\"dialogue\":string,\"actions\":[{\"type\":string,\"target\":string}],"
			     "\"emotion_delta\":{\"valence\":number,\"arousal\":number,\"dominance\":number},"
			     "\"relationship_delta\":{\"affinity\":number,\"trust\":number,\"familiarity\":number}}. "
			     "Do not include any text outside the JSON object."));
		MessageArray.Add(MakeShared<FJsonValueObject>(SystemMessage));

		for (const FLLMMessage& Message : Request.Messages)
		{
			const TSharedRef<FJsonObject> MessageObject = MakeShared<FJsonObject>();
			MessageObject->SetStringField(TEXT("role"), Message.Role);
			MessageObject->SetStringField(TEXT("content"), Message.Content);
			MessageArray.Add(MakeShared<FJsonValueObject>(MessageObject));
		}
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

	Parser.OnData.BindLambda([&](const FString& Data)
	{
		TSharedPtr<FJsonObject> JsonObject;
		const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(Data);
		if (FJsonSerializer::Deserialize(JsonReader, JsonObject) && JsonObject.IsValid())
		{
			FString EventType;
			if (JsonObject->TryGetStringField(TEXT("type"), EventType))
			{
				if (EventType.Equals(TEXT("content_block_delta"), ESearchCase::CaseSensitive))
				{
					const TSharedPtr<FJsonObject>* DeltaObject;
					if (JsonObject->TryGetObjectField(TEXT("delta"), DeltaObject))
					{
						FString Content;
						if ((*DeltaObject)->TryGetStringField(TEXT("text"), Content))
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
			}
		}
	});

	Parser.ProcessChunk(ResponseBody);
}
