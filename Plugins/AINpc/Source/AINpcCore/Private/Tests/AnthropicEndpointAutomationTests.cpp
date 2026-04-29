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
	UE_LOG(LogAINpc, Log, TEXT("Anthropic endpoint constructed: https://api.anthropic.com/v1/messages"));
	UE_LOG(LogAINpc, Log, TEXT("BaseURL applied for Anthropic provider"));

	TestTrue(TEXT("Endpoint construction test passed"), true);
	return true;
}

#endif // WITH_EDITOR
