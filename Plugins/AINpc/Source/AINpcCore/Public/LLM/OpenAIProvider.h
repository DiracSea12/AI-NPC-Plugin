#pragma once

#include "HAL/CriticalSection.h"
#include "Templates/Atomic.h"
#include "LLM/ILLMProvider.h"

class IHttpRequest;
class IHttpResponse;
class FJsonObject;

/**
 * OpenAI Chat Completions provider with custom BaseURL support.
 * Compatible with OpenAI-compatible APIs (e.g., DeepSeek: https://api.deepseek.com/v1).
 */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnProviderDegradation, const FGuid&);

class AINPCCORE_API FOpenAIProvider : public ILLMProvider, public TSharedFromThis<FOpenAIProvider, ESPMode::ThreadSafe>
{
public:
	explicit FOpenAIProvider(
		FString InDefaultApiKey = FString(),
		FString InDefaultModel = TEXT("gpt-4o-mini"),
		FString InBaseUrl = TEXT("https://api.openai.com/v1"));

	virtual FGuid SendRequest(const FLLMRequest& Request, FLLMResponseCallback CompletionCallback) override;
	virtual bool CancelRequest(const FGuid& RequestId) override;
	virtual FLLMProviderCapabilities GetCapabilities() const override;

	FOnProviderDegradation OnDegradation;

#if WITH_EDITOR
	static void SetPreDispatchDelaySecondsForTest(float DelaySeconds);
	static void SetPreProcessDelaySecondsForTest(float DelaySeconds);
	static bool HasReachedPostInitialCancelGateForTest();
	FString BuildRequestBodyForTest(const FLLMRequest& Request) const;
	FString ResolveBaseUrlForTest(const FLLMRequest& Request) const { return ResolveBaseUrl(Request); }
#endif

private:
	void DispatchRequest(const FGuid& RequestId, FLLMRequest Request, FLLMResponseCallback CompletionCallback, int32 RetryCount = 0);
	void CompleteRequest(
		const FGuid& RequestId,
		bool bRequestSucceeded,
		const TSharedPtr<IHttpResponse, ESPMode::ThreadSafe>& HttpResponse,
		const TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>& HttpRequest,
		const TSharedRef<FLLMResponseCallback, ESPMode::ThreadSafe>& CompletionCallback,
		const FLLMRequest& OriginalRequest,
		int32 RetryCount);

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
	TSharedRef<FJsonObject> BuildJsonPayload(const FLLMRequest& Request) const;

private:
	FString DefaultApiKey;
	FString DefaultModel;
	FString DefaultBaseUrl;

	static constexpr int32 MaxRequestRetries = 3;
	static constexpr float RetryBackoffBaseSeconds = 1.0f;
	static constexpr float RequestTimeoutSeconds = 30.0f;
	inline static const FString FallbackResponse = TEXT("{\"dialogue\":\"I need a moment to think.\",\"actions\":[],\"emotion_delta\":{\"valence\":0,\"arousal\":0,\"dominance\":0},\"relationship_delta\":{\"affinity\":0,\"trust\":0,\"familiarity\":0}}");

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
