#include "Misc/AutomationTest.h"

#include "Components/AINpcComponent.h"
#include "Events/NpcEventPayloadBlueprintLibrary.h"
#include "Events/NpcEventPayloadTypes.h"
#include "Events/NpcEventSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "LLM/LLMResponseParser.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2LLMDegradationPayloadHelperRoundTripTest,
	"AINpc.US2.Events.LLMDegradationPayloadHelperRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2LLMDegradationPayloadHelperRoundTripTest::RunTest(const FString& Parameters)
{
	AActor* NpcActor = NewObject<AActor>();
	TestNotNull(TEXT("NPC actor should be available for degradation payload roundtrip."), NpcActor);
	if (!NpcActor)
	{
		return false;
	}

	const FGameplayTag EventTag = FGameplayTag::RequestGameplayTag(TEXT("AINpc.Tests.Degradation"), false);
	const FGameplayTag RoutingTag = FGameplayTag::RequestGameplayTag(TEXT("AINpc.Tests.Route.Degradation"), false);
	FGameplayTagContainer RoutingTags;
	if (RoutingTag.IsValid())
	{
		RoutingTags.AddTag(RoutingTag);
	}

	const FNpcEventMessage Message = UNpcEventPayloadBlueprintLibrary::MakeLLMDegradationEventMessage(
		EventTag,
		RoutingTags,
		NpcActor,
		TEXT("primary provider timed out"),
		2,
		true);

	TestEqual(TEXT("Helper should preserve event tag."), Message.EventTag, EventTag);
	if (RoutingTag.IsValid())
	{
		TestTrue(TEXT("Helper should preserve routing tags."), Message.RoutingTags.HasTagExact(RoutingTag));
	}
	TestEqual(TEXT("Helper should expose degradation payload type."), UNpcEventPayloadBlueprintLibrary::GetPayloadStructType(Message), FNpcLLMDegradationEventPayload::StaticStruct());

	FNpcLLMDegradationEventPayload Payload;
	TestTrue(TEXT("Extractor should read degradation payload from message."), UNpcEventPayloadBlueprintLibrary::TryGetLLMDegradationPayloadFromMessage(Message, Payload));
	TestEqual(TEXT("Extractor should preserve NPC actor."), static_cast<AActor*>(Payload.NpcActor), NpcActor);
	TestEqual(TEXT("Extractor should preserve reason."), Payload.Reason, FString(TEXT("primary provider timed out")));
	TestEqual(TEXT("Extractor should preserve retry count."), Payload.RetryCount, 2);
	TestTrue(TEXT("Extractor should preserve template flag."), Payload.bUsedTemplate);

	FNpcAttackEventPayload WrongPayload;
	TestFalse(TEXT("Attack extractor must not accept degradation payloads."), UNpcEventPayloadBlueprintLibrary::TryGetAttackPayloadFromMessage(Message, WrongPayload));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2PlainTextFallbackHasNoExecutableActionsTest,
	"AINpc.US2.Parser.PlainTextFallbackHasNoExecutableActions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2PlainTextFallbackHasNoExecutableActionsTest::RunTest(const FString& Parameters)
{
	const FString ResponseBody = TEXT("{\"choices\":[{\"message\":{\"content\":\"  I can answer without using a gameplay action.  \"}}]}");

	FParsedLLMResponse ParsedResponse;
	FString ErrorMessage;
	const bool bParsed = FLLMResponseParser::ParseOpenAIChatCompletion(ResponseBody, ParsedResponse, ErrorMessage);

	TestTrue(TEXT("Plain text fallback should still parse successfully."), bParsed);
	TestTrue(TEXT("Plain text fallback should not report parser errors."), ErrorMessage.IsEmpty());
	TestEqual(TEXT("Plain text fallback should preserve trimmed dialogue."), ParsedResponse.Dialogue, FString(TEXT("I can answer without using a gameplay action.")));
	TestEqual(TEXT("Plain text fallback should record PlainText tier."), ParsedResponse.ParseTier, ELLMResponseParseTier::PlainText);
	TestFalse(TEXT("Plain text fallback should not claim JSON parsing."), ParsedResponse.bParsedAsJson);
	TestEqual(TEXT("Plain text fallback must not create executable action intents."), ParsedResponse.Actions.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2EndPlayCleansRequestAndDelayMaskingTest,
	"AINpc.US2.Component.EndPlayCleansRequestAndDelayMasking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2EndPlayCleansRequestAndDelayMaskingTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("AINpcUs2EndPlayCleanupWorld"));
	TestNotNull(TEXT("Transient world should be available for EndPlay cleanup test."), World);
	if (!World)
	{
		return false;
	}

	AActor* Owner = World->SpawnActor<AActor>();
	TestNotNull(TEXT("Owner actor should be available for EndPlay cleanup test."), Owner);
	if (!Owner)
	{
		World->DestroyWorld(false);
		return false;
	}

	UAINpcComponent* Component = NewObject<UAINpcComponent>(Owner);
	TestNotNull(TEXT("Component should be available for EndPlay cleanup test."), Component);
	if (!Component)
	{
		World->DestroyWorld(false);
		return false;
	}

	Component->RegisterComponent();
	Component->BeginPlay();
	Component->SetDialogueTestState(true, true, FGuid::NewGuid(), 3, ENpcDialogueState::WaitingForLLM);
	Component->HandleDelayMaskingThresholdReachedForTest();

	TestTrue(TEXT("Test setup should mark request in flight."), Component->IsRequestInFlight());
	TestTrue(TEXT("Test setup should mark dialogue session active."), Component->IsDialogueActive());
	TestTrue(TEXT("Test setup should activate delay masking."), Component->IsDelayMaskingActive());

	int32 EndCount = 0;
	Component->OnDialogueSessionEndedNative().AddLambda([&EndCount]() { ++EndCount; });

	Component->EndPlay(EEndPlayReason::Destroyed);

	TestFalse(TEXT("EndPlay should clear dialogue session state."), Component->IsDialogueActive());
	TestFalse(TEXT("EndPlay should clear request in-flight state."), Component->IsRequestInFlight());
	TestFalse(TEXT("EndPlay should end active delay masking."), Component->IsDelayMaskingActive());
	TestFalse(TEXT("EndPlay should invalidate active request id."), Component->GetActiveRequestIdForTest().IsValid());
	TestEqual(TEXT("EndPlay should reset retry attempt count."), Component->GetRetryAttemptCountForTest(), 0);
	TestEqual(TEXT("EndPlay should transition dialogue state to Idle."), Component->GetDialogueState(), ENpcDialogueState::Idle);
	TestEqual(TEXT("EndPlay should preserve EndDialogue session-ended semantics."), EndCount, 1);

	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
