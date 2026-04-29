#pragma once

#include "HAL/CriticalSection.h"
#include "Templates/Atomic.h"
#include "LLM/ILLMProvider.h"

class IHttpRequest;
class IHttpResponse;
class FJsonObject;

class AINPCCORE_API FAnthropicProvider : public ILLMProvider, public TSharedFromThis<FAnthropicProvider, ESPMode::ThreadSafe>
{
public:
	explicit FAnthropicProvider(
		FString InDefaultApiKey = FString(),
		FString InDefaultModel = TEXT("claude-3-5-sonnet-20241022"),
		FString InBaseUrl = TEXT("https://api.anthropic.com/v1"));

	virtual FGuid SendRequest(const FLLMRequest& Request, FLLMResponseCallback CompletionCallback) override;
	virtual bool CancelRequest(const FGuid& RequestId) override;
	virtual FLLMProviderCapabilities GetCapabilities() const override;

#if WITH_EDITOR
	static void SetPreDispatchDelaySecondsForTest(float DelaySeconds);
	static void SetPreProcessDelaySecondsForTest(float DelaySeconds);
	static bool HasReachedPostInitialCancelGateForTest();
	FString BuildRequestBodyForTest(const FLLMRequest& Request) const;
#endif

private:
	void DispatchRequest(const FGuid& RequestId, FLLMRequest Request, FLLMResponseCallback CompletionCallback);
	void CompleteRequest(
		const FGuid& RequestId,
		bool bRequestSucceeded,
		const TSharedPtr<IHttpResponse, ESPMode::ThreadSafe>& HttpResponse,
		const TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>& HttpRequest,
		const TSharedRef<FLLMResponseCallback, ESPMode::ThreadSafe>& CompletionCallback);

	bool TryExtractContent(
		const FString& ResponseBody,
		FString& OutContent,
		FParsedLLMResponse& OutParsedResponse,
		FString& OutErrorMessage) const;
	void ProcessStreamResponse(
		const FGuid& RequestId,
		const FString& ResponseBody,
		const FLLMStreamCallback& StreamCallback) const;
	FString ResolveApiKey(const FLLMRequest& Request) const;
	FString ResolveModel(const FLLMRequest& Request) const;
	FString ResolveBaseUrl(const FLLMRequest& Request) const;
	TSharedRef<FJsonObject> BuildAnthropicMessagesPayload(const FLLMRequest& Request) const;

private:
	FString DefaultApiKey;
	FString DefaultModel;
	FString DefaultBaseUrl;

	mutable FCriticalSection ActiveRequestsMutex;
	TSet<FGuid> PendingRequests;
	TSet<FGuid> CancelledPendingRequests;
	TMap<FGuid, TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>> ActiveRequests;

#if WITH_EDITOR
	static TAtomic<float> PreDispatchDelaySecondsForTest;
	static TAtomic<float> PreProcessDelaySecondsForTest;
	static TAtomic<bool> bReachedPostInitialCancelGateForTest;
#endif
};
