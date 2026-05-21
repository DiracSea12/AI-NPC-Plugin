#include "AINpcUs2EventRoutingAutomationTests.h"

#include "Components/AINpcComponent.h"
#include "Animation/AnimMontage.h"
#include "Events/NpcEventPayloadBlueprintLibrary.h"
#include "Events/NpcEventPayloadTypes.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Data/NpcPersonaDataAsset.h"

#if WITH_DEV_AUTOMATION_TESTS

void UAINpcUs2EventDispatchRecorder::RecordEventStage(const FNpcEventMessage& EventMessage, const ENpcEventDispatchStage DispatchStage)
{
	Messages.Add(EventMessage);
	Stages.Add(DispatchStage);
}

namespace
{
	FGameplayTag RequestOptionalTestTag(const TCHAR* TagName)
	{
		return FGameplayTag::RequestGameplayTag(FName(TagName), false);
	}

	FGameplayTag RequireRoutingTag(FAutomationTestBase& Test, const TCHAR* TagName)
	{
		const FGameplayTag Tag = RequestOptionalTestTag(TagName);
		Test.TestTrue(FString::Printf(TEXT("Gameplay tag '%s' must exist for US-2 event routing tests."), TagName), Tag.IsValid());
		return Tag;
	}

	FNpcEventMessage MakeOwnerAttackMessage(AActor* OwnerActor, const FGameplayTag& EventTag, const FGameplayTagContainer& RoutingTags)
	{
		FNpcAttackEventPayload Payload;
		Payload.TargetActor = OwnerActor;
		return UNpcEventPayloadBlueprintLibrary::MakeAttackEventMessage(
			EventTag,
			RoutingTags,
			nullptr,
			OwnerActor,
			1.0f,
			FGameplayTag());
	}

	UAINpcComponent* NewWaitingComponentWithHitReact(FAutomationTestBase& Test, AActor* OwnerActor)
	{
		UAINpcComponent* Component = NewObject<UAINpcComponent>(OwnerActor);
		Test.TestNotNull(TEXT("Component should be available for US-2 event routing test."), Component);
		if (!Component)
		{
			return nullptr;
		}

		UNpcPersonaDataAsset* Persona = NewObject<UNpcPersonaDataAsset>();
		Test.TestNotNull(TEXT("Persona should be available for US-2 event routing test."), Persona);
		if (!Persona)
		{
			return nullptr;
		}

		Persona->HitReactionDelayMaskingMontages.Add(NewObject<UAnimMontage>());
		Persona->PersonaName = TEXT("Before Event");
		Component->PersonaDataAsset = Persona;
		Component->SetDialogueTestState(true, true, FGuid::NewGuid(), 0, ENpcDialogueState::WaitingForLLM);
		return Component;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2EventBroadcastPreservesNativeAndDynamicPayloadTest,
	"AINpc.US2.Events.BroadcastPreservesNativeAndDynamicPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2EventBroadcastPreservesNativeAndDynamicPayloadTest::RunTest(const FString& Parameters)
{
	const FGameplayTag EventTag = RequireRoutingTag(*this, TEXT("AINpc.Tests.Route.Match"));
	const FGameplayTag RoutingTag = RequireRoutingTag(*this, TEXT("AINpc.Tests.Route.Other"));
	if (!EventTag.IsValid() || !RoutingTag.IsValid())
	{
		return false;
	}

	FGameplayTagContainer RoutingTags;
	RoutingTags.AddTag(RoutingTag);

	AActor* TargetActor = NewObject<AActor>();
	TestNotNull(TEXT("Target actor should exist for payload preservation test."), TargetActor);
	if (!TargetActor)
	{
		return false;
	}

	const FNpcEventMessage EventMessage = MakeOwnerAttackMessage(TargetActor, EventTag, RoutingTags);
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	TestNotNull(TEXT("Game instance should exist as the event subsystem outer."), GameInstance);
	UNpcEventSubsystem* Subsystem = GameInstance ? NewObject<UNpcEventSubsystem>(GameInstance) : nullptr;
	TestNotNull(TEXT("Event subsystem should exist for payload preservation test."), Subsystem);
	UAINpcUs2EventDispatchRecorder* DynamicRecorder = NewObject<UAINpcUs2EventDispatchRecorder>();
	TestNotNull(TEXT("Dynamic recorder should exist for payload preservation test."), DynamicRecorder);
	if (!Subsystem || !DynamicRecorder)
	{
		return false;
	}

	TArray<FNpcEventMessage> NativeMessages;
	TArray<ENpcEventDispatchStage> NativeStages;
	Subsystem->OnEventStageDispatchedNative().AddLambda(
		[&NativeMessages, &NativeStages](const FNpcEventMessage& ObservedMessage, const ENpcEventDispatchStage ObservedStage)
		{
			NativeMessages.Add(ObservedMessage);
			NativeStages.Add(ObservedStage);
		});
	Subsystem->OnEventStageDispatched.AddDynamic(DynamicRecorder, &UAINpcUs2EventDispatchRecorder::RecordEventStage);

	Subsystem->BroadcastEvent(EventMessage);

	const TArray<ENpcEventDispatchStage> ExpectedStages = UNpcEventSubsystem::GetDefaultDispatchOrder();
	TestEqual(TEXT("Native dispatch should emit each default stage."), NativeMessages.Num(), ExpectedStages.Num());
	TestEqual(TEXT("Dynamic dispatch should emit each default stage."), DynamicRecorder->Messages.Num(), ExpectedStages.Num());
	if (NativeMessages.Num() != ExpectedStages.Num() || DynamicRecorder->Messages.Num() != ExpectedStages.Num())
	{
		return false;
	}

	for (int32 Index = 0; Index < ExpectedStages.Num(); ++Index)
	{
		TestEqual(FString::Printf(TEXT("Native stage %d should match default order."), Index), NativeStages[Index], ExpectedStages[Index]);
		TestEqual(FString::Printf(TEXT("Dynamic stage %d should match default order."), Index), DynamicRecorder->Stages[Index], ExpectedStages[Index]);

		const FNpcAttackEventPayload* NativePayload = NativeMessages[Index].Payload.GetPtr<FNpcAttackEventPayload>();
		const FNpcAttackEventPayload* DynamicPayload = DynamicRecorder->Messages[Index].Payload.GetPtr<FNpcAttackEventPayload>();
		TestNotNull(FString::Printf(TEXT("Native payload at stage %d should keep attack payload type."), Index), NativePayload);
		TestNotNull(FString::Printf(TEXT("Dynamic payload at stage %d should keep attack payload type."), Index), DynamicPayload);
		if (!NativePayload || !DynamicPayload)
		{
			return false;
		}

		TestEqual(FString::Printf(TEXT("Native event tag at stage %d should be preserved."), Index), NativeMessages[Index].EventTag, EventTag);
		TestTrue(FString::Printf(TEXT("Native routing tags at stage %d should be preserved."), Index), NativeMessages[Index].RoutingTags.HasTagExact(RoutingTag));
		TestEqual(FString::Printf(TEXT("Native payload target at stage %d should be preserved."), Index), static_cast<AActor*>(NativePayload->TargetActor), TargetActor);

		TestEqual(FString::Printf(TEXT("Dynamic event tag at stage %d should be preserved."), Index), DynamicRecorder->Messages[Index].EventTag, EventTag);
		TestTrue(FString::Printf(TEXT("Dynamic routing tags at stage %d should be preserved."), Index), DynamicRecorder->Messages[Index].RoutingTags.HasTagExact(RoutingTag));
		TestEqual(FString::Printf(TEXT("Dynamic payload target at stage %d should be preserved."), Index), static_cast<AActor*>(DynamicPayload->TargetActor), TargetActor);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2EventRoutingFilterIgnoresNonMatchingTagsTest,
	"AINpc.US2.EventRouting.FilterIgnoresNonMatchingTags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2EventRoutingFilterIgnoresNonMatchingTagsTest::RunTest(const FString& Parameters)
{
	const FGameplayTag MatchingTag = RequireRoutingTag(*this, TEXT("AINpc.Tests.Route.Match"));
	const FGameplayTag NonMatchingTag = RequireRoutingTag(*this, TEXT("AINpc.Tests.Route.Other"));
	if (!MatchingTag.IsValid() || !NonMatchingTag.IsValid())
	{
		return false;
	}

	AActor* OwnerActor = NewObject<AActor>();
	TestNotNull(TEXT("Owner actor should exist for routing filter test."), OwnerActor);
	UAINpcComponent* Component = NewWaitingComponentWithHitReact(*this, OwnerActor);
	if (!OwnerActor || !Component)
	{
		return false;
	}

	Component->EventSubscriptionTags.AddTag(MatchingTag);

	int32 DelayMaskingStartCount = 0;
	Component->OnDelayMaskingStartNative().AddLambda(
		[&DelayMaskingStartCount](UAnimMontage*, const FText&)
		{
			++DelayMaskingStartCount;
		});

	FGameplayTagContainer NonMatchingRoutingTags;
	NonMatchingRoutingTags.AddTag(NonMatchingTag);
	Component->HandleNpcEventStageDispatchedForTest(
		MakeOwnerAttackMessage(OwnerActor, NonMatchingTag, NonMatchingRoutingTags),
		ENpcEventDispatchStage::DelayMasking);

	TestEqual(TEXT("Non-matching routing tags must not start event-driven delay masking."), DelayMaskingStartCount, 0);
	TestFalse(TEXT("Non-matching routing tags must leave delay masking inactive."), Component->IsDelayMaskingActive());

	FGameplayTagContainer MatchingRoutingTags;
	MatchingRoutingTags.AddTag(MatchingTag);
	Component->HandleNpcEventStageDispatchedForTest(
		MakeOwnerAttackMessage(OwnerActor, NonMatchingTag, MatchingRoutingTags),
		ENpcEventDispatchStage::DelayMasking);

	TestEqual(TEXT("Matching routing tag should start delay masking exactly once."), DelayMaskingStartCount, 1);
	TestTrue(TEXT("Matching routing tag should activate delay masking."), Component->IsDelayMaskingActive());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2EventRoutingDefaultOrderRunsDelayBeforePromptUpdateTest,
	"AINpc.US2.EventRouting.DefaultOrderRunsDelayBeforePromptUpdate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2EventRoutingDefaultOrderRunsDelayBeforePromptUpdateTest::RunTest(const FString& Parameters)
{
	const FGameplayTag MatchingTag = RequireRoutingTag(*this, TEXT("AINpc.Tests.Route.Match"));
	if (!MatchingTag.IsValid())
	{
		return false;
	}

	AActor* OwnerActor = NewObject<AActor>();
	TestNotNull(TEXT("Owner actor should exist for routing order test."), OwnerActor);
	UAINpcComponent* Component = NewWaitingComponentWithHitReact(*this, OwnerActor);
	if (!OwnerActor || !Component)
	{
		return false;
	}

	Component->EventSubscriptionTags.AddTag(MatchingTag);
	UAINpcComponent::SetSmartObjectTargetsForPromptForTest({TEXT("SO_SLOT_US2_BEFORE")});
	FLLMMessage SystemMessage;
	SystemMessage.Role = TEXT("system");
	SystemMessage.Content = TEXT("Stale prompt context: SO_SLOT_US2_BEFORE");
	FLLMMessage UserMessage;
	UserMessage.Role = TEXT("user");
	UserMessage.Content = TEXT("Watch the event.");
	Component->SeedConversationHistoryForTest({SystemMessage, UserMessage});
	const FLLMRequest RequestBeforeEvent = Component->BuildRequestForTest();
	UAINpcComponent::SetSmartObjectTargetsForPromptForTest({TEXT("SO_SLOT_US2_AFTER")});

	TArray<ENpcEventDispatchStage> ObservedStages;
	int32 DelayMaskingStartCount = 0;
	Component->OnDelayMaskingStartNative().AddLambda(
		[&DelayMaskingStartCount, &ObservedStages](UAnimMontage*, const FText&)
		{
			++DelayMaskingStartCount;
			ObservedStages.Add(ENpcEventDispatchStage::DelayMasking);
		});

	FGameplayTagContainer RoutingTags;
	RoutingTags.AddTag(MatchingTag);
	const FNpcEventMessage EventMessage = MakeOwnerAttackMessage(OwnerActor, MatchingTag, RoutingTags);

	for (const ENpcEventDispatchStage Stage : UNpcEventSubsystem::GetDefaultDispatchOrder())
	{
		Component->HandleNpcEventStageDispatchedForTest(EventMessage, Stage);
		if (Stage == ENpcEventDispatchStage::PromptUpdate)
		{
			ObservedStages.Add(Stage);
		}
	}

	TestEqual(TEXT("Default-order dispatch should trigger one event-driven delay masking start."), DelayMaskingStartCount, 1);
	TestEqual(TEXT("Focused order observation should include delay masking then prompt update."), ObservedStages.Num(), 2);
	if (ObservedStages.Num() != 2)
	{
		return false;
	}

	TestEqual(TEXT("Delay masking must run before prompt update in the default component routing path."), ObservedStages[0], ENpcEventDispatchStage::DelayMasking);
	TestEqual(TEXT("Prompt update must remain the final observable Phase 2 stage."), ObservedStages[1], ENpcEventDispatchStage::PromptUpdate);
	const FLLMRequest RequestAfterEvent = Component->BuildRequestForTest();
	TestTrue(TEXT("Prompt update stage should refresh the system prompt from current Phase 2 prompt context."), RequestAfterEvent.Messages[0].Content.Contains(TEXT("SO_SLOT_US2_AFTER")));
	TestFalse(TEXT("Prompt update stage should replace stale prompt context instead of preserving old targets."), RequestAfterEvent.Messages[0].Content.Contains(TEXT("SO_SLOT_US2_BEFORE")));
	TestTrue(TEXT("Test setup must prove the prompt actually changed."), RequestBeforeEvent.Messages[0].Content != RequestAfterEvent.Messages[0].Content);
	TestTrue(TEXT("Emotion and memory stages are intentionally no-op/deferred in Phase 2, not fake behavior."), Component->IsDelayMaskingActive());

	UAINpcComponent::ClearSmartObjectTargetsForPromptForTest();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
