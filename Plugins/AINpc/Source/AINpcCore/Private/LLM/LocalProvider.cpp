#include "LLM/LocalProvider.h"
#include "LLM/StructuredOutputSchemaHelpers.h"

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

FLocalProvider::FLocalProvider(FString InDefaultModel, FString InBaseUrl)
	: DefaultModel(MoveTemp(InDefaultModel))
	, DefaultBaseUrl(MoveTemp(InBaseUrl))
{
}

FGuid FLocalProvider::SendRequest(const FLLMRequest& Request, FLLMResponseCallback CompletionCallback)
{
	const FGuid RequestId = FGuid::NewGuid();
	const TWeakPtr<FLocalProvider, ESPMode::ThreadSafe> WeakProvider = AsShared();
	const FLLMRequest RequestCopy = Request;

	{
		FScopeLock Lock(&ActiveRequestsMutex);
		PendingRequests.Add(RequestId);
	}

	Async(EAsyncExecution::ThreadPool, [WeakProvider, RequestId, RequestCopy, CompletionCallback = MoveTemp(CompletionCallback)]() mutable
	{
		if (const TSharedPtr<FLocalProvider, ESPMode::ThreadSafe> Provider = WeakProvider.Pin())
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

bool FLocalProvider::CancelRequest(const FGuid& RequestId)
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

FLLMProviderCapabilities FLocalProvider::GetCapabilities() const
{
	FLLMProviderCapabilities Capabilities;
	Capabilities.bSupportsStreaming = false;
	Capabilities.bSupportsFunctionCalling = true;
	Capabilities.bSupportsJsonMode = true;
	return Capabilities;
}

void FLocalProvider::DispatchRequest(const FGuid& RequestId, FLLMRequest Request, FLLMResponseCallback CompletionCallback)
{
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
			Response.ErrorMessage = TEXT("Local provider request was cancelled.");
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
	HttpRequest->SetURL(Endpoint);
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetContentAsString(RequestBody);
	if (Request.TimeoutSeconds > 0.0f)
	{
		HttpRequest->SetTimeout(Request.TimeoutSeconds);
	}

	const TSharedRef<FLLMResponseCallback, ESPMode::ThreadSafe> CompletionCallbackRef =
		MakeShared<FLLMResponseCallback, ESPMode::ThreadSafe>(MoveTemp(CompletionCallback));

	HttpRequest->OnProcessRequestComplete().BindLambda(
		[
			WeakProvider = TWeakPtr<FLocalProvider, ESPMode::ThreadSafe>(AsShared()),
			RequestId,
			CompletionCallbackRef
		](FHttpRequestPtr RequestPtr, FHttpResponsePtr ResponsePtr, bool bRequestSucceeded)
		{
			if (const TSharedPtr<FLocalProvider, ESPMode::ThreadSafe> Provider = WeakProvider.Pin())
			{
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
						Response.ErrorMessage = TEXT("Local provider request failed due to connection error.");
						break;
					case EHttpFailureReason::Cancelled:
						Response.ErrorMessage = TEXT("Local provider request was cancelled.");
						break;
					case EHttpFailureReason::TimedOut:
						Response.ErrorMessage = TEXT("Local provider request timed out.");
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
			Response.ErrorMessage = TEXT("Local provider request was cancelled.");
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
			Response.ErrorMessage = TEXT("Failed to dispatch local provider HTTP request.");
			(*CompletionCallbackRef)(Response);
		}
	}
}

void FLocalProvider::CompleteRequest(
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
				Response.ErrorMessage = TEXT("Local provider request failed due to connection error.");
				break;
			case EHttpFailureReason::Cancelled:
				Response.ErrorMessage = TEXT("Local provider request was cancelled.");
				break;
			case EHttpFailureReason::TimedOut:
				Response.ErrorMessage = TEXT("Local provider request timed out.");
				break;
			default:
				Response.ErrorMessage = TEXT("Local provider request failed before receiving a response.");
				break;
			}
		}
		else
		{
			Response.ErrorMessage = TEXT("Local provider request failed before receiving a response.");
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
				? FString::Printf(TEXT("Local provider request failed with HTTP %d."), Response.HttpStatusCode)
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

bool FLocalProvider::TryExtractContent(
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
		OutErrorMessage = TEXT("Local provider response parser did not produce dialogue text.");
		return false;
	}

	return true;
}

FString FLocalProvider::ResolveModel(const FLLMRequest& Request) const
{
	return Request.Model.IsEmpty() ? DefaultModel : Request.Model;
}

FString FLocalProvider::ResolveBaseUrl(const FLLMRequest& Request) const
{
	return Request.BaseUrl.IsEmpty() ? DefaultBaseUrl : Request.BaseUrl;
}

TSharedRef<FJsonObject> FLocalProvider::BuildJsonPayload(const FLLMRequest& Request) const
{
	const TSharedRef<FJsonObject> JsonPayload = MakeShared<FJsonObject>();
	JsonPayload->SetStringField(TEXT("model"), ResolveModel(Request));
	JsonPayload->SetNumberField(TEXT("temperature"), Request.Temperature);
	if (Request.MaxTokens > 0)
	{
		JsonPayload->SetNumberField(TEXT("max_tokens"), Request.MaxTokens);
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
		const TSharedRef<FJsonObject> SystemMessage = MakeShared<FJsonObject>();
		SystemMessage->SetStringField(TEXT("role"), TEXT("system"));
		SystemMessage->SetStringField(TEXT("content"),
			TEXT("You must respond with valid JSON matching this schema: "
			     "{\"dialogue\":string,\"actions\":[{\"type\":string,\"target\":string}],"
			     "\"emotion_delta\":{\"valence\":number,\"arousal\":number,\"dominance\":number},"
			     "\"relationship_delta\":{\"affinity\":number,\"trust\":number,\"familiarity\":number}}"));
		MessageArray.Add(MakeShared<FJsonValueObject>(SystemMessage));

		for (const FLLMMessage& Message : Request.Messages)
		{
			const TSharedRef<FJsonObject> MessageObject = MakeShared<FJsonObject>();
			MessageObject->SetStringField(TEXT("role"), Message.Role);
			MessageObject->SetStringField(TEXT("content"), Message.Content);
			MessageArray.Add(MakeShared<FJsonValueObject>(MessageObject));
		}
		JsonPayload->SetArrayField(TEXT("messages"), MessageArray);

		const TSharedRef<FJsonObject> ResponseFormat = MakeShared<FJsonObject>();
		ResponseFormat->SetStringField(TEXT("type"), TEXT("json_object"));
		JsonPayload->SetObjectField(TEXT("response_format"), ResponseFormat);
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

	return JsonPayload;
}
