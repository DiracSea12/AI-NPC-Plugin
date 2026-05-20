#include "LLM/AnthropicProvider.h"
#include "AINpcCoreLog.h"
#include "Misc/AutomationTest.h"

#if WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAnthropicEndpointConstructionTest,
	"AINpc.Anthropic.EndpointConstruction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnthropicEndpointConstructionTest::RunTest(const FString& Parameters)
{
	FLLMRequest Request;

	{
		const TSharedRef<FAnthropicProvider> Provider =
			MakeShared<FAnthropicProvider>(TEXT("test-key"), TEXT("test-model"), TEXT("https://api.anthropic.com/v1"));
		TestEqual(
			TEXT("Anthropic /v1 base URL should append /messages."),
			Provider->ResolveMessagesEndpointForTest(Request),
			FString(TEXT("https://api.anthropic.com/v1/messages")));
	}

	{
		const TSharedRef<FAnthropicProvider> Provider =
			MakeShared<FAnthropicProvider>(TEXT("test-key"), TEXT("test-model"), TEXT("https://code.example.test/claude/aws"));
		TestEqual(
			TEXT("Anthropic base URL without /v1 should normalize to /v1/messages."),
			Provider->ResolveMessagesEndpointForTest(Request),
			FString(TEXT("https://code.example.test/claude/aws/v1/messages")));
	}

	{
		const TSharedRef<FAnthropicProvider> Provider =
			MakeShared<FAnthropicProvider>(TEXT("test-key"), TEXT("test-model"), TEXT("https://code.example.test/claude/aws/v1/messages"));
		TestEqual(
			TEXT("Anthropic messages endpoint URL should be used as-is."),
			Provider->ResolveMessagesEndpointForTest(Request),
			FString(TEXT("https://code.example.test/claude/aws/v1/messages")));
	}

	Request.BaseUrl = TEXT("  https://override.example.test/anthropic  ");
	const TSharedRef<FAnthropicProvider> Provider =
		MakeShared<FAnthropicProvider>(TEXT("test-key"), TEXT("test-model"), TEXT("https://api.anthropic.com/v1"));
	TestEqual(
		TEXT("Per-request Anthropic base URL override should normalize to /v1/messages."),
		Provider->ResolveMessagesEndpointForTest(Request),
		FString(TEXT("https://override.example.test/anthropic/v1/messages")));

	return true;
}

#endif // WITH_EDITOR
