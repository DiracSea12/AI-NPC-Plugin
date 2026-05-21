#include "Widgets/NpcDialogueBubbleWidget.h"

#include "AINpcUILog.h"
#include "Components/AINpcComponent.h"
#include "Components/TextBlock.h"
#include "Misc/AutomationTest.h"

#if WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDialogueBubbleWidgetBindFlowTest,
	"AINpc.UI.BlueprintIntegration.WidgetBindToNpcComponentFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcDialogueBubbleWidgetBindFlowTest::RunTest(const FString& Parameters)
{
	UAINpcComponent::ResetConcurrencyStateForTest();
	UAINpcComponent::SetDialogueDispatchBypassForTest(true);

	UAINpcComponent* Component = NewObject<UAINpcComponent>();
	UNpcDialogueBubbleWidget* Widget = NewObject<UNpcDialogueBubbleWidget>();
	TestNotNull(TEXT("Component should be created for widget binding test."), Component);
	TestNotNull(TEXT("Widget should be created for widget binding test."), Widget);
	if (!Component || !Widget)
	{
		UAINpcComponent::ResetConcurrencyStateForTest();
		return false;
	}

	Widget->bUseTypewriterEffect = false;
	Widget->BindToNpcComponent(Component);

	const bool bFirstStarted = Component->StartDialogue(TEXT("第一个响应应该更新小部件。"));
	TestTrue(TEXT("First StartDialogue should succeed."), bFirstStarted);

	FLLMResponse FirstResponse;
	FirstResponse.RequestId = Component->GetActiveRequestIdForTest();
	FirstResponse.bSuccess = true;
	FirstResponse.Content = TEXT("你好，来自绑定的组件。");
	FirstResponse.ParsedResponse.Dialogue = FirstResponse.Content;
	Component->HandleRequestCompletedForTest(FirstResponse);

	TestEqual(TEXT("Bound widget should receive full response text."), Widget->GetFullResponseText(), FirstResponse.Content);
	TestEqual(TEXT("Bound widget should display response text."), Widget->GetDisplayedText().ToString(), FirstResponse.Content);

	Widget->BindToNpcComponent(nullptr);

	const bool bSecondStarted = Component->StartDialogue(TEXT("解绑后第二个响应不应该更新小部件。"));
	TestTrue(TEXT("Second StartDialogue should succeed."), bSecondStarted);

	FLLMResponse SecondResponse;
	SecondResponse.RequestId = Component->GetActiveRequestIdForTest();
	SecondResponse.bSuccess = true;
	SecondResponse.Content = TEXT("这段文本不应该出现在小部件中。");
	SecondResponse.ParsedResponse.Dialogue = SecondResponse.Content;
	Component->HandleRequestCompletedForTest(SecondResponse);

	TestEqual(TEXT("Unbound widget should keep previous full response text."), Widget->GetFullResponseText(), FirstResponse.Content);
	TestEqual(TEXT("Unbound widget should keep previous displayed text."), Widget->GetDisplayedText().ToString(), FirstResponse.Content);

	Component->EndDialogue();
	UAINpcComponent::ResetConcurrencyStateForTest();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDialogueBubbleWidgetStreamingSentenceTest,
	"AINpc.DialogueBubble.StreamingSentence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDialogueBubbleWidgetBindResyncActiveSessionTest,
	"AINpc.DialogueBubble.BindResyncActiveSession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDialogueBubbleWidgetRebindIdleClearsStaleTextTest,
	"AINpc.DialogueBubble.RebindIdleClearsStaleText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDialogueBubbleWidgetSameComponentRebindActiveNoResponseClearsTextTest,
	"AINpc.DialogueBubble.SameComponentRebindActiveNoResponseClearsText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcDialogueBubbleWidgetBindResyncActiveSessionTest::RunTest(const FString& Parameters)
{
	UAINpcComponent::ResetConcurrencyStateForTest();
	UAINpcComponent::SetDialogueDispatchBypassForTest(true);

	UAINpcComponent* Component = NewObject<UAINpcComponent>();
	UNpcDialogueBubbleWidget* Widget = NewObject<UNpcDialogueBubbleWidget>();
	TestNotNull(TEXT("Component should be created for bind resync test."), Component);
	TestNotNull(TEXT("Widget should be created for bind resync test."), Widget);
	if (!Component || !Widget)
	{
		UAINpcComponent::ResetConcurrencyStateForTest();
		return false;
	}

	const bool bStarted = Component->StartDialogue(TEXT("小部件绑定前的提示。"));
	TestTrue(TEXT("StartDialogue should succeed for bind resync test."), bStarted);

	FLLMResponse Response;
	Response.RequestId = Component->GetActiveRequestIdForTest();
	Response.bSuccess = true;
	Response.Content = TEXT("小部件绑定前生成的响应。");
	Response.ParsedResponse.Dialogue = Response.Content;
	Component->HandleRequestCompletedForTest(Response);

	Widget->bUseTypewriterEffect = false;
	Widget->BindToNpcComponent(Component);

	TestEqual(TEXT("Widget bind should resync current full response text."),
		Widget->GetFullResponseText(), Response.Content);
	TestEqual(TEXT("Widget bind should resync currently displayed text."),
		Widget->GetDisplayedText().ToString(), Response.Content);

	Component->EndDialogue();
	UAINpcComponent::ResetConcurrencyStateForTest();
	return true;
}

bool FAINpcDialogueBubbleWidgetRebindIdleClearsStaleTextTest::RunTest(const FString& Parameters)
{
	UAINpcComponent::ResetConcurrencyStateForTest();
	UAINpcComponent::SetDialogueDispatchBypassForTest(true);

	UAINpcComponent* FirstComponent = NewObject<UAINpcComponent>();
	UAINpcComponent* SecondComponent = NewObject<UAINpcComponent>();
	UNpcDialogueBubbleWidget* Widget = NewObject<UNpcDialogueBubbleWidget>();
	TestNotNull(TEXT("First component should be created for rebind idle clear test."), FirstComponent);
	TestNotNull(TEXT("Second component should be created for rebind idle clear test."), SecondComponent);
	TestNotNull(TEXT("Widget should be created for rebind idle clear test."), Widget);
	if (!FirstComponent || !SecondComponent || !Widget)
	{
		UAINpcComponent::ResetConcurrencyStateForTest();
		return false;
	}

	Widget->bUseTypewriterEffect = false;
	Widget->BindToNpcComponent(FirstComponent);

	const bool bStarted = FirstComponent->StartDialogue(TEXT("第一个组件上的提示。"));
	TestTrue(TEXT("StartDialogue should succeed on first component."), bStarted);

	FLLMResponse FirstResponse;
	FirstResponse.RequestId = FirstComponent->GetActiveRequestIdForTest();
	FirstResponse.bSuccess = true;
	FirstResponse.Content = TEXT("来自第一个组件的响应。");
	FirstResponse.ParsedResponse.Dialogue = FirstResponse.Content;
	FirstComponent->HandleRequestCompletedForTest(FirstResponse);

	TestEqual(TEXT("Widget should display first component response before rebind."),
		Widget->GetDisplayedText().ToString(), FirstResponse.Content);

	FirstComponent->EndDialogue();
	Widget->BindToNpcComponent(SecondComponent);

	TestEqual(TEXT("Rebinding to an idle component should clear stale displayed text."),
		Widget->GetDisplayedText().ToString(), TEXT(""));
	TestEqual(TEXT("Rebinding to an idle component should clear stale full response text."),
		Widget->GetFullResponseText(), TEXT(""));

	UAINpcComponent::ResetConcurrencyStateForTest();
	return true;
}

bool FAINpcDialogueBubbleWidgetSameComponentRebindActiveNoResponseClearsTextTest::RunTest(const FString& Parameters)
{
	UAINpcComponent::ResetConcurrencyStateForTest();
	UAINpcComponent::SetDialogueDispatchBypassForTest(true);

	UAINpcComponent* Component = NewObject<UAINpcComponent>();
	UNpcDialogueBubbleWidget* Widget = NewObject<UNpcDialogueBubbleWidget>();
	TestNotNull(TEXT("Component should be created for same-component active rebind test."), Component);
	TestNotNull(TEXT("Widget should be created for same-component active rebind test."), Widget);
	if (!Component || !Widget)
	{
		UAINpcComponent::ResetConcurrencyStateForTest();
		return false;
	}

	Widget->bUseTypewriterEffect = false;
	Widget->BindToNpcComponent(Component);

	const bool bStarted = Component->StartDialogue(TEXT("同一组件重新绑定前的提示。"));
	TestTrue(TEXT("StartDialogue should succeed for same-component active rebind test."), bStarted);

	Widget->ShowResponseText(TEXT("过期的文本"));
	TestEqual(TEXT("Widget should show stale text before explicit same-component resync."),
		Widget->GetDisplayedText().ToString(), TEXT("过期的文本"));

	Widget->BindToNpcComponent(Component);

	TestEqual(TEXT("Same-component rebind during active no-response state should clear stale displayed text."),
		Widget->GetDisplayedText().ToString(), TEXT(""));
	TestEqual(TEXT("Same-component rebind during active no-response state should clear stale full text."),
		Widget->GetFullResponseText(), TEXT(""));

	Component->EndDialogue();
	UAINpcComponent::ResetConcurrencyStateForTest();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDialogueBubbleWidgetSessionStartClearsStateTest,
	"AINpc.DialogueBubble.SessionStartClearsState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDialogueBubbleWidgetIgnoreStalePartialWhenSessionInactiveTest,
	"AINpc.DialogueBubble.IgnoreStalePartialWhenSessionInactive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcDialogueBubbleWidgetSessionStartClearsStateTest::RunTest(const FString& Parameters)
{
	UAINpcComponent::ResetConcurrencyStateForTest();
	UAINpcComponent::SetDialogueDispatchBypassForTest(true);

	UAINpcComponent* Component = NewObject<UAINpcComponent>();
	UNpcDialogueBubbleWidget* Widget = NewObject<UNpcDialogueBubbleWidget>();
	TestNotNull(TEXT("Component should be created for session start clear test."), Component);
	TestNotNull(TEXT("Widget should be created for session start clear test."), Widget);
	if (!Component || !Widget)
	{
		UAINpcComponent::ResetConcurrencyStateForTest();
		return false;
	}

	Widget->bUseTypewriterEffect = false;
	Widget->BindToNpcComponent(Component);

	const bool bFirstStarted = Component->StartDialogue(TEXT("第一个响应。"));
	TestTrue(TEXT("First StartDialogue should succeed."), bFirstStarted);

	FLLMResponse FirstResponse;
	FirstResponse.RequestId = Component->GetActiveRequestIdForTest();
	FirstResponse.bSuccess = true;
	FirstResponse.Content = TEXT("旧响应");
	FirstResponse.ParsedResponse.Dialogue = FirstResponse.Content;
	Component->HandleRequestCompletedForTest(FirstResponse);

	TestEqual(TEXT("Widget should display first response."), Widget->GetDisplayedText().ToString(), TEXT("旧响应"));
	TestEqual(TEXT("Widget should store first full response."), Widget->GetFullResponseText(), TEXT("旧响应"));

	Component->EndDialogue();

	const bool bSecondStarted = Component->StartDialogue(TEXT("第二个响应。"));
	TestTrue(TEXT("Second StartDialogue should succeed."), bSecondStarted);
	TestEqual(TEXT("Starting a new session should clear previous displayed text."), Widget->GetDisplayedText().ToString(), TEXT(""));
	TestEqual(TEXT("Starting a new session should clear previous full text."), Widget->GetFullResponseText(), TEXT(""));

	Component->EndDialogue();
	UAINpcComponent::ResetConcurrencyStateForTest();
	return true;
}

bool FAINpcDialogueBubbleWidgetIgnoreStalePartialWhenSessionInactiveTest::RunTest(const FString& Parameters)
{
	UAINpcComponent::ResetConcurrencyStateForTest();
	UAINpcComponent::SetDialogueDispatchBypassForTest(true);

	UAINpcComponent* Component = NewObject<UAINpcComponent>();
	UNpcDialogueBubbleWidget* Widget = NewObject<UNpcDialogueBubbleWidget>();
	TestNotNull(TEXT("Component should be created for stale partial guard test."), Component);
	TestNotNull(TEXT("Widget should be created for stale partial guard test."), Widget);
	if (!Component || !Widget)
	{
		UAINpcComponent::ResetConcurrencyStateForTest();
		return false;
	}

	Widget->bUseTypewriterEffect = false;
	Widget->bEnableStreamingDisplay = true;
	Widget->StreamingDisplayMode = EStreamingDisplayMode::SentenceBySentence;
	Widget->BindToNpcComponent(Component);

	const bool bStarted = Component->StartDialogue(TEXT("初始提示。"));
	TestTrue(TEXT("StartDialogue should succeed for stale partial guard test."), bStarted);

	Widget->HandlePartialResponse(TEXT("活跃"));
	TestEqual(TEXT("Active dialogue partial should be accepted."), Widget->GetFullResponseText(), TEXT("活跃"));

	Component->EndDialogue();
	Widget->HandlePartialResponse(TEXT(" 过期"));
	TestEqual(TEXT("Late partial chunks should be ignored once session is inactive."),
		Widget->GetFullResponseText(), TEXT("活跃"));

	UAINpcComponent::ResetConcurrencyStateForTest();
	return true;
}

bool FAINpcDialogueBubbleWidgetStreamingSentenceTest::RunTest(const FString& Parameters)
{
	UNpcDialogueBubbleWidget* Widget = NewObject<UNpcDialogueBubbleWidget>();
	TestNotNull(TEXT("Widget should be created."), Widget);
	if (!Widget) return false;

	UE_LOG(LogAINpcUI, Log, TEXT("NpcDialogueBubbleWidget created for DialogueBubble streaming test"));

	Widget->bUseTypewriterEffect = false;
	Widget->bEnableStreamingDisplay = true;
	Widget->StreamingDisplayMode = EStreamingDisplayMode::SentenceBySentence;

	Widget->HandlePartialResponse(TEXT("你好"));
	TestEqual(TEXT("Incomplete sentence should not display."), Widget->GetFullResponseText(), TEXT(""));

	Widget->HandlePartialResponse(TEXT(" 世界。"));
	TestEqual(TEXT("Complete sentence should display."), Widget->GetFullResponseText(), TEXT("你好 世界。"));

	Widget->HandlePartialResponse(TEXT(" 下一句"));
	TestEqual(TEXT("Incomplete second sentence should not append."), Widget->GetFullResponseText(), TEXT("你好 世界。"));

	Widget->HandlePartialResponse(TEXT(" 来了！"));
	TestEqual(TEXT("Second complete sentence should append."), Widget->GetFullResponseText(), TEXT("你好 世界。 下一句 来了！"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDialogueBubbleWidgetStreamingAccumulatedTextClearTest,
	"AINpc.DialogueBubble.StreamingClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcDialogueBubbleWidgetStreamingAccumulatedTextClearTest::RunTest(const FString& Parameters)
{
	UNpcDialogueBubbleWidget* Widget = NewObject<UNpcDialogueBubbleWidget>();
	TestNotNull(TEXT("Widget should be created."), Widget);
	if (!Widget) return false;

	Widget->bUseTypewriterEffect = false;
	Widget->bEnableStreamingDisplay = true;
	Widget->StreamingDisplayMode = EStreamingDisplayMode::SentenceBySentence;

	Widget->HandlePartialResponse(TEXT("未完成"));
	TestEqual(TEXT("Incomplete text should not display."), Widget->GetFullResponseText(), TEXT(""));

	Widget->ShowResponseText(TEXT("新的非流式响应"));
	TestEqual(TEXT("ShowResponseText should clear accumulated text and show new response."),
		Widget->GetFullResponseText(), TEXT("新的非流式响应"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDialogueBubbleWidgetDuplicateTerminalKeepsBufferedStreamingTextTest,
	"AINpc.DialogueBubble.DuplicateTerminalKeepsBufferedStreamingText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcDialogueBubbleWidgetDuplicateTerminalKeepsBufferedStreamingTextTest::RunTest(const FString& Parameters)
{
	UNpcDialogueBubbleWidget* Widget = NewObject<UNpcDialogueBubbleWidget>();
	TestNotNull(TEXT("Widget should be created."), Widget);
	if (!Widget) return false;

	Widget->bUseTypewriterEffect = false;
	Widget->bEnableStreamingDisplay = true;
	Widget->StreamingDisplayMode = EStreamingDisplayMode::SentenceBySentence;

	Widget->HandlePartialResponse(TEXT("你好。"));
	TestEqual(TEXT("First sentence should flush immediately."), Widget->GetFullResponseText(), TEXT("你好。"));

	Widget->HandlePartialResponse(TEXT(" 尾部"));
	TestEqual(TEXT("Incomplete trailing text should remain buffered."), Widget->GetFullResponseText(), TEXT("你好。"));

	// Simulate a duplicate terminal callback where the final response repeats the
	// currently visible text while sentence-buffered streaming text still exists.
	Widget->ShowResponseText(TEXT("你好。"));
	Widget->HandlePartialResponse(TEXT("！"));

	TestEqual(TEXT("Buffered trailing text should be preserved across duplicate terminal callbacks."),
		Widget->GetFullResponseText(), TEXT("你好。 尾部！"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDialogueBubbleWidgetPunctuationPriorityTest,
	"AINpc.DialogueBubble.PunctuationPriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcDialogueBubbleWidgetPunctuationPriorityTest::RunTest(const FString& Parameters)
{
	UNpcDialogueBubbleWidget* Widget = NewObject<UNpcDialogueBubbleWidget>();
	TestNotNull(TEXT("Widget should be created."), Widget);
	if (!Widget) return false;

	Widget->bUseTypewriterEffect = false;
	Widget->bEnableStreamingDisplay = true;
	Widget->StreamingDisplayMode = EStreamingDisplayMode::SentenceBySentence;

	Widget->HandlePartialResponse(TEXT("第一句。 第二句？ 第三句！"));
	TestEqual(TEXT("Should find last punctuation (exclamation)."),
		Widget->GetFullResponseText(), TEXT("第一句。 第二句？ 第三句！"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDialogueBubbleWidgetSentenceFallbackFlushTest,
	"AINpc.DialogueBubble.SentenceFallbackFlush",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcDialogueBubbleWidgetSentenceFallbackFlushTest::RunTest(const FString& Parameters)
{
	UNpcDialogueBubbleWidget* Widget = NewObject<UNpcDialogueBubbleWidget>();
	TestNotNull(TEXT("Widget should be created."), Widget);
	if (!Widget) return false;

	Widget->bUseTypewriterEffect = false;
	Widget->bEnableStreamingDisplay = true;
	Widget->StreamingDisplayMode = EStreamingDisplayMode::SentenceBySentence;

	const FString LongChunk = FString::ChrN(160, TEXT('a'));
	Widget->HandlePartialResponse(LongChunk);
	TestEqual(TEXT("Long punctuation-less buffer should flush to visible text."),
		Widget->GetFullResponseText(), LongChunk);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDialogueBubbleWidgetMultilingualBoundaryTest,
	"AINpc.DialogueBubble.MultilingualBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDialogueBubbleWidgetCarriageReturnBoundaryTest,
	"AINpc.DialogueBubble.CarriageReturnBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDialogueBubbleWidgetCarriageReturnLineFeedBoundaryTest,
	"AINpc.DialogueBubble.CarriageReturnLineFeedBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDialogueBubbleWidgetEllipsisBoundaryTest,
	"AINpc.DialogueBubble.EllipsisBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcDialogueBubbleWidgetCarriageReturnBoundaryTest::RunTest(const FString& Parameters)
{
	UNpcDialogueBubbleWidget* Widget = NewObject<UNpcDialogueBubbleWidget>();
	TestNotNull(TEXT("Widget should be created."), Widget);
	if (!Widget) return false;

	Widget->bUseTypewriterEffect = false;
	Widget->bEnableStreamingDisplay = true;
	Widget->StreamingDisplayMode = EStreamingDisplayMode::SentenceBySentence;

	Widget->HandlePartialResponse(TEXT("第一行\r第二行"));
	TestEqual(TEXT("Carriage return should flush complete sentence chunk."),
		Widget->GetFullResponseText(), TEXT("第一行\r"));

	return true;
}

bool FAINpcDialogueBubbleWidgetCarriageReturnLineFeedBoundaryTest::RunTest(const FString& Parameters)
{
	UNpcDialogueBubbleWidget* Widget = NewObject<UNpcDialogueBubbleWidget>();
	TestNotNull(TEXT("Widget should be created."), Widget);
	if (!Widget) return false;

	Widget->bUseTypewriterEffect = false;
	Widget->bEnableStreamingDisplay = true;
	Widget->StreamingDisplayMode = EStreamingDisplayMode::SentenceBySentence;

	Widget->HandlePartialResponse(TEXT("第一行\r\n第二行"));
	TestEqual(TEXT("CRLF should flush as one boundary unit without leaving dangling LF."),
		Widget->GetFullResponseText(), TEXT("第一行\r\n"));

	Widget->HandlePartialResponse(TEXT(" 行。"));
	TestEqual(TEXT("Subsequent sentence should append cleanly after CRLF flush."),
		Widget->GetFullResponseText(), TEXT("第一行\r\n第二行 行。"));

	return true;
}

bool FAINpcDialogueBubbleWidgetEllipsisBoundaryTest::RunTest(const FString& Parameters)
{
	UNpcDialogueBubbleWidget* Widget = NewObject<UNpcDialogueBubbleWidget>();
	TestNotNull(TEXT("Widget should be created."), Widget);
	if (!Widget) return false;

	Widget->bUseTypewriterEffect = false;
	Widget->bEnableStreamingDisplay = true;
	Widget->StreamingDisplayMode = EStreamingDisplayMode::SentenceBySentence;

	Widget->HandlePartialResponse(TEXT("Wait\u2026next chunk"));
	TestEqual(TEXT("Unicode ellipsis should flush completed sentence chunk."),
		Widget->GetFullResponseText(), TEXT("Wait\u2026"));

	return true;
}

bool FAINpcDialogueBubbleWidgetMultilingualBoundaryTest::RunTest(const FString& Parameters)
{
	UNpcDialogueBubbleWidget* Widget = NewObject<UNpcDialogueBubbleWidget>();
	TestNotNull(TEXT("Widget should be created."), Widget);
	if (!Widget) return false;

	Widget->bUseTypewriterEffect = false;
	Widget->bEnableStreamingDisplay = true;
	Widget->StreamingDisplayMode = EStreamingDisplayMode::SentenceBySentence;

	Widget->HandlePartialResponse(TEXT("\u7B2C\u4E00\u884C\n\u7B2C\u4E8C\u884C"));
	TestEqual(TEXT("Line break should flush complete sentence chunk."),
		Widget->GetFullResponseText(), TEXT("\u7B2C\u4E00\u884C\n"));

	Widget->HandlePartialResponse(TEXT("\u4F60\u597D\u3002"));
	TestEqual(TEXT("Full-width Chinese period should flush buffered text."),
		Widget->GetFullResponseText(), TEXT("\u7B2C\u4E00\u884C\n\u7B2C\u4E8C\u884C\u4F60\u597D\u3002"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDialogueBubbleWidgetPartialDelegateTest,
	"AINpc.DialogueBubble.PartialDelegate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcDialogueBubbleWidgetPartialDelegateTest::RunTest(const FString& Parameters)
{
	UNpcDialogueBubbleWidget* Widget = NewObject<UNpcDialogueBubbleWidget>();
	TestNotNull(TEXT("Widget should be created."), Widget);
	if (!Widget) return false;

	FString LastPartialText;
	int32 PartialBroadcastCount = 0;
	Widget->OnPartialResponseNative().AddLambda([&LastPartialText, &PartialBroadcastCount](const FString& PartialText)
	{
		LastPartialText = PartialText;
		++PartialBroadcastCount;
	});

	Widget->bEnableStreamingDisplay = false;
	Widget->HandlePartialResponse(TEXT("块-甲"));
	Widget->bEnableStreamingDisplay = true;
	Widget->StreamingDisplayMode = EStreamingDisplayMode::SentenceBySentence;
	Widget->HandlePartialResponse(TEXT("块-乙"));

	TestEqual(TEXT("Native partial delegate should broadcast once per chunk."),
		PartialBroadcastCount, 2);
	TestEqual(TEXT("Native partial delegate should carry latest chunk."),
		LastPartialText, TEXT("块-乙"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDialogueBubbleWidgetTypewriterSpeedContractTest,
	"AINpc.DialogueBubble.TypewriterSpeedContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDialogueBubbleWidgetStreamingCharacterTypewriterContractTest,
	"AINpc.DialogueBubble.StreamingCharacterTypewriterContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDialogueBubbleWidgetTypewriterHitchClampTest,
	"AINpc.DialogueBubble.TypewriterHitchClamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcDialogueBubbleWidgetTypewriterSpeedContractTest::RunTest(const FString& Parameters)
{
	UNpcDialogueBubbleWidget* Widget = NewObject<UNpcDialogueBubbleWidget>();
	TestNotNull(TEXT("Widget should be created."), Widget);
	if (!Widget) return false;

	Widget->bUseTypewriterEffect = true;
	Widget->CharactersPerSecond = 30.0f;

	Widget->ShowResponseText(TEXT("你好"));
	TestEqual(TEXT("Typewriter should begin hidden before reveal starts."),
		Widget->GetDisplayedText().ToString(), TEXT(""));

	Widget->AdvanceTypewriterForTest(0.04f);
	TestEqual(TEXT("Typewriter should reveal one character at 30 chars/sec after 0.04s."),
		Widget->GetDisplayedText().ToString(), TEXT("你"));

	Widget->AdvanceTypewriterForTest(0.10f);
	TestEqual(TEXT("Typewriter should complete reveal when accumulated time reaches 4 chars."),
		Widget->GetDisplayedText().ToString(), TEXT("你好"));

	return true;
}

bool FAINpcDialogueBubbleWidgetStreamingCharacterTypewriterContractTest::RunTest(const FString& Parameters)
{
	UNpcDialogueBubbleWidget* Widget = NewObject<UNpcDialogueBubbleWidget>();
	TestNotNull(TEXT("Widget should be created."), Widget);
	if (!Widget) return false;

	Widget->bUseTypewriterEffect = true;
	Widget->CharactersPerSecond = 30.0f;
	Widget->bEnableStreamingDisplay = true;
	Widget->StreamingDisplayMode = EStreamingDisplayMode::CharacterByCharacter;

	Widget->HandlePartialResponse(TEXT("你好"));
	TestEqual(TEXT("Character streaming should queue full response text immediately."),
		Widget->GetFullResponseText(), TEXT("你好"));
	TestEqual(TEXT("Character streaming should still use typewriter for display."),
		Widget->GetDisplayedText().ToString(), TEXT(""));

	Widget->AdvanceTypewriterForTest(0.04f);
	TestEqual(TEXT("Typewriter should reveal one character per 0.04s at 30 chars/sec."),
		Widget->GetDisplayedText().ToString(), TEXT("你"));

	Widget->HandlePartialResponse(TEXT("吗"));
	TestEqual(TEXT("Additional stream chunks should extend queued full response text."),
		Widget->GetFullResponseText(), TEXT("你好吗"));
	TestEqual(TEXT("Display should preserve revealed progress until next tick."),
		Widget->GetDisplayedText().ToString(), TEXT("你"));

	Widget->AdvanceTypewriterForTest(0.04f);
	TestEqual(TEXT("Typewriter should continue reveal from existing progress."),
		Widget->GetDisplayedText().ToString(), TEXT("你好"));

	Widget->AdvanceTypewriterForTest(0.10f);
	TestEqual(TEXT("Typewriter should reveal remaining queued characters."),
		Widget->GetDisplayedText().ToString(), TEXT("你好吗"));

	return true;
}

bool FAINpcDialogueBubbleWidgetTypewriterHitchClampTest::RunTest(const FString& Parameters)
{
	UNpcDialogueBubbleWidget* Widget = NewObject<UNpcDialogueBubbleWidget>();
	TestNotNull(TEXT("Widget should be created."), Widget);
	if (!Widget) return false;

	Widget->bUseTypewriterEffect = true;
	Widget->CharactersPerSecond = 30.0f;

	Widget->ShowResponseText(TEXT("你好吗"));
	Widget->AdvanceTypewriterForTest(0.5f);

	TestEqual(TEXT("Large frame hitches should clamp reveal progress instead of revealing the full line in one step."),
		Widget->GetDisplayedText().ToString(), TEXT("你好"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDialogueBubbleWidgetShowResponseTextStreamingExtensionKeepsProgressTest,
	"AINpc.DialogueBubble.ShowResponseTextStreamingExtensionKeepsProgress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcDialogueBubbleWidgetShowResponseTextStreamingExtensionKeepsProgressTest::RunTest(const FString& Parameters)
{
	UNpcDialogueBubbleWidget* Widget = NewObject<UNpcDialogueBubbleWidget>();
	TestNotNull(TEXT("Widget should be created."), Widget);
	if (!Widget) return false;

	Widget->bUseTypewriterEffect = true;
	Widget->CharactersPerSecond = 30.0f;
	Widget->bEnableStreamingDisplay = true;
	Widget->StreamingDisplayMode = EStreamingDisplayMode::Immediate;

	Widget->ShowResponseText(TEXT("你好"));
	Widget->AdvanceTypewriterForTest(0.04f);
	TestEqual(TEXT("Typewriter should reveal one character before extension."),
		Widget->GetDisplayedText().ToString(), TEXT("你"));

	Widget->ShowResponseText(TEXT("你好吗"));
	TestEqual(TEXT("Streaming extension should keep in-flight reveal progress without restart flicker."),
		Widget->GetDisplayedText().ToString(), TEXT("你"));

	Widget->AdvanceTypewriterForTest(0.04f);
	TestEqual(TEXT("Typewriter should continue from prior progress after extension."),
		Widget->GetDisplayedText().ToString(), TEXT("你好"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDialogueBubbleWidgetIgnoresEmptyPartialChunksTest,
	"AINpc.DialogueBubble.IgnoresEmptyPartialChunks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDialogueBubbleWidgetEmptyTerminalPreservesStreamedTextTest,
	"AINpc.DialogueBubble.EmptyTerminalPreservesStreamedText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDialogueBubbleWidgetFinalResponseMergeDoesNotDuplicateStreamedTextTest,
	"AINpc.DialogueBubble.FinalResponseMergeDoesNotDuplicateStreamedText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDialogueBubbleWidgetSessionRestartClearsBufferedStreamingStateTest,
	"AINpc.DialogueBubble.SessionRestartClearsBufferedStreamingState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcDialogueBubbleWidgetIgnoresEmptyPartialChunksTest::RunTest(const FString& Parameters)
{
	UNpcDialogueBubbleWidget* Widget = NewObject<UNpcDialogueBubbleWidget>();
	TestNotNull(TEXT("Widget should be created."), Widget);
	if (!Widget) return false;

	Widget->bUseTypewriterEffect = false;
	Widget->bEnableStreamingDisplay = true;
	Widget->StreamingDisplayMode = EStreamingDisplayMode::SentenceBySentence;

	int32 PartialBroadcastCount = 0;
	Widget->OnPartialResponseNative().AddLambda([&PartialBroadcastCount](const FString&)
	{
		++PartialBroadcastCount;
	});

	Widget->HandlePartialResponse(TEXT(""));
	TestEqual(TEXT("Empty chunks should not mutate streamed response text."),
		Widget->GetFullResponseText(), TEXT(""));
	TestEqual(TEXT("Empty chunks should not broadcast partial-response delegates."),
		PartialBroadcastCount, 0);

	Widget->HandlePartialResponse(TEXT("你"));
	TestEqual(TEXT("Incomplete sentence chunks should stay buffered until a boundary is received."),
		Widget->GetFullResponseText(), TEXT(""));
	TestEqual(TEXT("Non-empty chunks should broadcast partial-response delegates."),
		PartialBroadcastCount, 1);

	return true;
}

bool FAINpcDialogueBubbleWidgetEmptyTerminalPreservesStreamedTextTest::RunTest(const FString& Parameters)
{
	UNpcDialogueBubbleWidget* CharModeWidget = NewObject<UNpcDialogueBubbleWidget>();
	TestNotNull(TEXT("Character-mode widget should be created."), CharModeWidget);
	if (!CharModeWidget)
	{
		return false;
	}

	CharModeWidget->bUseTypewriterEffect = false;
	CharModeWidget->bEnableStreamingDisplay = true;
	CharModeWidget->StreamingDisplayMode = EStreamingDisplayMode::Immediate;
	CharModeWidget->HandlePartialResponse(TEXT("你好"));
	CharModeWidget->ShowResponseText(TEXT(""));

	TestEqual(TEXT("Empty terminal callback should keep character-mode streamed text."),
		CharModeWidget->GetFullResponseText(), TEXT("你好"));
	TestEqual(TEXT("Displayed text should remain visible after empty terminal callback."),
		CharModeWidget->GetDisplayedText().ToString(), TEXT("你好"));

	UNpcDialogueBubbleWidget* SentenceModeWidget = NewObject<UNpcDialogueBubbleWidget>();
	TestNotNull(TEXT("Sentence-mode widget should be created."), SentenceModeWidget);
	if (!SentenceModeWidget)
	{
		return false;
	}

	SentenceModeWidget->bUseTypewriterEffect = false;
	SentenceModeWidget->bEnableStreamingDisplay = true;
	SentenceModeWidget->StreamingDisplayMode = EStreamingDisplayMode::SentenceBySentence;
	SentenceModeWidget->HandlePartialResponse(TEXT("没有标点的缓冲文本"));
	SentenceModeWidget->ShowResponseText(TEXT(""));

	TestEqual(TEXT("Empty terminal callback should flush and keep sentence buffer content."),
		SentenceModeWidget->GetFullResponseText(), TEXT("没有标点的缓冲文本"));
	TestEqual(TEXT("Displayed sentence buffer content should remain visible after empty terminal callback."),
		SentenceModeWidget->GetDisplayedText().ToString(), TEXT("没有标点的缓冲文本"));

	return true;
}

bool FAINpcDialogueBubbleWidgetFinalResponseMergeDoesNotDuplicateStreamedTextTest::RunTest(const FString& Parameters)
{
	UNpcDialogueBubbleWidget* Widget = NewObject<UNpcDialogueBubbleWidget>();
	TestNotNull(TEXT("Widget should be created."), Widget);
	if (!Widget)
	{
		return false;
	}

	Widget->bUseTypewriterEffect = false;
	Widget->bEnableStreamingDisplay = true;
	Widget->StreamingDisplayMode = EStreamingDisplayMode::SentenceBySentence;

	Widget->HandlePartialResponse(TEXT("第一句。"));
	Widget->HandlePartialResponse(TEXT("第二句"));
	Widget->ShowResponseText(TEXT("第一句。第二句。"));

	TestEqual(TEXT("Final response should replace/merge streamed text without duplication."),
		Widget->GetFullResponseText(), TEXT("第一句。第二句。"));
	TestEqual(TEXT("Displayed final response should not duplicate streamed prefix."),
		Widget->GetDisplayedText().ToString(), TEXT("第一句。第二句。"));

	return true;
}

bool FAINpcDialogueBubbleWidgetSessionRestartClearsBufferedStreamingStateTest::RunTest(const FString& Parameters)
{
	UAINpcComponent::ResetConcurrencyStateForTest();
	UAINpcComponent::SetDialogueDispatchBypassForTest(true);

	UNpcDialogueBubbleWidget* Widget = NewObject<UNpcDialogueBubbleWidget>();
	TestNotNull(TEXT("Widget should be created."), Widget);
	if (!Widget)
	{
		return false;
	}
	UAINpcComponent* Component = NewObject<UAINpcComponent>();
	TestNotNull(TEXT("Component should be created."), Component);
	if (!Component)
	{
		UAINpcComponent::ResetConcurrencyStateForTest();
		return false;
	}

	Widget->bUseTypewriterEffect = false;
	Widget->bEnableStreamingDisplay = true;
	Widget->StreamingDisplayMode = EStreamingDisplayMode::SentenceBySentence;
	Widget->BindToNpcComponent(Component);

	Widget->HandlePartialResponse(TEXT("上一轮未完成"));
	Component->StartDialogue(TEXT("新一轮 prompt"));
	Widget->HandlePartialResponse(TEXT("新一轮。"));

	TestEqual(TEXT("Session restart should clear old buffered chunks before accepting new text."),
		Widget->GetFullResponseText(), TEXT("新一轮。"));
	TestEqual(TEXT("Displayed session restart text should only contain the new request."),
		Widget->GetDisplayedText().ToString(), TEXT("新一轮。"));

	Component->EndDialogue();
	UAINpcComponent::ResetConcurrencyStateForTest();

	return true;
}

#endif
