#pragma once

#include "HAL/CriticalSection.h"
#include "Templates/Atomic.h"
#include "LLM/ILLMProvider.h"

class IHttpRequest;
class IHttpResponse;
class FJsonObject;

class AINPCCORE_API FLocalProvider : public ILLMProvider, public TSharedFromThis<FLocalProvider, ESPMode::ThreadSafe>
{
public:
	explicit FLocalProvider(
		FString InDefaultModel = FString(),
		FString InBaseUrl = FString());

	virtual FGuid SendRequest(const FLLMRequest& Request, FLLMResponseCallback CompletionCallback) override;
	virtual bool CancelRequest(const FGuid& RequestId) override;
	virtual FLLMProviderCapabilities GetCapabilities() const override;

#if WITH_EDITOR
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
	FString ResolveModel(const FLLMRequest& Request) const;
	FString ResolveBaseUrl(const FLLMRequest& Request) const;
	TSharedRef<FJsonObject> BuildJsonPayload(const FLLMRequest& Request) const;

private:
	FString DefaultModel;
	FString DefaultBaseUrl;

	mutable FCriticalSection ActiveRequestsMutex;
	TSet<FGuid> PendingRequests;
	TSet<FGuid> CancelledPendingRequests;
	TMap<FGuid, TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>> ActiveRequests;
};
