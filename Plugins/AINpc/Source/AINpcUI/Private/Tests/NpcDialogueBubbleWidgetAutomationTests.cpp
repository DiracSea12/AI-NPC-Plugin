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

	const bool bFirstStarted = Component->StartDialogue(TEXT("First response should update widget."));
	TestTrue(TEXT("First StartDialogue should succeed."), bFirstStarted);

	FLLMResponse FirstResponse;
	FirstResponse.RequestId = Component->GetActiveRequestIdForTest();
	FirstResponse.bSuccess = true;
	FirstResponse.Content = TEXT("Hello from bound component.");
	Component->HandleRequestCompletedForTest(FirstResponse);

	TestEqual(TEXT("Bound widget should receive full response text."), Widget->GetFullResponseText(), FirstResponse.Content);
	TestEqual(TEXT("Bound widget should display response text."), Widget->GetDisplayedText().ToString(), FirstResponse.Content);

	Widget->BindToNpcComponent(nullptr);

	const bool bSecondStarted = Component->StartDialogue(TEXT("Second response should not update widget after unbind."));
	TestTrue(TEXT("Second StartDialogue should succeed."), bSecondStarted);

	FLLMResponse SecondResponse;
	SecondResponse.RequestId = Component->GetActiveRequestIdForTest();
	SecondResponse.bSuccess = true;
	SecondResponse.Content = TEXT("This text should not appear in widget.");
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

	const bool bStarted = Component->StartDialogue(TEXT("Prompt before widget bind."));
	TestTrue(TEXT("StartDialogue should succeed for bind resync test."), bStarted);

	FLLMResponse Response;
	Response.RequestId = Component->GetActiveRequestIdForTest();
	Response.bSuccess = true;
	Response.Content = TEXT("Response generated before widget bind.");
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

	const bool bStarted = FirstComponent->StartDialogue(TEXT("Prompt on first component."));
	TestTrue(TEXT("StartDialogue should succeed on first component."), bStarted);

	FLLMResponse FirstResponse;
	FirstResponse.RequestId = FirstComponent->GetActiveRequestIdForTest();
	FirstResponse.bSuccess = true;
	FirstResponse.Content = TEXT("Response from first component.");
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

	const bool bStarted = Component->StartDialogue(TEXT("Prompt before same-component rebind."));
	TestTrue(TEXT("StartDialogue should succeed for same-component active rebind test."), bStarted);

	// Simulate stale widget text from prior state while an active dialogue has no response yet.
	Widget->ShowResponseText(TEXT("stale text"));
	TestEqual(TEXT("Widget should show stale text before explicit same-component resync."),
		Widget->GetDisplayedText().ToString(), TEXT("stale text"));

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

	const bool bFirstStarted = Component->StartDialogue(TEXT("First response."));
	TestTrue(TEXT("First StartDialogue should succeed."), bFirstStarted);

	FLLMResponse FirstResponse;
	FirstResponse.RequestId = Component->GetActiveRequestIdForTest();
	FirstResponse.bSuccess = true;
	FirstResponse.Content = TEXT("Old response");
	Component->HandleRequestCompletedForTest(FirstResponse);

	TestEqual(TEXT("Widget should display first response."), Widget->GetDisplayedText().ToString(), TEXT("Old response"));
	TestEqual(TEXT("Widget should store first full response."), Widget->GetFullResponseText(), TEXT("Old response"));

	Component->EndDialogue();

	const bool bSecondStarted = Component->StartDialogue(TEXT("Second response."));
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

	const bool bStarted = Component->StartDialogue(TEXT("Initial prompt."));
	TestTrue(TEXT("StartDialogue should succeed for stale partial guard test."), bStarted);

	Widget->HandlePartialResponse(TEXT("active"));
	TestEqual(TEXT("Active dialogue partial should be accepted."), Widget->GetFullResponseText(), TEXT("active"));

	Component->EndDialogue();
	Widget->HandlePartialResponse(TEXT(" stale"));
	TestEqual(TEXT("Late partial chunks should be ignored once session is inactive."),
		Widget->GetFullResponseText(), TEXT("active"));

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

	Widget->HandlePartialResponse(TEXT("Hello"));
	TestEqual(TEXT("Incomplete sentence should not display."), Widget->GetFullResponseText(), TEXT(""));

	Widget->HandlePartialResponse(TEXT(" world."));
	TestEqual(TEXT("Complete sentence should display."), Widget->GetFullResponseText(), TEXT("Hello world."));

	Widget->HandlePartialResponse(TEXT(" Next"));
	TestEqual(TEXT("Incomplete second sentence should not append."), Widget->GetFullResponseText(), TEXT("Hello world."));

	Widget->HandlePartialResponse(TEXT(" sentence!"));
	TestEqual(TEXT("Second complete sentence should append."), Widget->GetFullResponseText(), TEXT("Hello world. Next sentence!"));

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

	Widget->HandlePartialResponse(TEXT("Incomplete"));
	TestEqual(TEXT("Incomplete text should not display."), Widget->GetFullResponseText(), TEXT(""));

	Widget->ShowResponseText(TEXT("New non-streaming response"));
	TestEqual(TEXT("ShowResponseText should clear accumulated text and show new response."),
		Widget->GetFullResponseText(), TEXT("New non-streaming response"));

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

	Widget->HandlePartialResponse(TEXT("Hello."));
	TestEqual(TEXT("First sentence should flush immediately."), Widget->GetFullResponseText(), TEXT("Hello."));

	Widget->HandlePartialResponse(TEXT(" trailing"));
	TestEqual(TEXT("Incomplete trailing text should remain buffered."), Widget->GetFullResponseText(), TEXT("Hello."));

	// Simulate a duplicate terminal callback where the final response repeats the
	// currently visible text while sentence-buffered streaming text still exists.
	Widget->ShowResponseText(TEXT("Hello."));
	Widget->HandlePartialResponse(TEXT("!"));

	TestEqual(TEXT("Buffered trailing text should be preserved across duplicate terminal callbacks."),
		Widget->GetFullResponseText(), TEXT("Hello. trailing!"));

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

	Widget->HandlePartialResponse(TEXT("First. Second? Third!"));
	TestEqual(TEXT("Should find last punctuation (exclamation)."),
		Widget->GetFullResponseText(), TEXT("First. Second? Third!"));

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

	Widget->HandlePartialResponse(TEXT("First line\rSecond line"));
	TestEqual(TEXT("Carriage return should flush complete sentence chunk."),
		Widget->GetFullResponseText(), TEXT("First line\r"));

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

	Widget->HandlePartialResponse(TEXT("First line\r\nSecond"));
	TestEqual(TEXT("CRLF should flush as one boundary unit without leaving dangling LF."),
		Widget->GetFullResponseText(), TEXT("First line\r\n"));

	Widget->HandlePartialResponse(TEXT(" line."));
	TestEqual(TEXT("Subsequent sentence should append cleanly after CRLF flush."),
		Widget->GetFullResponseText(), TEXT("First line\r\nSecond line."));

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

	Widget->HandlePartialResponse(TEXT("First line\nSecond line"));
	TestEqual(TEXT("Line break should flush complete sentence chunk."),
		Widget->GetFullResponseText(), TEXT("First line\n"));

	Widget->HandlePartialResponse(TEXT("\u4F60\u597D\u3002"));
	TestEqual(TEXT("Full-width Chinese period should flush buffered text."),
		Widget->GetFullResponseText(), TEXT("First line\nSecond line\u4F60\u597D\u3002"));

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
	Widget->HandlePartialResponse(TEXT("chunk-a"));
	Widget->bEnableStreamingDisplay = true;
	Widget->StreamingDisplayMode = EStreamingDisplayMode::SentenceBySentence;
	Widget->HandlePartialResponse(TEXT("chunk-b"));

	TestEqual(TEXT("Native partial delegate should broadcast once per chunk."),
		PartialBroadcastCount, 2);
	TestEqual(TEXT("Native partial delegate should carry latest chunk."),
		LastPartialText, TEXT("chunk-b"));

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

	Widget->ShowResponseText(TEXT("abcd"));
	TestEqual(TEXT("Typewriter should begin hidden before reveal starts."),
		Widget->GetDisplayedText().ToString(), TEXT(""));

	Widget->AdvanceTypewriterForTest(0.04f);
	TestEqual(TEXT("Typewriter should reveal one character at 30 chars/sec after 0.04s."),
		Widget->GetDisplayedText().ToString(), TEXT("a"));

	Widget->AdvanceTypewriterForTest(0.10f);
	TestEqual(TEXT("Typewriter should complete reveal when accumulated time reaches 4 chars."),
		Widget->GetDisplayedText().ToString(), TEXT("abcd"));

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

	Widget->HandlePartialResponse(TEXT("ab"));
	TestEqual(TEXT("Character streaming should queue full response text immediately."),
		Widget->GetFullResponseText(), TEXT("ab"));
	TestEqual(TEXT("Character streaming should still use typewriter for display."),
		Widget->GetDisplayedText().ToString(), TEXT(""));

	Widget->AdvanceTypewriterForTest(0.04f);
	TestEqual(TEXT("Typewriter should reveal one character per 0.04s at 30 chars/sec."),
		Widget->GetDisplayedText().ToString(), TEXT("a"));

	Widget->HandlePartialResponse(TEXT("cd"));
	TestEqual(TEXT("Additional stream chunks should extend queued full response text."),
		Widget->GetFullResponseText(), TEXT("abcd"));
	TestEqual(TEXT("Display should preserve revealed progress until next tick."),
		Widget->GetDisplayedText().ToString(), TEXT("a"));

	Widget->AdvanceTypewriterForTest(0.04f);
	TestEqual(TEXT("Typewriter should continue reveal from existing progress."),
		Widget->GetDisplayedText().ToString(), TEXT("ab"));

	Widget->AdvanceTypewriterForTest(0.10f);
	TestEqual(TEXT("Typewriter should reveal remaining queued characters."),
		Widget->GetDisplayedText().ToString(), TEXT("abcd"));

	return true;
}

bool FAINpcDialogueBubbleWidgetTypewriterHitchClampTest::RunTest(const FString& Parameters)
{
	UNpcDialogueBubbleWidget* Widget = NewObject<UNpcDialogueBubbleWidget>();
	TestNotNull(TEXT("Widget should be created."), Widget);
	if (!Widget) return false;

	Widget->bUseTypewriterEffect = true;
	Widget->CharactersPerSecond = 30.0f;

	Widget->ShowResponseText(TEXT("abcdef"));
	Widget->AdvanceTypewriterForTest(0.5f);

	TestEqual(TEXT("Large frame hitches should clamp reveal progress instead of revealing the full line in one step."),
		Widget->GetDisplayedText().ToString(), TEXT("abc"));

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

	Widget->ShowResponseText(TEXT("ab"));
	Widget->AdvanceTypewriterForTest(0.04f);
	TestEqual(TEXT("Typewriter should reveal one character before extension."),
		Widget->GetDisplayedText().ToString(), TEXT("a"));

	Widget->ShowResponseText(TEXT("abcd"));
	TestEqual(TEXT("Streaming extension should keep in-flight reveal progress without restart flicker."),
		Widget->GetDisplayedText().ToString(), TEXT("a"));

	Widget->AdvanceTypewriterForTest(0.04f);
	TestEqual(TEXT("Typewriter should continue from prior progress after extension."),
		Widget->GetDisplayedText().ToString(), TEXT("ab"));

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

	Widget->HandlePartialResponse(TEXT("a"));
	TestEqual(TEXT("Non-empty chunks should still be accepted."),
		Widget->GetFullResponseText(), TEXT("a"));
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
	CharModeWidget->HandlePartialResponse(TEXT("Hello"));
	CharModeWidget->ShowResponseText(TEXT(""));

	TestEqual(TEXT("Empty terminal callback should keep character-mode streamed text."),
		CharModeWidget->GetFullResponseText(), TEXT("Hello"));
	TestEqual(TEXT("Displayed text should remain visible after empty terminal callback."),
		CharModeWidget->GetDisplayedText().ToString(), TEXT("Hello"));

	UNpcDialogueBubbleWidget* SentenceModeWidget = NewObject<UNpcDialogueBubbleWidget>();
	TestNotNull(TEXT("Sentence-mode widget should be created."), SentenceModeWidget);
	if (!SentenceModeWidget)
	{
		return false;
	}

	SentenceModeWidget->bUseTypewriterEffect = false;
	SentenceModeWidget->bEnableStreamingDisplay = true;
	SentenceModeWidget->StreamingDisplayMode = EStreamingDisplayMode::SentenceBySentence;
	SentenceModeWidget->HandlePartialResponse(TEXT("Buffered text without punctuation"));
	SentenceModeWidget->ShowResponseText(TEXT(""));

	TestEqual(TEXT("Empty terminal callback should flush and keep sentence buffer content."),
		SentenceModeWidget->GetFullResponseText(), TEXT("Buffered text without punctuation"));
	TestEqual(TEXT("Displayed sentence buffer content should remain visible after empty terminal callback."),
		SentenceModeWidget->GetDisplayedText().ToString(), TEXT("Buffered text without punctuation"));

	return true;
}

#endif

