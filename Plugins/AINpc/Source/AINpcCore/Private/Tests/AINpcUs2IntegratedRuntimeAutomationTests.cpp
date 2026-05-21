#include "Misc/AutomationTest.h"

#include "Animation/AnimMontage.h"
#include "Async/TaskGraphInterfaces.h"
#include "Components/AINpcComponent.h"
#include "Data/NpcPersonaDataAsset.h"
#include "Events/NpcEventPayloadBlueprintLibrary.h"
#include "Events/NpcEventSubsystem.h"
#include "HAL/PlatformProcess.h"
#include "LLM/LLMResponseParser.h"
#include "SmartObjectBridge/AINpcSmartObjectRuntimeExecutor.h"

#if defined(WITH_DEV_AUTOMATION_TESTS) && WITH_DEV_AUTOMATION_TESTS

namespace
{
	void PumpGameThreadTasksForIntegratedRuntimeTest()
	{
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		FPlatformProcess::Sleep(0.01f);
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
	}

	FGameplayTag RequireIntegratedRuntimeTag(FAutomationTestBase& Test, const TCHAR* TagName)
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TagName), false);
		Test.TestTrue(FString::Printf(TEXT("Gameplay tag '%s' must exist for US-2 integrated runtime diagnostics."), TagName), Tag.IsValid());
		return Tag;
	}

	FParsedLLMResponse ParseStructuredOpenAIResponseForIntegratedRuntimeTest(
		FAutomationTestBase& Test,
		const FString& TargetId)
	{
		const FString EscapedTargetId = TargetId.ReplaceCharWithEscapedChar();
		const FString ResponseBody = FString::Printf(
			TEXT(R"({"choices":[{"message":{"content":"{\"dialogue\":\"Wrong content path.\",\"actions\":[{\"type\":\"Action.Inspect\",\"target\":\"Wrong.Target\"}],\"emotion_delta\":{\"valence\":-1.0,\"arousal\":-1.0,\"dominance\":-1.0},\"relationship_delta\":{\"affinity\":-1.0,\"trust\":-1.0,\"familiarity\":-1.0}}","tool_calls":[{"id":"call_1","type":"function","function":{"name":"emit_npc_response","arguments":"{\"dialogue\":\"Event acknowledged; inspecting the visible SmartObject.\",\"actions\":[{\"type\":\"Action.Inspect\",\"target\":\"%s\"}],\"emotion_delta\":{\"valence\":0.2,\"arousal\":0.3,\"dominance\":0.1},\"relationship_delta\":{\"affinity\":0.1,\"trust\":0.2,\"familiarity\":0.3}}"}}]}}]})"),
			*EscapedTargetId);

		FParsedLLMResponse ParsedResponse;
		FString ErrorMessage;
		Test.TestTrue(TEXT("Structured provider output should parse through the function/tool tier."), FLLMResponseParser::ParseOpenAIChatCompletion(ResponseBody, ParsedResponse, ErrorMessage));
		Test.TestTrue(TEXT("Structured provider output should not report a parser error."), ErrorMessage.IsEmpty());
		return ParsedResponse;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2IntegratedRuntimeDiagnosticTest,
	"AINpc.Core.US2.IntegratedRuntime.EventStructuredStreamingSmartObjectDiagnostic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2IntegratedRuntimeDiagnosticTest::RunTest(const FString& Parameters)
{
	AddInfo(TEXT("Diagnostic/runtime-system coverage only. This test uses ForTest seams and synthetic callbacks; it is not final US-2 visible acceptance."));

	AActor* OwnerActor = NewObject<AActor>();
	TestNotNull(TEXT("Owner actor should be available for integrated runtime diagnostic."), OwnerActor);
	UAINpcComponent* Component = OwnerActor ? NewObject<UAINpcComponent>(OwnerActor) : nullptr;
	TestNotNull(TEXT("AINpc component should be available for integrated runtime diagnostic."), Component);
	if (!OwnerActor || !Component)
	{
		return false;
	}

	UAINpcComponent::ResetConcurrencyStateForTest();

	const FGameplayTag RouteTag = RequireIntegratedRuntimeTag(*this, TEXT("AINpc.Tests.Route.Match"));
	if (!RouteTag.IsValid())
	{
		return false;
	}

	UNpcPersonaDataAsset* Persona = NewObject<UNpcPersonaDataAsset>();
	TestNotNull(TEXT("Persona should be available for integrated runtime delay masking diagnostic."), Persona);
	if (!Persona)
	{
		return false;
	}
	Persona->DelayFillerTexts.Add(FText::FromString(TEXT("diagnostic event filler")));
	Persona->DelayFillerThreshold = 30.0f;
	Persona->InspectDelayMaskingMontages.Add(NewObject<UAnimMontage>(Persona));
	Component->SetPersonaData(Persona);
	Component->EventSubscriptionTags.AddTag(RouteTag);
	Component->SetDialogueTestState(true, true, FGuid::NewGuid(), 0, ENpcDialogueState::WaitingForLLM);

	int32 DelayMaskingStartCount = 0;
	int32 DelayMaskingEndCount = 0;
	Component->OnDelayMaskingStartNative().AddLambda(
		[&DelayMaskingStartCount](UAnimMontage*, const FText&)
		{
			++DelayMaskingStartCount;
		});
	Component->OnDelayMaskingEndNative().AddLambda(
		[&DelayMaskingEndCount]()
		{
			++DelayMaskingEndCount;
		});

	FGameplayTagContainer RoutingTags;
	RoutingTags.AddTag(RouteTag);
	const FNpcEventMessage EventMessage = UNpcEventPayloadBlueprintLibrary::MakeGiftEventMessage(
		RouteTag,
		RoutingTags,
		nullptr,
		OwnerActor,
		FGameplayTag(),
		1);
	Component->HandleNpcEventStageDispatchedForTest(EventMessage, ENpcEventDispatchStage::DelayMasking);

	TestEqual(TEXT("Event-triggered delay masking should start once while the dialogue request is waiting."), DelayMaskingStartCount, 1);
	TestTrue(TEXT("Event-triggered delay masking should mark component state active."), Component->IsDelayMaskingActive());

	const FGuid ActiveRequestId = Component->GetActiveRequestIdForTest();
	TArray<FString> PartialTexts;
	Component->OnDialoguePartialResponseNative().AddLambda(
		[&PartialTexts](const FString& PartialText)
		{
			PartialTexts.Add(PartialText);
		});

	FLLMRequest StreamingRequest = Component->BuildStreamingRequestForTest();
	TestTrue(TEXT("Integrated diagnostic should install a partial-response callback."), static_cast<bool>(StreamingRequest.StreamCallback));
	if (!StreamingRequest.StreamCallback)
	{
		return false;
	}

	FLLMStreamChunk PartialChunk;
	PartialChunk.RequestId = ActiveRequestId;
	PartialChunk.Content = TEXT("partial US-2 evidence");
	StreamingRequest.StreamCallback(PartialChunk);
	PumpGameThreadTasksForIntegratedRuntimeTest();

	TestEqual(TEXT("Active request partial chunk should reach the component delegate once."), PartialTexts.Num(), 1);
	if (PartialTexts.Num() == 1)
	{
		TestEqual(TEXT("Partial-response text should be preserved."), PartialTexts[0], FString(TEXT("partial US-2 evidence")));
	}

	const FString LegalTarget = TEXT("SO_SLOT_Integrated");
	const FParsedLLMResponse ParsedResponse = ParseStructuredOpenAIResponseForIntegratedRuntimeTest(*this, LegalTarget);
	TestEqual(TEXT("Integrated parser diagnostic should use the function-calling tier."), ParsedResponse.ParseTier, ELLMResponseParseTier::FunctionCalling);
	TestEqual(TEXT("Integrated parser diagnostic should produce one action intent."), ParsedResponse.Actions.Num(), 1);
	TestEqual(TEXT("Integrated parser diagnostic should preserve dialogue text."), ParsedResponse.Dialogue, FString(TEXT("Event acknowledged; inspecting the visible SmartObject.")));

	FLLMResponse ProviderResponse;
	ProviderResponse.RequestId = ActiveRequestId;
	ProviderResponse.bSuccess = true;
	ProviderResponse.Content = ParsedResponse.Dialogue;
	ProviderResponse.ParsedResponse = ParsedResponse;

	int32 ResponseCount = 0;
	Component->OnDialogueResponseNative().AddLambda(
		[&ResponseCount](const FString&)
		{
			++ResponseCount;
		});
	Component->HandleRequestCompletedForTest(ProviderResponse);

	TestEqual(TEXT("Structured completion should broadcast exactly one dialogue response."), ResponseCount, 1);
	TestEqual(TEXT("Structured completion should end event-driven delay masking exactly once."), DelayMaskingEndCount, 1);
	TestFalse(TEXT("Structured completion should clear active delay masking state."), Component->IsDelayMaskingActive());
	TestEqual(TEXT("Structured completion should leave the dialogue in speaking state."), Component->GetDialogueState(), ENpcDialogueState::Speaking);

	FNpcAction LatestAction;
	TestTrue(TEXT("Structured completion should store an executable action intent."), Component->TryGetLatestActionIntent(LatestAction));
	TestEqual(TEXT("Stored action target should match the structured provider target."), LatestAction.Target, LegalTarget);

	FString RequestedTarget;
	FString FailureReason;
	TestTrue(
		TEXT("Legal structured action should pass inline SmartObject validation."),
		FAINpcSmartObjectRuntimeExecutor::ValidateSmartObjectActionIntent(
			LatestAction,
			{LegalTarget},
			RequestedTarget,
			FailureReason));
	TestEqual(TEXT("Accepted target should be returned for execution."), RequestedTarget, LegalTarget);
	TestTrue(TEXT("Accepted action should not leave a failure reason."), FailureReason.IsEmpty());

	FNpcAction InvalidAction = LatestAction;
	InvalidAction.Target = TEXT("SO_SLOT_NotLegal");
	RequestedTarget.Reset();
	FailureReason.Reset();
	TestFalse(
		TEXT("Invalid structured action target should be rejected before claim/use."),
		FAINpcSmartObjectRuntimeExecutor::ValidateSmartObjectActionIntent(
			InvalidAction,
			{LegalTarget},
			RequestedTarget,
			FailureReason));
	TestFalse(TEXT("Rejected action should report a diagnostic reason."), FailureReason.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2IntegratedRuntimeUs1NonStreamingPreservedTest,
	"AINpc.Core.US2.IntegratedRuntime.US1NonStreamingDialoguePreserved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2IntegratedRuntimeUs1NonStreamingPreservedTest::RunTest(const FString& Parameters)
{
	AddInfo(TEXT("Diagnostic/runtime-system coverage only. This proves the non-streaming completion path still produces US-1-style dialogue state without final visible acceptance."));

	UAINpcComponent* Component = NewObject<UAINpcComponent>();
	TestNotNull(TEXT("AINpc component should be available for non-streaming preservation diagnostic."), Component);
	if (!Component)
	{
		return false;
	}

	UAINpcComponent::ResetConcurrencyStateForTest();

	const FGuid ActiveRequestId = FGuid::NewGuid();
	Component->SetDialogueTestState(true, true, ActiveRequestId, 0, ENpcDialogueState::WaitingForLLM);

	int32 ResponseCount = 0;
	int32 PartialCount = 0;
	int32 DelayMaskingEndCount = 0;
	Component->OnDialogueResponseNative().AddLambda(
		[&ResponseCount](const FString& ResponseText)
		{
			if (!ResponseText.IsEmpty())
			{
				++ResponseCount;
			}
		});
	Component->OnDialoguePartialResponseNative().AddLambda(
		[&PartialCount](const FString&)
		{
			++PartialCount;
		});
	Component->OnDelayMaskingEndNative().AddLambda(
		[&DelayMaskingEndCount]()
		{
			++DelayMaskingEndCount;
		});

	FParsedLLMResponse ParsedResponse;
	ParsedResponse.Dialogue = TEXT("Plain US-1 dialogue still works.");
	ParsedResponse.ParseTier = ELLMResponseParseTier::PlainText;
	ParsedResponse.bParsedAsJson = false;

	FLLMResponse Response;
	Response.RequestId = ActiveRequestId;
	Response.bSuccess = true;
	Response.Content = ParsedResponse.Dialogue;
	Response.ParsedResponse = ParsedResponse;
	Component->HandleRequestCompletedForTest(Response);

	TestEqual(TEXT("US-1 non-streaming completion should broadcast one final response."), ResponseCount, 1);
	TestEqual(TEXT("US-1 non-streaming completion should not emit partial responses."), PartialCount, 0);
	TestEqual(TEXT("US-1 non-streaming completion should not invent action intents."), ParsedResponse.Actions.Num(), 0);
	TestFalse(TEXT("US-1 non-streaming component should have no latest executable action intent."), [&]()
	{
		FNpcAction Action;
		return Component->TryGetLatestActionIntent(Action);
	}());
	TestEqual(TEXT("US-1 non-streaming completion should transition to Speaking."), Component->GetDialogueState(), ENpcDialogueState::Speaking);
	TestFalse(TEXT("US-1 non-streaming completion should clear in-flight state."), Component->IsRequestInFlight());
	TestEqual(TEXT("No active delay masking should mean no end delegate is fabricated."), DelayMaskingEndCount, 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
