#include "Components/AINpcComponent.h"
#include "HAL/PlatformProcess.h"
#include "LLM/AnthropicProvider.h"
#include "LLM/OpenAIProvider.h"
#include "LLM/SSEParser.h"
#include "Misc/AutomationTest.h"
#include "Async/TaskGraphInterfaces.h"

#if defined(WITH_DEV_AUTOMATION_TESTS) && WITH_DEV_AUTOMATION_TESTS

namespace
{
void PumpGameThreadTasksForTest()
{
	FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
	FPlatformProcess::Sleep(0.01f);
	FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2SSESplitMultilineCommentDoneTest,
	"AINpc.Core.US2.Streaming.SSEParser.SplitMultilineCommentDone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2SSESplitMultilineCommentDoneTest::RunTest(const FString& Parameters)
{
	FSSEParser Parser;
	TArray<FString> DataEvents;
	int32 DoneCount = 0;
	int32 ErrorCount = 0;

	Parser.OnData.BindLambda([&DataEvents](const FString& Data)
	{
		DataEvents.Add(Data);
	});
	Parser.OnDone.BindLambda([&DoneCount]()
	{
		++DoneCount;
	});
	Parser.OnError.BindLambda([&ErrorCount](const FString&)
	{
		++ErrorCount;
	});

	Parser.ProcessChunk(TEXT(": heartbeat\n"));
	Parser.ProcessChunk(TEXT("data: hel"));
	TestEqual(TEXT("Split chunk should not emit before event terminator."), DataEvents.Num(), 0);

	Parser.ProcessChunk(TEXT("lo\ndata: world\n\n"));
	TestEqual(TEXT("Completed multiline event should emit once."), DataEvents.Num(), 1);
	if (DataEvents.Num() == 1)
	{
		TestEqual(TEXT("Multiline data fields should join with newline."), DataEvents[0], FString(TEXT("hello\nworld")));
	}

	Parser.ProcessChunk(TEXT("data: [DONE]\n\n"));
	Parser.ProcessChunk(TEXT("data: [DONE]\n\n"));
	Parser.ProcessChunk(TEXT("data: stale\n\n"));

	TestEqual(TEXT("[DONE] should emit exactly once."), DoneCount, 1);
	TestEqual(TEXT("Duplicate terminal input should not emit stale data."), DataEvents.Num(), 1);
	TestEqual(TEXT("Heartbeat/comment and done path should not emit errors."), ErrorCount, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2SSEResetErrorTest,
	"AINpc.Core.US2.Streaming.SSEParser.ResetAndError",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2SSEResetErrorTest::RunTest(const FString& Parameters)
{
	FSSEParser Parser;
	TArray<FString> DataEvents;
	TArray<FString> ErrorEvents;
	int32 DoneCount = 0;

	Parser.OnData.BindLambda([&DataEvents](const FString& Data)
	{
		DataEvents.Add(Data);
	});
	Parser.OnDone.BindLambda([&DoneCount]()
	{
		++DoneCount;
	});
	Parser.OnError.BindLambda([&ErrorEvents](const FString& Error)
	{
		ErrorEvents.Add(Error);
	});

	Parser.ProcessChunk(TEXT("event: error\ndata: upstream failed\n\n"));
	Parser.ProcessChunk(TEXT("data: stale\n\n"));

	TestEqual(TEXT("Error event should surface exactly once."), ErrorEvents.Num(), 1);
	if (ErrorEvents.Num() == 1)
	{
		TestEqual(TEXT("Error event data should be preserved."), ErrorEvents[0], FString(TEXT("upstream failed")));
	}
	TestEqual(TEXT("Error terminal should suppress stale data."), DataEvents.Num(), 0);
	TestEqual(TEXT("Error terminal should not emit done."), DoneCount, 0);

	Parser.Reset();
	Parser.ProcessChunk(TEXT("data: after reset\n\n"));

	TestEqual(TEXT("Reset should clear terminal state and allow new data."), DataEvents.Num(), 1);
	if (DataEvents.Num() == 1)
	{
		TestEqual(TEXT("Post-reset data should be emitted."), DataEvents[0], FString(TEXT("after reset")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2ComponentRejectsStalePartialChunksTest,
	"AINpc.Core.US2.Streaming.ComponentRejectsStalePartialChunks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2OpenAIProviderTranslatesStreamChunksTest,
	"AINpc.Core.US2.Streaming.OpenAIProviderTranslatesStreamChunks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2AnthropicProviderTranslatesStreamChunksTest,
	"AINpc.Core.US2.Streaming.AnthropicProviderTranslatesStreamChunks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2ComponentRejectsStalePartialChunksTest::RunTest(const FString& Parameters)
{
	UAINpcComponent* Component = NewObject<UAINpcComponent>();
	TestNotNull(TEXT("Component should be created for partial-response request identity test."), Component);
	if (!Component)
	{
		return false;
	}

	const FGuid ActiveRequestId = FGuid::NewGuid();
	Component->SetDialogueTestState(true, true, ActiveRequestId, 0, ENpcDialogueState::WaitingForLLM);

	TArray<FString> PartialTexts;
	Component->OnDialoguePartialResponseNative().AddLambda([&PartialTexts](const FString& PartialText)
	{
		PartialTexts.Add(PartialText);
	});

	FLLMRequest Request = Component->BuildStreamingRequestForTest();
	TestTrue(TEXT("Streaming test request should install a stream callback."), static_cast<bool>(Request.StreamCallback));
	if (!Request.StreamCallback)
	{
		return false;
	}

	FLLMStreamChunk StaleChunk;
	StaleChunk.RequestId = FGuid::NewGuid();
	StaleChunk.Content = TEXT("stale");
	Request.StreamCallback(StaleChunk);
	PumpGameThreadTasksForTest();
	TestEqual(TEXT("Stale partial chunk should be rejected."), PartialTexts.Num(), 0);

	FLLMStreamChunk EmptyChunk;
	EmptyChunk.RequestId = ActiveRequestId;
	Request.StreamCallback(EmptyChunk);
	PumpGameThreadTasksForTest();
	TestEqual(TEXT("Empty partial chunk should be rejected."), PartialTexts.Num(), 0);

	FLLMStreamChunk FinalChunk;
	FinalChunk.RequestId = ActiveRequestId;
	FinalChunk.Content = TEXT("final");
	FinalChunk.bIsFinal = true;
	Request.StreamCallback(FinalChunk);
	PumpGameThreadTasksForTest();
	TestEqual(TEXT("Final partial chunk should be rejected."), PartialTexts.Num(), 0);

	FLLMStreamChunk ErrorChunk;
	ErrorChunk.RequestId = ActiveRequestId;
	ErrorChunk.Content = TEXT("error text must not display");
	ErrorChunk.ErrorMessage = TEXT("stream failed");
	ErrorChunk.bIsError = true;
	ErrorChunk.bIsFinal = true;
	Request.StreamCallback(ErrorChunk);
	PumpGameThreadTasksForTest();
	TestEqual(TEXT("Error stream chunk should be rejected as UI text."), PartialTexts.Num(), 0);

	FLLMStreamChunk ActiveChunk;
	ActiveChunk.RequestId = ActiveRequestId;
	ActiveChunk.Content = TEXT("active");
	Request.StreamCallback(ActiveChunk);
	PumpGameThreadTasksForTest();

	TestEqual(TEXT("Active non-final partial chunk should broadcast once."), PartialTexts.Num(), 1);
	if (PartialTexts.Num() == 1)
	{
		TestEqual(TEXT("Active chunk content should be forwarded."), PartialTexts[0], FString(TEXT("active")));
	}

	Component->SetDialogueTestState(false, true, ActiveRequestId, 0, ENpcDialogueState::WaitingForLLM);
	FLLMStreamChunk InactiveDialogueChunk;
	InactiveDialogueChunk.RequestId = ActiveRequestId;
	InactiveDialogueChunk.Content = TEXT("inactive");
	Request.StreamCallback(InactiveDialogueChunk);
	PumpGameThreadTasksForTest();
	TestEqual(TEXT("Inactive dialogue should reject partial chunk."), PartialTexts.Num(), 1);

	Component->SetDialogueTestState(true, false, ActiveRequestId, 0, ENpcDialogueState::WaitingForLLM);
	FLLMStreamChunk NotInFlightChunk;
	NotInFlightChunk.RequestId = ActiveRequestId;
	NotInFlightChunk.Content = TEXT("not in flight");
	Request.StreamCallback(NotInFlightChunk);
	PumpGameThreadTasksForTest();
	TestEqual(TEXT("Non-in-flight request should reject partial chunk."), PartialTexts.Num(), 1);

	return true;
}

bool FAINpcUs2OpenAIProviderTranslatesStreamChunksTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FOpenAIProvider> Provider = MakeShared<FOpenAIProvider>(TEXT("test-key"), TEXT("gpt-test"), TEXT("https://provider.example.invalid/v1"));
	const FGuid RequestId = FGuid::NewGuid();
	TArray<FLLMStreamChunk> Chunks;

	const FString StreamBody =
		TEXT("data: {\"choices\":[{\"delta\":{\"content\":\"你\"}}]}\n\n")
		TEXT("data: {\"choices\":[{\"delta\":{\"content\":\"好\"}}]}\n\n")
		TEXT("data: [DONE]\n\n");

	Provider->ProcessStreamResponseForTest(
		RequestId,
		StreamBody,
		[&Chunks](const FLLMStreamChunk& Chunk)
		{
			Chunks.Add(Chunk);
		});

	TestEqual(TEXT("OpenAI stream adapter should emit two content chunks plus final."), Chunks.Num(), 3);
	if (Chunks.Num() == 3)
	{
		TestEqual(TEXT("First OpenAI chunk should preserve request id."), Chunks[0].RequestId, RequestId);
		TestEqual(TEXT("First OpenAI chunk content should be vendor delta content."), Chunks[0].Content, FString(TEXT("你")));
		TestFalse(TEXT("Content chunks must not be final."), Chunks[0].bIsFinal);
		TestEqual(TEXT("Second OpenAI chunk content should be vendor delta content."), Chunks[1].Content, FString(TEXT("好")));
		TestTrue(TEXT("OpenAI final chunk should be final."), Chunks[2].bIsFinal);
		TestEqual(TEXT("OpenAI final chunk should carry accumulated text."), Chunks[2].Content, FString(TEXT("你好")));
	}

	Chunks.Reset();
	Provider->ProcessStreamResponseForTest(
		RequestId,
		TEXT("event: error\ndata: upstream failed\n\n"),
		[&Chunks](const FLLMStreamChunk& Chunk)
		{
			Chunks.Add(Chunk);
		});

	TestEqual(TEXT("OpenAI SSE error event should emit one diagnosable error chunk."), Chunks.Num(), 1);
	if (Chunks.Num() == 1)
	{
		TestTrue(TEXT("OpenAI stream error should be marked as error."), Chunks[0].bIsError);
		TestFalse(TEXT("OpenAI stream error message should be diagnosable."), Chunks[0].ErrorMessage.IsEmpty());
	}

	return true;
}

bool FAINpcUs2AnthropicProviderTranslatesStreamChunksTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FAnthropicProvider> Provider = MakeShared<FAnthropicProvider>(TEXT("test-key"), TEXT("claude-test"), TEXT("https://provider.example.invalid/v1"));
	const FGuid RequestId = FGuid::NewGuid();
	TArray<FLLMStreamChunk> Chunks;

	const FString StreamBody =
		TEXT("data: {\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\",\"text\":\"你\"}}\n\n")
		TEXT("data: {\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\",\"text\":\"好\"}}\n\n")
		TEXT("data: {\"type\":\"message_stop\"}\n\n");

	Provider->ProcessStreamResponseForTest(
		RequestId,
		StreamBody,
		[&Chunks](const FLLMStreamChunk& Chunk)
		{
			Chunks.Add(Chunk);
		});

	TestEqual(TEXT("Anthropic stream adapter should emit two content chunks plus final."), Chunks.Num(), 3);
	if (Chunks.Num() == 3)
	{
		TestEqual(TEXT("First Anthropic chunk should preserve request id."), Chunks[0].RequestId, RequestId);
		TestEqual(TEXT("First Anthropic chunk content should be vendor delta text."), Chunks[0].Content, FString(TEXT("你")));
		TestFalse(TEXT("Content chunks must not be final."), Chunks[0].bIsFinal);
		TestEqual(TEXT("Second Anthropic chunk content should be vendor delta text."), Chunks[1].Content, FString(TEXT("好")));
		TestTrue(TEXT("Anthropic final chunk should be final."), Chunks[2].bIsFinal);
		TestEqual(TEXT("Anthropic final chunk should carry accumulated text."), Chunks[2].Content, FString(TEXT("你好")));
	}

	Chunks.Reset();
	auto BuildAnthropicToolDeltaEvent = [](const FString& PartialJson)
	{
		const TSharedRef<FJsonObject> DeltaObject = MakeShared<FJsonObject>();
		DeltaObject->SetStringField(TEXT("type"), TEXT("input_json_delta"));
		DeltaObject->SetStringField(TEXT("partial_json"), PartialJson);

		const TSharedRef<FJsonObject> EventObject = MakeShared<FJsonObject>();
		EventObject->SetStringField(TEXT("type"), TEXT("content_block_delta"));
		EventObject->SetNumberField(TEXT("index"), 0);
		EventObject->SetObjectField(TEXT("delta"), DeltaObject);

		FString SerializedEvent;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> JsonWriter =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&SerializedEvent);
		FJsonSerializer::Serialize(EventObject, JsonWriter);
		return FString::Printf(TEXT("data: %s\n\n"), *SerializedEvent);
	};
	const FString ToolStreamBody =
		BuildAnthropicToolDeltaEvent(TEXT("{\"dialogue\":\"I will inspect the pillar.\",\"actions\":[{\"type\":\"Action.Inspect\",\"target\":\"SO_SLOT_Pillar\"}],")) +
		BuildAnthropicToolDeltaEvent(TEXT("\"emotion_delta\":{\"valence\":0.2,\"arousal\":0.1,\"dominance\":0.0},\"relationship_delta\":{\"affinity\":0.0,\"trust\":0.1,\"familiarity\":0.0}}")) +
		TEXT("data: {\"type\":\"message_stop\"}\n\n");
	Provider->ProcessStreamResponseForTest(
		RequestId,
		ToolStreamBody,
		[&Chunks](const FLLMStreamChunk& Chunk)
		{
			Chunks.Add(Chunk);
		});

	TestEqual(TEXT("Anthropic tool streaming adapter should emit dialogue partial plus final."), Chunks.Num(), 2);
	if (Chunks.Num() != 2)
	{
		AddInfo(FString::Printf(TEXT("Anthropic tool stream body: %s"), *ToolStreamBody));
		for (int32 ChunkIndex = 0; ChunkIndex < Chunks.Num(); ++ChunkIndex)
		{
			AddInfo(FString::Printf(
				TEXT("Anthropic tool chunk[%d]: Content='%s' Error='%s' Final=%s IsError=%s"),
				ChunkIndex,
				*Chunks[ChunkIndex].Content,
				*Chunks[ChunkIndex].ErrorMessage,
				Chunks[ChunkIndex].bIsFinal ? TEXT("true") : TEXT("false"),
				Chunks[ChunkIndex].bIsError ? TEXT("true") : TEXT("false")));
		}
	}
	if (Chunks.Num() == 2)
	{
		TestEqual(TEXT("Anthropic tool chunk should expose only dialogue text from input_json_delta."), Chunks[0].Content, FString(TEXT("I will inspect the pillar.")));
		TestFalse(TEXT("Anthropic tool dialogue chunk must not be final."), Chunks[0].bIsFinal);
		TestTrue(TEXT("Anthropic tool final chunk should be final."), Chunks[1].bIsFinal);
		TestEqual(TEXT("Anthropic tool final chunk should carry accumulated dialogue text."), Chunks[1].Content, FString(TEXT("I will inspect the pillar.")));
	}

	Chunks.Reset();
	Provider->ProcessStreamResponseForTest(
		RequestId,
		TEXT("data: {\"type\":\"error\",\"error\":{\"message\":\"quota exceeded\"}}\n\n"),
		[&Chunks](const FLLMStreamChunk& Chunk)
		{
			Chunks.Add(Chunk);
		});

	TestEqual(TEXT("Anthropic stream error event should emit one diagnosable error chunk."), Chunks.Num(), 1);
	if (Chunks.Num() == 1)
	{
		TestTrue(TEXT("Anthropic stream error should be marked as error."), Chunks[0].bIsError);
		TestEqual(TEXT("Anthropic stream error message should be preserved."), Chunks[0].ErrorMessage, FString(TEXT("quota exceeded")));
	}

	return true;
}

#endif
