#pragma once

#include "CoreMinimal.h"
#include "LLM/LLMResponseParser.h"

enum class ELLMFallbackReason : uint8
{
	None,
	PrimaryFailed,
	FallbackFailed,
	NoTemplateAvailable
};

struct AINPCCORE_API FLLMProviderCapabilities
{
	bool bSupportsStreaming = false;
	bool bSupportsFunctionCalling = false;
	bool bSupportsJsonMode = false;
	bool bSupportsToolCalling = false;
};

struct AINPCCORE_API FLLMMessage
{
	FString Role;
	FString Content;
};

struct AINPCCORE_API FLLMStreamChunk
{
	FGuid RequestId;
	FString Content;
	FString ErrorMessage;
	bool bIsFinal = false;
	bool bIsError = false;
};

using FLLMStreamCallback = TFunction<void(const FLLMStreamChunk&)>;

struct AINPCCORE_API FLLMRequest
{
	TArray<FLLMMessage> Messages;
	FString Model;
	FString ApiKey;
	FString BaseUrl;
	FString EffortLevel;
	float Temperature = 0.7f;
	int32 MaxTokens = 0;
	float TimeoutSeconds = 0.0f;
	bool bUseStreaming = false;
	FLLMStreamCallback StreamCallback;
};

struct AINPCCORE_API FLLMResponse
{
	FGuid RequestId;
	bool bSuccess = false;
	int32 HttpStatusCode = 0;
	FString Content;
	FParsedLLMResponse ParsedResponse;
	FString ErrorMessage;
	bool bIsFallback = false;
	ELLMFallbackReason FallbackReason = ELLMFallbackReason::None;
};

using FLLMResponseCallback = TFunction<void(const FLLMResponse&)>;
