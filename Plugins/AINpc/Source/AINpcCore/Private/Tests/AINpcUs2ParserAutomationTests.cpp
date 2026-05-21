#include "Misc/AutomationTest.h"

#include "LLM/LLMResponseParser.h"
#include "StateTree/Tasks/StateTreeTask_ExecuteSmartObject.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
void AssertUs2StructuredContract(
	FAutomationTestBase& Test,
	const FParsedLLMResponse& ParsedResponse,
	const ELLMResponseParseTier ExpectedTier,
	const FString& ExpectedDialogue)
{
	Test.TestEqual(TEXT("Parser should select the expected US-2 parse tier."), ParsedResponse.ParseTier, ExpectedTier);
	Test.TestEqual(TEXT("Parser should preserve US-2 dialogue text."), ParsedResponse.Dialogue, ExpectedDialogue);
	Test.TestTrue(TEXT("Structured parser tiers should report JSON-backed parsing."), ParsedResponse.bParsedAsJson);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2OpenAIToolCallsPreferStructuredArgumentsTest,
	"AINpc.US2.Parser.OpenAIToolCallsPreferStructuredArguments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2OpenAIToolCallsPreferStructuredArgumentsTest::RunTest(const FString& Parameters)
{
	const FString ResponseBody = TEXT(R"({"choices":[{"message":{"content":"{\"dialogue\":\"Wrong content JSON.\",\"actions\":[{\"type\":\"Action.Wave\",\"target\":\"wrong_target\"}],\"emotion_delta\":{\"valence\":-1.0,\"arousal\":-1.0,\"dominance\":-1.0},\"relationship_delta\":{\"affinity\":-1.0,\"trust\":-1.0,\"familiarity\":-1.0}}","tool_calls":[{"id":"call_1","type":"function","function":{"name":"emit_npc_response","arguments":"{\"dialogue\":\"Tool argument wins.\",\"actions\":[{\"type\":\"Action.Inspect\",\"target\":\"SO_SLOT_Tool\"}],\"emotion_delta\":{\"valence\":0.4,\"arousal\":0.2,\"dominance\":0.1},\"relationship_delta\":{\"affinity\":0.3,\"trust\":0.5,\"familiarity\":0.7}}"}}]}}]})");
	FParsedLLMResponse ParsedResponse;
	FString ErrorMessage;

	const bool bParsed = FLLMResponseParser::ParseOpenAIChatCompletion(ResponseBody, ParsedResponse, ErrorMessage);
	TestTrue(TEXT("OpenAI tool_calls payload should parse."), bParsed);
	TestTrue(TEXT("OpenAI tool_calls payload should not emit parser errors."), ErrorMessage.IsEmpty());
	AssertUs2StructuredContract(*this, ParsedResponse, ELLMResponseParseTier::FunctionCalling, TEXT("Tool argument wins."));
	TestEqual(TEXT("Tool argument action should win over content JSON action."), ParsedResponse.Actions.Num(), 1);
	if (ParsedResponse.Actions.Num() == 1)
	{
		TestEqual(TEXT("Tool argument action type should be used."), ParsedResponse.Actions[0].ActionType, FString(TEXT("Action.Inspect")));
		TestEqual(TEXT("Tool argument target should be used."), ParsedResponse.Actions[0].Target, FString(TEXT("SO_SLOT_Tool")));
	}
	TestEqual(TEXT("Tool argument emotion valence should be used."), ParsedResponse.EmotionDelta.Valence, 0.4f);
	TestEqual(TEXT("Tool argument relationship trust should be used."), ParsedResponse.RelationshipDelta.Trust, 0.5f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2OpenAIFunctionCallPrefersArgumentsTest,
	"AINpc.US2.Parser.OpenAIFunctionCallPrefersArguments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2OpenAIFunctionCallPrefersArgumentsTest::RunTest(const FString& Parameters)
{
	const FString ResponseBody = TEXT(R"({"choices":[{"message":{"content":"{\"dialogue\":\"Wrong function content.\",\"actions\":[{\"type\":\"Action.Wave\",\"target\":\"wrong_target\"}],\"emotion_delta\":{\"valence\":-1.0,\"arousal\":-1.0,\"dominance\":-1.0},\"relationship_delta\":{\"affinity\":-1.0,\"trust\":-1.0,\"familiarity\":-1.0}}","function_call":{"name":"emit_npc_response","arguments":"{\"dialogue\":\"Function argument wins.\",\"actions\":[{\"type\":\"Action.Use\",\"target\":\"SO_SLOT_Function\"}],\"emotion_delta\":{\"valence\":0.1,\"arousal\":0.3,\"dominance\":0.5},\"relationship_delta\":{\"affinity\":0.2,\"trust\":0.4,\"familiarity\":0.6}}"}}}]})");
	FParsedLLMResponse ParsedResponse;
	FString ErrorMessage;

	const bool bParsed = FLLMResponseParser::ParseOpenAIChatCompletion(ResponseBody, ParsedResponse, ErrorMessage);
	TestTrue(TEXT("OpenAI function_call payload should parse."), bParsed);
	TestTrue(TEXT("OpenAI function_call payload should not emit parser errors."), ErrorMessage.IsEmpty());
	AssertUs2StructuredContract(*this, ParsedResponse, ELLMResponseParseTier::FunctionCalling, TEXT("Function argument wins."));
	TestEqual(TEXT("Function argument action should win over content JSON action."), ParsedResponse.Actions.Num(), 1);
	if (ParsedResponse.Actions.Num() == 1)
	{
		TestEqual(TEXT("Function argument action type should be used."), ParsedResponse.Actions[0].ActionType, FString(TEXT("Action.Use")));
		TestEqual(TEXT("Function argument target should be used."), ParsedResponse.Actions[0].Target, FString(TEXT("SO_SLOT_Function")));
	}
	TestEqual(TEXT("Function argument emotion dominance should be used."), ParsedResponse.EmotionDelta.Dominance, 0.5f);
	TestEqual(TEXT("Function argument relationship familiarity should be used."), ParsedResponse.RelationshipDelta.Familiarity, 0.6f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2AnthropicToolUseNonStreamingParityTest,
	"AINpc.US2.Parser.AnthropicToolUseNonStreamingParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2AnthropicToolUseNonStreamingParityTest::RunTest(const FString& Parameters)
{
	const FString ResponseBody = TEXT(R"({"id":"msg_1","type":"message","role":"assistant","content":[{"type":"text","text":"Wrong text fallback."},{"type":"tool_use","id":"toolu_1","name":"emit_npc_response","input":{"dialogue":"Anthropic tool input wins.","actions":[{"type":"Action.Inspect","target":"SO_SLOT_Anthropic"}],"emotion_delta":{"valence":0.25,"arousal":0.5,"dominance":0.75},"relationship_delta":{"affinity":0.1,"trust":0.2,"familiarity":0.3}}}]})");
	FParsedLLMResponse ParsedResponse;
	FString ErrorMessage;

	const bool bParsed = FLLMResponseParser::ParseAnthropicMessages(ResponseBody, ParsedResponse, ErrorMessage);
	TestTrue(TEXT("Anthropic non-streaming tool_use payload should parse."), bParsed);
	TestTrue(TEXT("Anthropic non-streaming tool_use payload should not emit parser errors."), ErrorMessage.IsEmpty());
	AssertUs2StructuredContract(*this, ParsedResponse, ELLMResponseParseTier::FunctionCalling, TEXT("Anthropic tool input wins."));
	TestEqual(TEXT("Anthropic non-streaming tool_use should extract one action."), ParsedResponse.Actions.Num(), 1);
	if (ParsedResponse.Actions.Num() == 1)
	{
		TestEqual(TEXT("Anthropic non-streaming tool_use action type should be used."), ParsedResponse.Actions[0].ActionType, FString(TEXT("Action.Inspect")));
		TestEqual(TEXT("Anthropic non-streaming tool_use action target should be used."), ParsedResponse.Actions[0].Target, FString(TEXT("SO_SLOT_Anthropic")));
	}
	TestEqual(TEXT("Anthropic non-streaming tool_use emotion arousal should be used."), ParsedResponse.EmotionDelta.Arousal, 0.5f);
	TestEqual(TEXT("Anthropic non-streaming tool_use relationship affinity should be used."), ParsedResponse.RelationshipDelta.Affinity, 0.1f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2AnthropicStreamingToolUseParityTest,
	"AINpc.US2.Parser.AnthropicToolUseStreamingParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2AnthropicStreamingToolUseParityTest::RunTest(const FString& Parameters)
{
	const FString ResponseBody = TEXT(
		"event: message_start\n"
		"data: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_1\",\"type\":\"message\",\"role\":\"assistant\",\"content\":[]}}\n"
		"\n"
		"event: content_block_start\n"
		"data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"tool_use\",\"id\":\"toolu_1\",\"name\":\"emit_npc_response\",\"input\":{}}}\n"
		"\n"
		"event: content_block_delta\n"
		"data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"dialogue\\\":\\\"Anthropic streaming input wins.\\\",\\\"actions\\\":[{\\\"type\\\":\\\"Action.Use\\\",\\\"target\\\":\\\"SO_SLOT_Stream\\\"}],\"}}\n"
		"\n"
		"event: content_block_delta\n"
		"data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"\\\"emotion_delta\\\":{\\\"valence\\\":0.6,\\\"arousal\\\":0.4,\\\"dominance\\\":0.2},\\\"relationship_delta\\\":{\\\"affinity\\\":0.9,\\\"trust\\\":0.8,\\\"familiarity\\\":0.7}}\"}}\n"
		"\n"
		"event: content_block_stop\n"
		"data: {\"type\":\"content_block_stop\",\"index\":0}\n"
		"\n"
		"event: message_stop\n"
		"data: {\"type\":\"message_stop\"}\n"
		"\n");
	FParsedLLMResponse ParsedResponse;
	FString ErrorMessage;

	const bool bParsed = FLLMResponseParser::ParseAnthropicMessages(ResponseBody, ParsedResponse, ErrorMessage);
	TestTrue(TEXT("Anthropic streaming tool_use payload should parse."), bParsed);
	TestTrue(TEXT("Anthropic streaming tool_use payload should not emit parser errors."), ErrorMessage.IsEmpty());
	AssertUs2StructuredContract(*this, ParsedResponse, ELLMResponseParseTier::FunctionCalling, TEXT("Anthropic streaming input wins."));
	TestEqual(TEXT("Anthropic streaming tool_use should extract one action."), ParsedResponse.Actions.Num(), 1);
	if (ParsedResponse.Actions.Num() == 1)
	{
		TestEqual(TEXT("Anthropic streaming tool_use action type should be used."), ParsedResponse.Actions[0].ActionType, FString(TEXT("Action.Use")));
		TestEqual(TEXT("Anthropic streaming tool_use action target should be used."), ParsedResponse.Actions[0].Target, FString(TEXT("SO_SLOT_Stream")));
	}
	TestEqual(TEXT("Anthropic streaming tool_use emotion valence should be used."), ParsedResponse.EmotionDelta.Valence, 0.6f);
	TestEqual(TEXT("Anthropic streaming tool_use relationship trust should be used."), ParsedResponse.RelationshipDelta.Trust, 0.8f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2StrictJsonFullContractMatrixTest,
	"AINpc.US2.Parser.StrictJsonFullContractMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2StrictJsonFullContractMatrixTest::RunTest(const FString& Parameters)
{
	const FString ResponseBody = TEXT(R"({"choices":[{"message":{"content":"{\"dialogue\":\"Strict matrix response.\",\"actions\":[{\"type\":\"Action.Wave\",\"target\":\"player\"},{\"type\":\"Action.Inspect\",\"target\":\"SO_SLOT_Strict\"}],\"emotion_delta\":{\"valence\":1.5,\"arousal\":-1.5,\"dominance\":0.25},\"relationship_delta\":{\"affinity\":1.2,\"trust\":-1.2,\"familiarity\":0.5}}"}}]})");
	FParsedLLMResponse ParsedResponse;
	FString ErrorMessage;

	const bool bParsed = FLLMResponseParser::ParseOpenAIChatCompletion(ResponseBody, ParsedResponse, ErrorMessage);
	TestTrue(TEXT("Strict JSON payload should parse."), bParsed);
	TestTrue(TEXT("Strict JSON payload should not emit parser errors."), ErrorMessage.IsEmpty());
	AssertUs2StructuredContract(*this, ParsedResponse, ELLMResponseParseTier::StrictJsonSchema, TEXT("Strict matrix response."));
	TestEqual(TEXT("Strict JSON should preserve action arrays."), ParsedResponse.Actions.Num(), 2);
	if (ParsedResponse.Actions.Num() == 2)
	{
		TestEqual(TEXT("Strict JSON first action should be preserved."), ParsedResponse.Actions[0].ActionType, FString(TEXT("Action.Wave")));
		TestEqual(TEXT("Strict JSON second target should be preserved."), ParsedResponse.Actions[1].Target, FString(TEXT("SO_SLOT_Strict")));
	}
	TestEqual(TEXT("Strict JSON should clamp emotion valence."), ParsedResponse.EmotionDelta.Valence, 1.0f);
	TestEqual(TEXT("Strict JSON should clamp emotion arousal."), ParsedResponse.EmotionDelta.Arousal, -1.0f);
	TestEqual(TEXT("Strict JSON should preserve emotion dominance."), ParsedResponse.EmotionDelta.Dominance, 0.25f);
	TestEqual(TEXT("Strict JSON should clamp relationship affinity."), ParsedResponse.RelationshipDelta.Affinity, 1.0f);
	TestEqual(TEXT("Strict JSON should clamp relationship trust."), ParsedResponse.RelationshipDelta.Trust, -1.0f);
	TestEqual(TEXT("Strict JSON should preserve relationship familiarity."), ParsedResponse.RelationshipDelta.Familiarity, 0.5f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2LooseJsonActionIntentRequiresSmartObjectValidationTest,
	"AINpc.US2.Parser.LooseJsonActionIntentRequiresSmartObjectValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2LooseJsonActionIntentRequiresSmartObjectValidationTest::RunTest(const FString& Parameters)
{
	const FString ResponseBody = TEXT(R"({"choices":[{"message":{"content":"Mixed prose before JSON.\n```json\n{\"dialogue\":\"Loose action intent response.\",\"actions\":[{\"type\":\"Action.Inspect\",\"target\":\"SO_SLOT_Illegal\"}],\"extra_field\":\"forces loose tier\"}\n```"}}]})");
	FParsedLLMResponse ParsedResponse;
	FString ErrorMessage;

	const bool bParsed = FLLMResponseParser::ParseOpenAIChatCompletion(ResponseBody, ParsedResponse, ErrorMessage);
	TestTrue(TEXT("Loose JSON payload should parse into an action intent."), bParsed);
	TestTrue(TEXT("Loose JSON payload should not emit parser errors."), ErrorMessage.IsEmpty());
	AssertUs2StructuredContract(*this, ParsedResponse, ELLMResponseParseTier::LooseExtraction, TEXT("Loose action intent response."));
	TestEqual(TEXT("Loose JSON should only produce an intent for downstream validation."), ParsedResponse.Actions.Num(), 1);
	if (ParsedResponse.Actions.Num() != 1)
	{
		return false;
	}

	FString FailureReason;
	const bool bAccepted = FStateTreeTask_ExecuteSmartObject::ValidateSmartObjectActionForTest(
		ParsedResponse.Actions[0],
		{TEXT("SO_SLOT_Legal")},
		FailureReason);
	TestFalse(TEXT("Loose JSON action intent with an illegal target must be rejected by SmartObject validation."), bAccepted);
	TestTrue(TEXT("Rejected loose JSON action intent should expose a diagnostic reason."), !FailureReason.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2MalformedJsonDowngradesToPlainTextTest,
	"AINpc.US2.Parser.MalformedJsonDowngradesToPlainText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2MalformedJsonDowngradesToPlainTextTest::RunTest(const FString& Parameters)
{
	const FString MalformedContent = TEXT("```json\n{\"dialogue\":\"broken\",\"actions\":[{\"type\":\"Action.Inspect\",\"target\":\"SO_SLOT_Illegal\"}]\n```");
	const FString ResponseBody = TEXT(R"({"choices":[{"message":{"content":"```json\n{\"dialogue\":\"broken\",\"actions\":[{\"type\":\"Action.Inspect\",\"target\":\"SO_SLOT_Illegal\"}]\n```"}}]})");
	FParsedLLMResponse ParsedResponse;
	FString ErrorMessage;

	const bool bParsed = FLLMResponseParser::ParseOpenAIChatCompletion(ResponseBody, ParsedResponse, ErrorMessage);
	TestTrue(TEXT("Malformed content JSON should downgrade instead of crashing."), bParsed);
	TestTrue(TEXT("Malformed content JSON downgrade should not emit provider-envelope parser errors."), ErrorMessage.IsEmpty());
	TestEqual(TEXT("Malformed content JSON should select plain text tier."), ParsedResponse.ParseTier, ELLMResponseParseTier::PlainText);
	TestEqual(TEXT("Malformed content JSON should preserve raw dialogue text for diagnostics."), ParsedResponse.Dialogue, MalformedContent);
	TestFalse(TEXT("Malformed content JSON should not claim JSON parsing."), ParsedResponse.bParsedAsJson);
	TestEqual(TEXT("Malformed content JSON must not create executable action intents."), ParsedResponse.Actions.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2LooseJsonExtractsDeltasTest,
	"AINpc.US2.Parser.LooseJsonExtractsDeltas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2LooseJsonExtractsDeltasTest::RunTest(const FString& Parameters)
{
	const FString ResponseBody = TEXT(R"({"choices":[{"message":{"content":"Loose prose before JSON.\n```json\n{\"dialogue\":\"Loose delta response.\",\"actions\":[],\"emotion_delta\":{\"valence\":1.7,\"arousal\":-1.7,\"dominance\":0.25},\"relationship_delta\":{\"affinity\":1.4,\"trust\":-1.4,\"familiarity\":0.75},\"extra_field\":\"forces loose tier\"}\n```"}}]})");
	FParsedLLMResponse ParsedResponse;
	FString ErrorMessage;

	const bool bParsed = FLLMResponseParser::ParseOpenAIChatCompletion(ResponseBody, ParsedResponse, ErrorMessage);
	TestTrue(TEXT("Loose JSON payload with deltas should parse."), bParsed);
	TestTrue(TEXT("Loose JSON payload with deltas should not emit parser errors."), ErrorMessage.IsEmpty());
	AssertUs2StructuredContract(*this, ParsedResponse, ELLMResponseParseTier::LooseExtraction, TEXT("Loose delta response."));
	TestEqual(TEXT("Loose JSON should preserve empty actions array."), ParsedResponse.Actions.Num(), 0);
	TestEqual(TEXT("Loose JSON should clamp emotion valence."), ParsedResponse.EmotionDelta.Valence, 1.0f);
	TestEqual(TEXT("Loose JSON should clamp emotion arousal."), ParsedResponse.EmotionDelta.Arousal, -1.0f);
	TestEqual(TEXT("Loose JSON should preserve emotion dominance."), ParsedResponse.EmotionDelta.Dominance, 0.25f);
	TestEqual(TEXT("Loose JSON should clamp relationship affinity."), ParsedResponse.RelationshipDelta.Affinity, 1.0f);
	TestEqual(TEXT("Loose JSON should clamp relationship trust."), ParsedResponse.RelationshipDelta.Trust, -1.0f);
	TestEqual(TEXT("Loose JSON should preserve relationship familiarity."), ParsedResponse.RelationshipDelta.Familiarity, 0.75f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2ParserClearsReusableOutputStateTest,
	"AINpc.US2.Parser.ClearsReusableOutputState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2ParserClearsReusableOutputStateTest::RunTest(const FString& Parameters)
{
	FParsedLLMResponse ParsedResponse;
	FString ErrorMessage = TEXT("stale diagnostic");

	const FString StructuredResponseBody = TEXT(R"({"choices":[{"message":{"content":"{\"dialogue\":\"Structured first response.\",\"actions\":[{\"type\":\"Action.Inspect\",\"target\":\"SO_SLOT_First\"}],\"emotion_delta\":{\"valence\":0.9,\"arousal\":0.8,\"dominance\":0.7},\"relationship_delta\":{\"affinity\":0.6,\"trust\":0.5,\"familiarity\":0.4}}"}}]})");
	TestTrue(
		TEXT("Initial structured response should parse for reusable-output setup."),
		FLLMResponseParser::ParseOpenAIChatCompletion(StructuredResponseBody, ParsedResponse, ErrorMessage));
	TestEqual(TEXT("Initial structured response should seed one action."), ParsedResponse.Actions.Num(), 1);

	const FString PlainTextResponseBody = TEXT(R"({"choices":[{"message":{"content":"Plain response after structured output."}}]})");
	const bool bPlainParsed = FLLMResponseParser::ParseOpenAIChatCompletion(PlainTextResponseBody, ParsedResponse, ErrorMessage);
	TestTrue(TEXT("Plain text response should parse after reusing output storage."), bPlainParsed);
	TestTrue(TEXT("Successful parse should clear stale diagnostics."), ErrorMessage.IsEmpty());
	TestEqual(TEXT("Plain text response should select plain text tier."), ParsedResponse.ParseTier, ELLMResponseParseTier::PlainText);
	TestEqual(TEXT("Plain text response should replace dialogue."), ParsedResponse.Dialogue, FString(TEXT("Plain response after structured output.")));
	TestFalse(TEXT("Plain text response should clear JSON flag from previous parse."), ParsedResponse.bParsedAsJson);
	TestEqual(TEXT("Plain text response must clear stale actions from previous parse."), ParsedResponse.Actions.Num(), 0);
	TestEqual(TEXT("Plain text response must clear stale emotion valence."), ParsedResponse.EmotionDelta.Valence, 0.0f);
	TestEqual(TEXT("Plain text response must clear stale relationship trust."), ParsedResponse.RelationshipDelta.Trust, 0.0f);

	const FString InvalidEnvelope = TEXT(R"({"choices":[]})");
	const bool bInvalidParsed = FLLMResponseParser::ParseOpenAIChatCompletion(InvalidEnvelope, ParsedResponse, ErrorMessage);
	TestFalse(TEXT("Invalid envelope should fail after reusing output storage."), bInvalidParsed);
	TestTrue(TEXT("Invalid envelope should set current diagnostic."), ErrorMessage.Contains(TEXT("No choices")));
	TestEqual(TEXT("Invalid envelope should reset parse tier to None."), ParsedResponse.ParseTier, ELLMResponseParseTier::None);
	TestEqual(TEXT("Invalid envelope must not keep stale actions."), ParsedResponse.Actions.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2ParserFailureDiagnosticsTest,
	"AINpc.US2.Parser.FailureDiagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2ParserFailureDiagnosticsTest::RunTest(const FString& Parameters)
{
	const FString ResponseBody = TEXT(R"({"choices":[]})");
	FParsedLLMResponse ParsedResponse;
	FString ErrorMessage;

	const bool bParsed = FLLMResponseParser::ParseOpenAIChatCompletion(ResponseBody, ParsedResponse, ErrorMessage);
	TestFalse(TEXT("Invalid provider envelope should fail parsing."), bParsed);
	TestTrue(TEXT("Invalid provider envelope should expose a diagnostic error message."), !ErrorMessage.IsEmpty());
	TestEqual(TEXT("Invalid provider envelope should not mutate parse tier into a successful tier."), ParsedResponse.ParseTier, ELLMResponseParseTier::None);
	TestEqual(TEXT("Invalid provider envelope should not create action intents."), ParsedResponse.Actions.Num(), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
