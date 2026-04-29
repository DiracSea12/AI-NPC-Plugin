#pragma once

#include "LLM/ILLMProvider.h"

class AActor;

class AINPCCORE_API FLLMProviderChain : public ILLMProvider, public TSharedFromThis<FLLMProviderChain, ESPMode::ThreadSafe>
{
public:
	explicit FLLMProviderChain(
		TSharedPtr<ILLMProvider> InPrimaryProvider,
		TSharedPtr<ILLMProvider> InFallbackProvider = nullptr,
		TArray<FString> InFallbackResponses = TArray<FString>(),
		int32 InMaxRetries = 2,
		float InBaseRetryDelaySeconds = 1.0f);

	virtual FGuid SendRequest(const FLLMRequest& Request, FLLMResponseCallback CompletionCallback) override;
	virtual bool CancelRequest(const FGuid& RequestId) override;
	virtual FLLMProviderCapabilities GetCapabilities() const override;

	void SetNpcActor(AActor* InNpcActor) { NpcActor = InNpcActor; }

private:
	void TryPrimaryWithRetry(const FGuid& RequestId, const FLLMRequest& Request, FLLMResponseCallback CompletionCallback, int32 RetryCount);
	void TryFallbackProvider(const FGuid& RequestId, const FLLMRequest& Request, FLLMResponseCallback CompletionCallback, int32 RetryCount);
	void UseFallbackTemplate(const FGuid& RequestId, FLLMResponseCallback CompletionCallback, int32 RetryCount);
	void BroadcastDegradationEvent(const FString& Reason, int32 RetryCount, bool bUsedTemplate);

private:
	TSharedPtr<ILLMProvider> PrimaryProvider;
	TSharedPtr<ILLMProvider> FallbackProvider;
	TArray<FString> FallbackResponses;
	int32 MaxRetries;
	float BaseRetryDelaySeconds;
	TWeakObjectPtr<AActor> NpcActor;

	mutable FCriticalSection RequestMutex;
	TMap<FGuid, FGuid> ActivePrimaryRequests;
	TMap<FGuid, FGuid> ActiveFallbackRequests;
};
