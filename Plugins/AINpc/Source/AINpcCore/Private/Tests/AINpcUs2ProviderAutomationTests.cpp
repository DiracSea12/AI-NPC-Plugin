#include "Misc/AutomationTest.h"

#if WITH_EDITOR

#include "Components/AINpcProviderConfigResolver.h"
#include "Components/AINpcComponent.h"
#include "Dom/JsonObject.h"
#include "HAL/Event.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "LLM/AnthropicProvider.h"
#include "LLM/ILLMProvider.h"
#include "LLM/LLMProviderChain.h"
#include "LLM/LocalProvider.h"
#include "LLM/OllamaProvider.h"
#include "LLM/OpenAIProvider.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
bool DeserializeJsonObjectForProviderTest(const FString& JsonText, TSharedPtr<FJsonObject>& OutObject)
{
	const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonText);
	return FJsonSerializer::Deserialize(JsonReader, OutObject) && OutObject.IsValid();
}

bool TryGetBoolFieldForProviderTest(const FString& JsonText, const TCHAR* FieldName, bool& bOutValue)
{
	TSharedPtr<FJsonObject> JsonObject;
	return DeserializeJsonObjectForProviderTest(JsonText, JsonObject) &&
		JsonObject.IsValid() &&
		JsonObject->TryGetBoolField(FieldName, bOutValue);
}

bool HasObjectFieldForProviderTest(const FString& JsonText, const TCHAR* FieldName)
{
	TSharedPtr<FJsonObject> JsonObject;
	const TSharedPtr<FJsonObject>* ObjectField = nullptr;
	return DeserializeJsonObjectForProviderTest(JsonText, JsonObject) &&
		JsonObject.IsValid() &&
		JsonObject->TryGetObjectField(FieldName, ObjectField) &&
		ObjectField &&
		ObjectField->IsValid();
}

bool HasArrayFieldForProviderTest(const FString& JsonText, const TCHAR* FieldName)
{
	TSharedPtr<FJsonObject> JsonObject;
	const TArray<TSharedPtr<FJsonValue>>* ArrayField = nullptr;
	return DeserializeJsonObjectForProviderTest(JsonText, JsonObject) &&
		JsonObject.IsValid() &&
		JsonObject->TryGetArrayField(FieldName, ArrayField) &&
		ArrayField &&
		ArrayField->Num() > 0;
}

class FScopedEnvironmentVariableOverrideForProviderTest
{
public:
	FScopedEnvironmentVariableOverrideForProviderTest(const TCHAR* InName, const FString& InValue)
		: Name(InName)
		, OriginalValue(FPlatformMisc::GetEnvironmentVariable(InName))
	{
		FPlatformMisc::SetEnvironmentVar(Name, *InValue);
	}

	~FScopedEnvironmentVariableOverrideForProviderTest()
	{
		FPlatformMisc::SetEnvironmentVar(Name, OriginalValue.IsEmpty() ? nullptr : *OriginalValue);
	}

private:
	const TCHAR* Name = nullptr;
	FString OriginalValue;
};

class FFailingProviderForUs2Test final : public ILLMProvider
{
public:
	virtual FGuid SendRequest(const FLLMRequest& Request, FLLMResponseCallback CompletionCallback) override
	{
		(void)Request;

		++SendCount;
		LastRequestId = FGuid::NewGuid();

		FLLMResponse Response;
		Response.RequestId = LastRequestId;
		Response.bSuccess = false;
		Response.HttpStatusCode = 503;
		Response.ErrorMessage = TEXT("synthetic provider failure");
		CompletionCallback(Response);
		return LastRequestId;
	}

	virtual bool CancelRequest(const FGuid& RequestId) override
	{
		return RequestId == LastRequestId;
	}

	virtual FLLMProviderCapabilities GetCapabilities() const override
	{
		FLLMProviderCapabilities Capabilities;
		Capabilities.bSupportsFunctionCalling = true;
		Capabilities.bSupportsJsonMode = true;
		return Capabilities;
	}

	int32 SendCount = 0;
	FGuid LastRequestId;
};

class FStreamingProviderForUs2Test final : public ILLMProvider
{
public:
	virtual FGuid SendRequest(const FLLMRequest& Request, FLLMResponseCallback CompletionCallback) override
	{
		++SendCount;
		LastRequestId = FGuid::NewGuid();
		CapturedStreamCallback = Request.StreamCallback;
		CapturedCompletionCallback = MoveTemp(CompletionCallback);
		return LastRequestId;
	}

	virtual bool CancelRequest(const FGuid& RequestId) override
	{
		if (RequestId != LastRequestId)
		{
			return false;
		}

		++CancelCount;
		CapturedStreamCallback = nullptr;
		CapturedCompletionCallback = nullptr;
		return true;
	}

	virtual FLLMProviderCapabilities GetCapabilities() const override
	{
		FLLMProviderCapabilities Capabilities;
		Capabilities.bSupportsStreaming = true;
		Capabilities.bSupportsFunctionCalling = true;
		Capabilities.bSupportsJsonMode = true;
		return Capabilities;
	}

	void EmitChunk(const FString& Content, const bool bFinal = false, const bool bError = false)
	{
		if (!CapturedStreamCallback)
		{
			return;
		}

		FLLMStreamChunk Chunk;
		Chunk.RequestId = LastRequestId;
		Chunk.Content = Content;
		Chunk.ErrorMessage = bError ? TEXT("synthetic stream error") : FString();
		Chunk.bIsFinal = bFinal;
		Chunk.bIsError = bError;
		CapturedStreamCallback(Chunk);
	}

	void CompleteSuccess()
	{
		if (!CapturedCompletionCallback)
		{
			return;
		}

		FLLMResponse Response;
		Response.RequestId = LastRequestId;
		Response.bSuccess = true;
		Response.HttpStatusCode = 200;
		Response.Content = TEXT("complete");
		Response.ParsedResponse.Dialogue = Response.Content;
		Response.ParsedResponse.ParseTier = ELLMResponseParseTier::PlainText;
		CapturedCompletionCallback(Response);
	}

	int32 SendCount = 0;
	int32 CancelCount = 0;
	FGuid LastRequestId;

private:
	FLLMStreamCallback CapturedStreamCallback;
	FLLMResponseCallback CapturedCompletionCallback;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2ProviderResolverRejectsMissingJsonConfigTest,
	"AINpc.US2.Provider.ResolverRejectsMissingJsonConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2ProviderResolverRejectsMissingJsonConfigTest::RunTest(const FString& Parameters)
{
	FAINpcProviderBootstrapConfig Config;
	const bool bResolved = FAINpcProviderConfigResolver::TryResolveBootstrapConfigFromJsonTextForTest(TEXT(""), Config);
	TestFalse(TEXT("Missing provider JSON must not resolve a provider source."), bResolved);
	TestFalse(TEXT("Missing provider JSON must leave source config unavailable."), Config.bHasProviderSourceConfig);
	TestFalse(TEXT("Missing provider JSON must not create a primary provider."), FAINpcProviderConfigResolver::CreateProviderForConfigForTest(Config).IsValid());
	TestTrue(TEXT("Missing provider JSON should expose a diagnosable source error."), Config.ProviderSourceError.Contains(TEXT("missing"), ESearchCase::IgnoreCase));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2ProviderResolverRejectsMissingCloudFieldsTest,
	"AINpc.US2.Provider.ResolverRejectsMissingCloudFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2ProviderResolverRejectsMissingCloudFieldsTest::RunTest(const FString& Parameters)
{
	struct FMissingFieldCase
	{
		const TCHAR* JsonText;
		const TCHAR* ExpectedFieldName;
	};

	const FMissingFieldCase Cases[] =
	{
		{ TEXT("{\"baseUrl\":\"https://provider.example.invalid/v1\",\"model\":\"test-model\",\"apiKey\":\"test-key\"}"), TEXT("provider") },
		{ TEXT("{\"provider\":\"openai\",\"model\":\"test-model\",\"apiKey\":\"test-key\"}"), TEXT("baseUrl") },
		{ TEXT("{\"provider\":\"openai\",\"baseUrl\":\"https://provider.example.invalid/v1\",\"apiKey\":\"test-key\"}"), TEXT("model") },
		{ TEXT("{\"provider\":\"openai\",\"baseUrl\":\"https://provider.example.invalid/v1\",\"model\":\"test-model\"}"), TEXT("apiKey") },
		{ TEXT("{\"provider\":\"local\",\"model\":\"test-model\"}"), TEXT("baseUrl") },
		{ TEXT("{\"provider\":\"ollama\",\"model\":\"test-model\"}"), TEXT("baseUrl") }
	};

	for (const FMissingFieldCase& TestCase : Cases)
	{
		FAINpcProviderBootstrapConfig Config;
		const bool bResolved = FAINpcProviderConfigResolver::TryResolveBootstrapConfigFromJsonTextForTest(TestCase.JsonText, Config);
		TestFalse(FString::Printf(TEXT("Provider JSON missing %s must not resolve."), TestCase.ExpectedFieldName), bResolved);
		TestFalse(FString::Printf(TEXT("Provider JSON missing %s must not create provider."), TestCase.ExpectedFieldName), FAINpcProviderConfigResolver::CreateProviderForConfigForTest(Config).IsValid());
		TestTrue(FString::Printf(TEXT("Source error should identify missing %s."), TestCase.ExpectedFieldName), Config.ProviderSourceError.Contains(TestCase.ExpectedFieldName, ESearchCase::CaseSensitive));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2ProviderResolverUsesExplicitJsonOnlySourceTest,
	"AINpc.US2.Provider.ResolverUsesExplicitJsonOnlySource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2ProviderResolverUsesExplicitJsonOnlySourceTest::RunTest(const FString& Parameters)
{
	FScopedEnvironmentVariableOverrideForProviderTest ScopedOpenAIEnv(TEXT("AINPC_OPENAI_API_KEY"), TEXT("env-key-that-must-not-be-used"));
	FScopedEnvironmentVariableOverrideForProviderTest ScopedAnthropicEnv(TEXT("ANTHROPIC_AUTH_TOKEN"), TEXT("env-key-that-must-not-be-used"));

	FAINpcProviderBootstrapConfig Config;
	const bool bResolved = FAINpcProviderConfigResolver::TryResolveBootstrapConfigFromJsonTextForTest(
		TEXT("{\"provider\":\"openai\",\"apiKey\":\"json-key\",\"baseUrl\":\"https://json.example.invalid/v1\",\"model\":\"json-model\",\"effortLevel\":\"MEDIUM\"}"),
		Config);
	TestTrue(TEXT("Complete cloud provider JSON should resolve."), bResolved);
	TestTrue(TEXT("Complete cloud provider JSON should mark provider source present."), Config.bHasProviderSourceConfig);
	TestEqual(TEXT("Provider type should come from JSON."), Config.ProviderType, FString(TEXT("openai")));
	TestEqual(TEXT("API key should come from JSON, not environment."), Config.ApiKey, FString(TEXT("json-key")));
	TestEqual(TEXT("BaseUrl should come from JSON."), Config.BaseUrl, FString(TEXT("https://json.example.invalid/v1")));
	TestEqual(TEXT("Model should come from JSON."), Config.Model, FString(TEXT("json-model")));
	TestEqual(TEXT("Effort level should be normalized from JSON."), Config.EffortLevel, FString(TEXT("medium")));

	const auto Provider = FAINpcProviderConfigResolver::CreateProviderForConfigForTest(Config);
	TestTrue(TEXT("Complete cloud provider JSON should create a provider."), Provider.IsValid());

	const UAINpcComponent* Component = NewObject<UAINpcComponent>();
	TestNotNull(TEXT("Component should be available for explicit config request application."), Component);
	if (!Component)
	{
		return false;
	}

	FLLMRequest Request;
	FAINpcProviderConfigResolver::ApplyRequestConfigForConfigForTest(*Component, Config, Request);
	TestEqual(TEXT("Request API key should come from explicit JSON config."), Request.ApiKey, FString(TEXT("json-key")));
	TestEqual(TEXT("Request BaseUrl should come from explicit JSON config."), Request.BaseUrl, FString(TEXT("https://json.example.invalid/v1")));
	TestEqual(TEXT("Request model should come from explicit JSON config."), Request.Model, FString(TEXT("json-model")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2ProviderResolverRejectsImplicitCustomTest,
	"AINpc.US2.Provider.ResolverRejectsImplicitCustom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2ProviderResolverRejectsImplicitCustomTest::RunTest(const FString& Parameters)
{
	FAINpcProviderBootstrapConfig Config;
	Config.bHasProviderSourceConfig = true;
	Config.ProviderType = TEXT("custom");
	Config.BaseUrl = TEXT("https://custom.example.invalid/v1");
	Config.Model = TEXT("custom-model");
	Config.ApiKey = TEXT("custom-key");

	const auto Provider = FAINpcProviderConfigResolver::CreateProviderForConfigForTest(Config);
	TestFalse(TEXT("Custom provider without explicit protocol seam must not silently become OpenAI."), Provider.IsValid());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2ProviderResolverSelectsCustomOpenAICompatibleTest,
	"AINpc.US2.Provider.ResolverSelectsCustomOpenAICompatible",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2ProviderResolverSelectsCustomOpenAICompatibleTest::RunTest(const FString& Parameters)
{
	FAINpcProviderBootstrapConfig Config;
	const bool bResolved = FAINpcProviderConfigResolver::TryResolveBootstrapConfigFromJsonTextForTest(
		TEXT("{\"provider\":\"custom\",\"protocol\":\"openai-compatible\",\"apiKey\":\"custom-key\",\"baseUrl\":\"https://custom.example.invalid/v1\",\"model\":\"custom-model\"}"),
		Config);
	TestTrue(TEXT("Explicit custom/openai-compatible JSON config should resolve."), bResolved);
	TestEqual(TEXT("Custom provider protocol should come from JSON."), Config.Protocol, FString(TEXT("openai-compatible")));

	const auto Provider = FAINpcProviderConfigResolver::CreateProviderForConfigForTest(Config);
	TestTrue(TEXT("Explicit custom/openai-compatible config should create a provider."), Provider.IsValid());
	TestTrue(TEXT("Custom OpenAI-compatible provider should report function calling."), Provider.IsValid() && Provider->GetCapabilities().bSupportsFunctionCalling);
	TestTrue(TEXT("Custom OpenAI-compatible provider should report JSON mode."), Provider.IsValid() && Provider->GetCapabilities().bSupportsJsonMode);
	TestTrue(TEXT("Custom OpenAI-compatible provider should report streaming."), Provider.IsValid() && Provider->GetCapabilities().bSupportsStreaming);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2ProviderResolverRejectsJsonFallbackSourceTest,
	"AINpc.US2.Provider.ResolverRejectsJsonFallbackSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2ProviderResolverRejectsJsonFallbackSourceTest::RunTest(const FString& Parameters)
{
	FAINpcProviderBootstrapConfig Config;
	const bool bResolved = FAINpcProviderConfigResolver::TryResolveBootstrapConfigFromJsonTextForTest(
		TEXT("{\"provider\":\"openai\",\"apiKey\":\"primary-key\",\"baseUrl\":\"https://primary.example.invalid/v1\",\"model\":\"primary-model\",")
		TEXT("\"fallback\":{\"provider\":\"local\",\"baseUrl\":\"http://localhost:11434/v1\",\"model\":\"llama3.2\"}}"),
		Config);
	TestFalse(TEXT("Provider JSON with fallback object must not resolve."), bResolved);
	TestFalse(TEXT("Rejected fallback config must not leave primary source available for partial use."), Config.bHasProviderSourceConfig);
	TestTrue(TEXT("Rejected fallback config should name the unsupported fallback field."), Config.ProviderSourceError.Contains(TEXT("fallback"), ESearchCase::CaseSensitive));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2ProviderResolverSelectsLocalTest,
	"AINpc.US2.Provider.ResolverSelectsLocal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2ProviderResolverSelectsLocalTest::RunTest(const FString& Parameters)
{
	FAINpcProviderBootstrapConfig Config;
	const bool bResolved = FAINpcProviderConfigResolver::TryResolveBootstrapConfigFromJsonTextForTest(
		TEXT("{\"provider\":\"local\",\"baseUrl\":\"http://localhost:11434/v1\",\"model\":\"llama3.2\"}"),
		Config);
	TestTrue(TEXT("Local provider JSON should resolve with explicit endpoint."), bResolved);
	TestEqual(TEXT("Local provider endpoint should come from JSON."), Config.BaseUrl, FString(TEXT("http://localhost:11434/v1")));
	TestTrue(TEXT("Local provider exception should not require API key."), Config.ApiKey.IsEmpty());

	const auto Provider = FAINpcProviderConfigResolver::CreateProviderForConfigForTest(Config);
	TestTrue(TEXT("Resolver should create a Local provider when provider is local."), Provider.IsValid());
	TestTrue(TEXT("Local provider should expose non-streaming capability."), Provider.IsValid() && !Provider->GetCapabilities().bSupportsStreaming);
	TestTrue(TEXT("Local provider should expose function-calling capability."), Provider.IsValid() && Provider->GetCapabilities().bSupportsFunctionCalling);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2ProviderCapabilityMatrixTest,
	"AINpc.US2.Provider.CapabilityMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2ProviderCapabilityMatrixTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FOpenAIProvider> OpenAIProvider =
		MakeShared<FOpenAIProvider>(TEXT("test-key"), TEXT("gpt-test"), TEXT("https://api.openai.com/v1"));
	const TSharedRef<FAnthropicProvider> AnthropicProvider =
		MakeShared<FAnthropicProvider>(TEXT("test-key"), TEXT("claude-test"), TEXT("https://api.anthropic.com/v1"));
	const TSharedRef<FLocalProvider> LocalProvider =
		MakeShared<FLocalProvider>(TEXT("llama3.2"), TEXT("http://localhost:11434/v1"));
	const TSharedRef<FOllamaProvider> OllamaProvider =
		MakeShared<FOllamaProvider>(TEXT("llama3.2"), TEXT("http://localhost:11434/v1"));

	const FLLMProviderCapabilities OpenAICapabilities = OpenAIProvider->GetCapabilities();
	TestTrue(TEXT("OpenAI-compatible provider should support streaming."), OpenAICapabilities.bSupportsStreaming);
	TestTrue(TEXT("OpenAI-compatible provider should support function calling."), OpenAICapabilities.bSupportsFunctionCalling);
	TestTrue(TEXT("OpenAI-compatible provider should support JSON mode."), OpenAICapabilities.bSupportsJsonMode);
	TestFalse(TEXT("OpenAI-compatible provider should not claim Anthropic tool calling."), OpenAICapabilities.bSupportsToolCalling);

	const FLLMProviderCapabilities AnthropicCapabilities = AnthropicProvider->GetCapabilities();
	TestTrue(TEXT("Anthropic provider should support streaming."), AnthropicCapabilities.bSupportsStreaming);
	TestFalse(TEXT("Anthropic provider should not claim OpenAI function calling."), AnthropicCapabilities.bSupportsFunctionCalling);
	TestFalse(TEXT("Anthropic provider should not claim OpenAI JSON mode."), AnthropicCapabilities.bSupportsJsonMode);
	TestTrue(TEXT("Anthropic provider should support tool calling."), AnthropicCapabilities.bSupportsToolCalling);

	const FLLMProviderCapabilities LocalCapabilities = LocalProvider->GetCapabilities();
	TestFalse(TEXT("Local provider should not claim streaming."), LocalCapabilities.bSupportsStreaming);
	TestTrue(TEXT("Local provider should support function calling."), LocalCapabilities.bSupportsFunctionCalling);
	TestTrue(TEXT("Local provider should support JSON mode."), LocalCapabilities.bSupportsJsonMode);
	TestFalse(TEXT("Local provider should not claim Anthropic tool calling."), LocalCapabilities.bSupportsToolCalling);

	const FLLMProviderCapabilities OllamaCapabilities = OllamaProvider->GetCapabilities();
	TestFalse(TEXT("Ollama provider should not claim streaming."), OllamaCapabilities.bSupportsStreaming);
	TestTrue(TEXT("Ollama provider should support function calling."), OllamaCapabilities.bSupportsFunctionCalling);
	TestTrue(TEXT("Ollama provider should support JSON mode."), OllamaCapabilities.bSupportsJsonMode);
	TestFalse(TEXT("Ollama provider should not claim Anthropic tool calling."), OllamaCapabilities.bSupportsToolCalling);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2ProviderResolverSelectsOllamaTest,
	"AINpc.US2.Provider.ResolverSelectsOllama",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2ProviderResolverSelectsOllamaTest::RunTest(const FString& Parameters)
{
	FAINpcProviderBootstrapConfig Config;
	const bool bResolved = FAINpcProviderConfigResolver::TryResolveBootstrapConfigFromJsonTextForTest(
		TEXT("{\"provider\":\"ollama\",\"baseUrl\":\"http://localhost:11434/v1\",\"model\":\"llama3.2\"}"),
		Config);
	TestTrue(TEXT("Ollama provider JSON should resolve with explicit endpoint."), bResolved);
	TestEqual(TEXT("Ollama provider endpoint should come from JSON."), Config.BaseUrl, FString(TEXT("http://localhost:11434/v1")));
	TestTrue(TEXT("Ollama provider exception should not require API key."), Config.ApiKey.IsEmpty());

	const auto Provider = FAINpcProviderConfigResolver::CreateProviderForConfigForTest(Config);
	TestTrue(TEXT("Resolver should create an Ollama provider when provider is ollama."), Provider.IsValid());
	TestTrue(TEXT("Ollama provider should expose non-streaming capability."), Provider.IsValid() && !Provider->GetCapabilities().bSupportsStreaming);
	TestTrue(TEXT("Ollama provider should expose JSON mode capability."), Provider.IsValid() && Provider->GetCapabilities().bSupportsJsonMode);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2ProviderStrongestStructuredModeShapeTest,
	"AINpc.US2.Provider.StrongestStructuredModeShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2ProviderStrongestStructuredModeShapeTest::RunTest(const FString& Parameters)
{
	FLLMRequest Request;
	Request.Messages.Add({TEXT("user"), TEXT("Respond with structured NPC output.")});

	const TSharedRef<FOpenAIProvider> OpenAIProvider =
		MakeShared<FOpenAIProvider>(TEXT("test-key"), TEXT("gpt-test"), TEXT("https://api.openai.com/v1"));
	const FString OpenAIRequestBody = OpenAIProvider->BuildRequestBodyForTest(Request);
	TestTrue(TEXT("OpenAI-compatible request should select function tools as strongest mode."), HasArrayFieldForProviderTest(OpenAIRequestBody, TEXT("tools")));
	TestTrue(TEXT("OpenAI-compatible request should pin tool_choice when function tools are available."), HasObjectFieldForProviderTest(OpenAIRequestBody, TEXT("tool_choice")));
	TestFalse(TEXT("OpenAI-compatible request must not also set response_format when function tools are selected."), HasObjectFieldForProviderTest(OpenAIRequestBody, TEXT("response_format")));

	const TSharedRef<FAnthropicProvider> AnthropicProvider =
		MakeShared<FAnthropicProvider>(TEXT("test-key"), TEXT("claude-test"), TEXT("https://api.anthropic.com/v1"));
	const FString AnthropicRequestBody = AnthropicProvider->BuildRequestBodyForTest(Request);
	TestTrue(TEXT("Anthropic request should select tool-use as strongest mode."), HasArrayFieldForProviderTest(AnthropicRequestBody, TEXT("tools")));
	TestFalse(TEXT("Anthropic request must not emit OpenAI response_format."), HasObjectFieldForProviderTest(AnthropicRequestBody, TEXT("response_format")));

	const TSharedRef<FLocalProvider> LocalProvider =
		MakeShared<FLocalProvider>(TEXT("llama3.2"), TEXT("http://localhost:11434/v1"));
	const FString LocalRequestBody = LocalProvider->BuildRequestBodyForTest(Request);
	TestTrue(TEXT("Local request should select function tools because its capabilities declare them."), HasArrayFieldForProviderTest(LocalRequestBody, TEXT("tools")));
	TestTrue(TEXT("Local request should pin tool_choice when function tools are available."), HasObjectFieldForProviderTest(LocalRequestBody, TEXT("tool_choice")));
	TestFalse(TEXT("Local request must not also set response_format when function tools are selected."), HasObjectFieldForProviderTest(LocalRequestBody, TEXT("response_format")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2OpenAICompatibleDeepSeekRequestShapeTest,
	"AINpc.US2.Provider.OpenAICompatibleDeepSeekRequestShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2OpenAICompatibleDeepSeekRequestShapeTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FOpenAIProvider> Provider =
		MakeShared<FOpenAIProvider>(TEXT("test-key"), TEXT("deepseek-chat"), TEXT("https://api.deepseek.com/v1"));

	FLLMRequest Request;
	Request.Model = TEXT("deepseek-chat");
	Request.Messages.Add({TEXT("user"), TEXT("Respond with structured NPC output.")});

	TestEqual(
		TEXT("DeepSeek-compatible base URL should stay on OpenAI-compatible provider path."),
		Provider->ResolveBaseUrlForTest(Request),
		FString(TEXT("https://api.deepseek.com/v1")));

	const FString RequestBody = Provider->BuildRequestBodyForTest(Request);
	TSharedPtr<FJsonObject> RequestJson;
	TestTrue(TEXT("OpenAI-compatible request body should deserialize."), DeserializeJsonObjectForProviderTest(RequestBody, RequestJson));
	if (!RequestJson.IsValid())
	{
		return false;
	}

	FString Model;
	TestTrue(TEXT("OpenAI-compatible request should include a model field."), RequestJson->TryGetStringField(TEXT("model"), Model));
	TestEqual(TEXT("OpenAI-compatible request should include the configured DeepSeek model."), Model, FString(TEXT("deepseek-chat")));

	const TArray<TSharedPtr<FJsonValue>>* ToolsArray = nullptr;
	TestTrue(TEXT("OpenAI-compatible request should include structured output tools."), RequestJson->TryGetArrayField(TEXT("tools"), ToolsArray) && ToolsArray && ToolsArray->Num() > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2ProviderStreamingCapabilityGateTest,
	"AINpc.US2.Provider.StreamingCapabilityGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2ProviderStreamingCapabilityGateTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FOpenAIProvider> OpenAIProvider =
		MakeShared<FOpenAIProvider>(TEXT("test-key"), TEXT("gpt-test"), TEXT("https://api.openai.com/v1"));
	const TSharedRef<FLocalProvider> LocalProvider =
		MakeShared<FLocalProvider>(TEXT("llama3.2"), TEXT("http://localhost:11434/v1"));

	FLLMRequest OpenAIRequest;
	OpenAIRequest.Messages.Add({TEXT("user"), TEXT("stream this")});
	if (OpenAIProvider->GetCapabilities().bSupportsStreaming)
	{
		OpenAIRequest.bUseStreaming = true;
	}

	FLLMRequest LocalRequest;
	LocalRequest.Messages.Add({TEXT("user"), TEXT("do not stream this")});
	if (LocalProvider->GetCapabilities().bSupportsStreaming)
	{
		LocalRequest.bUseStreaming = true;
	}

	bool bOpenAIStreamField = false;
	bool bLocalStreamField = true;
	TestTrue(TEXT("OpenAI request should serialize stream when capability gate enables it."),
		TryGetBoolFieldForProviderTest(OpenAIProvider->BuildRequestBodyForTest(OpenAIRequest), TEXT("stream"), bOpenAIStreamField) && bOpenAIStreamField);
	TestFalse(TEXT("Local request must not serialize stream when capability gate disables it."),
		TryGetBoolFieldForProviderTest(LocalProvider->BuildRequestBodyForTest(LocalRequest), TEXT("stream"), bLocalStreamField));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2ProviderChainTemplateFallbackResponseTest,
	"AINpc.US2.Provider.ChainTemplateFallbackResponse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2ProviderChainTemplateFallbackResponseTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FFailingProviderForUs2Test> PrimaryProvider = MakeShared<FFailingProviderForUs2Test>();
	const TSharedRef<FFailingProviderForUs2Test> FallbackProvider = MakeShared<FFailingProviderForUs2Test>();
	const TSharedRef<FLLMProviderChain> ProviderChain =
		MakeShared<FLLMProviderChain>(
			PrimaryProvider,
			FallbackProvider,
			TArray<FString>{TEXT("Template fallback line.")},
			0,
			0.0f);

	FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool(false);
	FLLMResponse ObservedResponse;

	FLLMRequest Request;
	Request.Messages.Add({TEXT("user"), TEXT("force fallback")});
	const FGuid ChainRequestId = ProviderChain->SendRequest(
		Request,
		[&ObservedResponse, CompletionEvent](const FLLMResponse& Response)
		{
			ObservedResponse = Response;
			CompletionEvent->Trigger();
		});

	const bool bCompleted = CompletionEvent->Wait(FTimespan::FromSeconds(2.0));
	FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);

	TestTrue(TEXT("Provider chain should complete through template fallback."), bCompleted);
	TestTrue(TEXT("Chain request id should be valid."), ChainRequestId.IsValid());
	TestEqual(TEXT("Primary provider should be called once."), PrimaryProvider->SendCount, 1);
	TestEqual(TEXT("Fallback provider should be called once."), FallbackProvider->SendCount, 1);
	TestTrue(TEXT("Template fallback should be reported as success."), ObservedResponse.bSuccess);
	TestTrue(TEXT("Template fallback should mark the response as fallback."), ObservedResponse.bIsFallback);
	TestEqual(TEXT("Template fallback should preserve the chain request id."), ObservedResponse.RequestId, ChainRequestId);
	TestEqual(TEXT("Template fallback should use the configured response."), ObservedResponse.Content, FString(TEXT("Template fallback line.")));
	TestEqual(TEXT("Template fallback should preserve dialogue as plain text."), ObservedResponse.ParsedResponse.Dialogue, FString(TEXT("Template fallback line.")));
	TestEqual(TEXT("Template fallback should record the plain-text parse tier."), ObservedResponse.ParsedResponse.ParseTier, ELLMResponseParseTier::PlainText);
	TestEqual(TEXT("Template fallback must not create executable actions."), ObservedResponse.ParsedResponse.Actions.Num(), 0);
	TestFalse(TEXT("Completed chain request should not remain cancellable as an active request."), ProviderChain->CancelRequest(ChainRequestId));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2ProviderChainNoTemplateFailureTest,
	"AINpc.US2.Provider.ChainNoTemplateFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2ProviderChainNoTemplateFailureTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FFailingProviderForUs2Test> PrimaryProvider = MakeShared<FFailingProviderForUs2Test>();
	const TSharedRef<FFailingProviderForUs2Test> FallbackProvider = MakeShared<FFailingProviderForUs2Test>();
	const TSharedRef<FLLMProviderChain> ProviderChain =
		MakeShared<FLLMProviderChain>(
			PrimaryProvider,
			FallbackProvider,
			TArray<FString>(),
			0,
			0.0f);

	FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool(false);
	FLLMResponse ObservedResponse;

	FLLMRequest Request;
	Request.Messages.Add({TEXT("user"), TEXT("force fallback without template")});
	const FGuid ChainRequestId = ProviderChain->SendRequest(
		Request,
		[&ObservedResponse, CompletionEvent](const FLLMResponse& Response)
		{
			ObservedResponse = Response;
			CompletionEvent->Trigger();
		});

	const bool bCompleted = CompletionEvent->Wait(FTimespan::FromSeconds(2.0));
	FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);

	TestTrue(TEXT("Provider chain should complete even without a fallback template."), bCompleted);
	TestEqual(TEXT("Primary provider should be called once."), PrimaryProvider->SendCount, 1);
	TestEqual(TEXT("Fallback provider should be called once."), FallbackProvider->SendCount, 1);
	TestFalse(TEXT("Missing template must not masquerade as a successful response."), ObservedResponse.bSuccess);
	TestTrue(TEXT("Missing template should mark the response as fallback/degradation."), ObservedResponse.bIsFallback);
	TestEqual(TEXT("Missing template should preserve the chain request id."), ObservedResponse.RequestId, ChainRequestId);
	TestEqual(TEXT("Missing template should report the no-template fallback reason."), ObservedResponse.FallbackReason, ELLMFallbackReason::NoTemplateAvailable);
	TestFalse(TEXT("Missing template should report a diagnosable error."), ObservedResponse.ErrorMessage.IsEmpty());
	TestTrue(TEXT("Missing-template chain request should not remain cancellable as an active request."), !ProviderChain->CancelRequest(ChainRequestId));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2ProviderChainMapsStreamingRequestIdsTest,
	"AINpc.US2.Provider.ChainMapsStreamingRequestIds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2ProviderChainMapsStreamingRequestIdsTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FStreamingProviderForUs2Test> PrimaryProvider = MakeShared<FStreamingProviderForUs2Test>();
	const TSharedRef<FLLMProviderChain> ProviderChain =
		MakeShared<FLLMProviderChain>(
			PrimaryProvider,
			nullptr,
			TArray<FString>(),
			0,
			0.0f);

	TArray<FLLMStreamChunk> ObservedChunks;
	FLLMResponse ObservedResponse;

	FLLMRequest Request;
	Request.bUseStreaming = true;
	Request.Messages.Add({TEXT("user"), TEXT("stream through chain")});
	Request.StreamCallback = [&ObservedChunks](const FLLMStreamChunk& Chunk)
	{
		ObservedChunks.Add(Chunk);
	};

	const FGuid ChainRequestId = ProviderChain->SendRequest(
		Request,
		[&ObservedResponse](const FLLMResponse& Response)
		{
			ObservedResponse = Response;
		});

	TestTrue(TEXT("Provider chain request id should be valid."), ChainRequestId.IsValid());
	TestEqual(TEXT("Primary streaming provider should be called once."), PrimaryProvider->SendCount, 1);
	TestNotEqual(TEXT("Synthetic provider request id should differ from chain request id."), PrimaryProvider->LastRequestId, ChainRequestId);

	PrimaryProvider->EmitChunk(TEXT("partial"));
	PrimaryProvider->EmitChunk(TEXT("partial-final"), true);
	PrimaryProvider->EmitChunk(TEXT("partial-error"), true, true);

	TestEqual(TEXT("Provider chain should forward provider chunks before completion."), ObservedChunks.Num(), 3);
	for (const FLLMStreamChunk& Chunk : ObservedChunks)
	{
		TestEqual(TEXT("Forwarded stream chunk must expose the chain request id."), Chunk.RequestId, ChainRequestId);
	}
	if (ObservedChunks.Num() == 3)
	{
		TestEqual(TEXT("Content stream chunk should preserve content."), ObservedChunks[0].Content, FString(TEXT("partial")));
		TestFalse(TEXT("Content stream chunk should remain non-final."), ObservedChunks[0].bIsFinal);
		TestTrue(TEXT("Final stream chunk should preserve final flag."), ObservedChunks[1].bIsFinal);
		TestTrue(TEXT("Error stream chunk should preserve error flag."), ObservedChunks[2].bIsError);
		TestFalse(TEXT("Error stream chunk should preserve diagnostic error text."), ObservedChunks[2].ErrorMessage.IsEmpty());
	}

	PrimaryProvider->CompleteSuccess();
	TestEqual(TEXT("Completion response must expose the chain request id."), ObservedResponse.RequestId, ChainRequestId);
	TestTrue(TEXT("Completion response should succeed."), ObservedResponse.bSuccess);

	PrimaryProvider->EmitChunk(TEXT("late"));
	TestEqual(TEXT("Provider chain must reject late chunks after completion."), ObservedChunks.Num(), 3);

	return true;
}

#endif
