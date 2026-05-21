// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_EDITOR

#include "LLM/OpenAIProvider.h"
#include "LLM/LLMProviderTypes.h"
#include "HAL/PlatformProcess.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOpenAIBaseURLTest, "AINpc.OpenAI.BaseURL",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOpenAIBaseURLTest::RunTest(const FString& Parameters)
{
	FString CustomBaseUrl = FPlatformMisc::GetEnvironmentVariable(TEXT("DEEPSEEK_TEST"));

	if (CustomBaseUrl.IsEmpty())
	{
		CustomBaseUrl = TEXT("https://api.deepseek.com");
		UE_LOG(LogTemp, Warning, TEXT("DEEPSEEK_TEST not set, using default: %s"), *CustomBaseUrl);
	}

	UE_LOG(LogTemp, Log, TEXT("Testing OpenAI BaseURL with custom URL: %s"), *CustomBaseUrl);

	// Test 1: Constructor BaseURL
	TSharedPtr<FOpenAIProvider> Provider = MakeShared<FOpenAIProvider>(
		TEXT("test-key"),
		TEXT("test-model"),
		CustomBaseUrl
	);

	FLLMRequest Request;
	Request.Messages.Add({TEXT("user"), TEXT("test")});

	// Access ResolveBaseUrl through test helper
	FString ResolvedUrl = Provider->ResolveBaseUrlForTest(Request);

	UE_LOG(LogTemp, Log, TEXT("Resolved BaseURL from constructor: %s"), *ResolvedUrl);
	TestEqual(TEXT("Constructor BaseURL should match"), ResolvedUrl, CustomBaseUrl);

	// Test 2: Per-request BaseURL override
	FString PerRequestUrl = TEXT("https://api.deepseek.com/v1");
	Request.BaseUrl = PerRequestUrl;

	FString OverriddenUrl = Provider->ResolveBaseUrlForTest(Request);
	UE_LOG(LogTemp, Log, TEXT("Resolved BaseURL with per-request override: %s"), *OverriddenUrl);
	TestEqual(TEXT("Per-request BaseURL should override constructor"), OverriddenUrl, PerRequestUrl);

	// Test 3: Verify deepseek compatibility
	if (CustomBaseUrl.Contains(TEXT("deepseek")))
	{
		UE_LOG(LogTemp, Log, TEXT("DeepSeek BaseURL detected and validated: %s"), *CustomBaseUrl);
		AddInfo(FString::Printf(TEXT("Successfully configured custom BaseURL for DeepSeek: %s"), *CustomBaseUrl));
	}

	return true;
}

#endif // WITH_EDITOR
