#include "Misc/AutomationTest.h"

#include "Components/AINpcComponent.h"
#include "Components/AINpcSmartObjectPromptHandler.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "LLM/LLMProviderTypes.h"
#include "LLM/LLMResponseParser.h"
#include "SmartObjectBridge/AINpcSmartObjectRuntimeExecutor.h"
#include "SmartObjectBridge/SmartObjectBridgeContext.h"
#include "StateTree/Tasks/StateTreeTask_ExecuteSmartObject.h"
#include "Test/AINpcTestSmartObjectActor.h"

#if defined(WITH_SMARTOBJECTS) && WITH_SMARTOBJECTS
#include "SmartObjectDefinition.h"
#include "SmartObjectRequestTypes.h"
#include "SmartObjectRuntime.h"
#include "SmartObjectSubsystem.h"
#include "SmartObjectTypes.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
void DestroyTestWorld(UWorld*& World)
{
	if (World)
	{
		if (GEngine)
		{
			GEngine->DestroyWorldContext(World);
		}

		World->DestroyWorld(false);
		World = nullptr;
	}
}

#if defined(WITH_SMARTOBJECTS) && WITH_SMARTOBJECTS
USmartObjectDefinition* CreateSmartObjectDefinition(UObject* Outer)
{
	USmartObjectDefinition* Definition = NewObject<USmartObjectDefinition>(Outer);
	FSmartObjectSlotDefinition& Slot = Definition->DebugAddSlot();
	Slot.Offset = FVector3f::ZeroVector;
	Slot.BehaviorDefinitions.Add(NewObject<UAINpcTestSmartObjectBehaviorDefinition>(Definition));
	Definition->Validate();
	return Definition;
}

void InitializeSmartObjectRuntime(UWorld& World)
{
	if (GEngine)
	{
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(World.WorldType);
		WorldContext.SetCurrentWorld(&World);
	}

	const FURL URL;
	World.InitializeActorsForPlay(URL);
	World.BeginPlay();
}

FSmartObjectRequestResult ResolveSingleSlotResult(
	USmartObjectSubsystem& SmartObjectSubsystem,
	AActor& UserActor,
	const FVector& QueryCenter,
	const float QueryRadius,
	const FSmartObjectHandle ExpectedSmartObjectHandle)
{
	const FSmartObjectRequest Request(
		FBox::BuildAABB(QueryCenter, FVector(QueryRadius)),
		FSmartObjectRequestFilter());

	TArray<FSmartObjectRequestResult> Results;
	SmartObjectSubsystem.FindSmartObjects(
		Request,
		Results,
		FConstStructView::Make(FSmartObjectActorUserData(&UserActor)));

	for (const FSmartObjectRequestResult& Result : Results)
	{
		if (Result.SmartObjectHandle == ExpectedSmartObjectHandle && Result.SlotHandle.IsValid())
		{
			return Result;
		}
	}

	return FSmartObjectRequestResult();
}
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2SmartObjectPromptUsesRealAvailableTargetsTest,
	"AINpc.US2.SmartObject.PromptUsesRealAvailableTargets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2SmartObjectPromptUsesRealAvailableTargetsTest::RunTest(const FString& Parameters)
{
#if defined(WITH_SMARTOBJECTS) && WITH_SMARTOBJECTS
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("AINpcUs2SmartObjectPromptRuntimeWorld"));
	TestNotNull(TEXT("World should be available for real SmartObject prompt target query."), World);
	if (!World)
	{
		return false;
	}

	USmartObjectSubsystem* SmartObjectSubsystem = USmartObjectSubsystem::GetCurrent(World);
	TestNotNull(TEXT("SmartObject subsystem should be available from the real world."), SmartObjectSubsystem);
	if (!SmartObjectSubsystem)
	{
		DestroyTestWorld(World);
		return false;
	}

	InitializeSmartObjectRuntime(*World);

	USmartObjectDefinition* Definition = CreateSmartObjectDefinition(World);
	TestNotNull(TEXT("Runtime SmartObject definition should be valid."), Definition);
	if (!Definition)
	{
		DestroyTestWorld(World);
		return false;
	}

	AActor* UserActor = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	TestNotNull(TEXT("User actor should be available for production prompt target query."), UserActor);
	if (!UserActor)
	{
		DestroyTestWorld(World);
		return false;
	}

	UAINpcComponent* Component = NewObject<UAINpcComponent>(UserActor);
	TestNotNull(TEXT("NPC component should be owned by the real query actor."), Component);
	if (!Component)
	{
		DestroyTestWorld(World);
		return false;
	}

	const FSmartObjectHandle AvailableHandle = SmartObjectSubsystem->CreateSmartObject(
		*Definition,
		FTransform(FRotator::ZeroRotator, FVector(100.0f, 0.0f, 0.0f)),
		FConstStructView::Make(FSmartObjectActorUserData(UserActor)));
	const FSmartObjectHandle ClaimedHandle = SmartObjectSubsystem->CreateSmartObject(
		*Definition,
		FTransform(FRotator::ZeroRotator, FVector(200.0f, 0.0f, 0.0f)),
		FConstStructView::Make(FSmartObjectActorUserData(UserActor)));
	const FSmartObjectHandle DisabledHandle = SmartObjectSubsystem->CreateSmartObject(
		*Definition,
		FTransform(FRotator::ZeroRotator, FVector(300.0f, 0.0f, 0.0f)),
		FConstStructView::Make(FSmartObjectActorUserData(UserActor)));
	const FSmartObjectHandle OutOfRangeHandle = SmartObjectSubsystem->CreateSmartObject(
		*Definition,
		FTransform(FRotator::ZeroRotator, FVector(2500.0f, 0.0f, 0.0f)),
		FConstStructView::Make(FSmartObjectActorUserData(UserActor)));

	TestTrue(TEXT("Available SmartObject should be created through the real subsystem."), AvailableHandle.IsValid());
	TestTrue(TEXT("Claimed SmartObject should be created through the real subsystem."), ClaimedHandle.IsValid());
	TestTrue(TEXT("Disabled SmartObject should be created through the real subsystem."), DisabledHandle.IsValid());
	TestTrue(TEXT("Out-of-range SmartObject should be created through the real subsystem."), OutOfRangeHandle.IsValid());
	if (!AvailableHandle.IsValid() || !ClaimedHandle.IsValid() || !DisabledHandle.IsValid() || !OutOfRangeHandle.IsValid())
	{
		DestroyTestWorld(World);
		return false;
	}

	const FSmartObjectRequestResult AvailableResult = ResolveSingleSlotResult(*SmartObjectSubsystem, *UserActor, FVector(100.0f, 0.0f, 0.0f), 50.0f, AvailableHandle);
	const FSmartObjectRequestResult ClaimedResult = ResolveSingleSlotResult(*SmartObjectSubsystem, *UserActor, FVector(200.0f, 0.0f, 0.0f), 50.0f, ClaimedHandle);
	const FSmartObjectRequestResult DisabledResult = ResolveSingleSlotResult(*SmartObjectSubsystem, *UserActor, FVector(300.0f, 0.0f, 0.0f), 50.0f, DisabledHandle);
	const FSmartObjectRequestResult OutOfRangeResult = ResolveSingleSlotResult(*SmartObjectSubsystem, *UserActor, FVector(2500.0f, 0.0f, 0.0f), 50.0f, OutOfRangeHandle);
	const FString AvailableTarget = LexToString(AvailableResult.SlotHandle);
	const FString ClaimedTarget = LexToString(ClaimedResult.SlotHandle);
	const FString DisabledTarget = LexToString(DisabledResult.SlotHandle);
	const FString OutOfRangeTarget = LexToString(OutOfRangeResult.SlotHandle);
	if (!AvailableResult.IsValid() || !ClaimedResult.IsValid() || !DisabledResult.IsValid() || !OutOfRangeResult.IsValid())
	{
		AddError(TEXT("Setup failed to resolve concrete slot ids from real FindSmartObjects before omission checks."));
		DestroyTestWorld(World);
		return false;
	}

	const FSmartObjectClaimHandle ClaimedSlotClaim = SmartObjectSubsystem->MarkSlotAsClaimed(
		ClaimedResult.SlotHandle,
		ESmartObjectClaimPriority::Normal,
		FConstStructView::Make(FSmartObjectActorUserData(UserActor)));
	TestTrue(TEXT("Setup must claim the unavailable SmartObject slot through the real subsystem."), ClaimedSlotClaim.IsValid());
	TestTrue(TEXT("Setup must disable the unavailable SmartObject through the real subsystem."), SmartObjectSubsystem->SetEnabled(DisabledHandle, false));
	if (!ClaimedSlotClaim.IsValid())
	{
		DestroyTestWorld(World);
		return false;
	}

	const TArray<FString> Targets = Component->GetAvailableSmartObjectTargetsForPromptForTest();
	TestTrue(TEXT("Production prompt target discovery should include the real available nearby slot."), Targets.Contains(AvailableTarget));
	TestFalse(TEXT("Production prompt target discovery should omit already-claimed slots."), Targets.Contains(ClaimedTarget));
	TestFalse(TEXT("Production prompt target discovery should omit disabled SmartObjects."), Targets.Contains(DisabledTarget));
	TestFalse(TEXT("Production prompt target discovery should omit out-of-range SmartObjects."), Targets.Contains(OutOfRangeTarget));

	const FString Prompt = FAINpcSmartObjectPromptHandler::BuildSystemPrompt(*Component);
	TestTrue(TEXT("Production prompt text should include the real available target."), Prompt.Contains(AvailableTarget));
	TestFalse(TEXT("Production prompt text should omit the already-claimed target."), Prompt.Contains(ClaimedTarget));
	TestFalse(TEXT("Production prompt text should omit the disabled target."), Prompt.Contains(DisabledTarget));
	TestFalse(TEXT("Production prompt text should omit the out-of-range target."), Prompt.Contains(OutOfRangeTarget));

	SmartObjectSubsystem->MarkSlotAsFree(ClaimedSlotClaim);
	DestroyTestWorld(World);
	return true;
#else
	AddInfo(TEXT("SmartObjects are disabled in this build. Real prompt target discovery is covered by enabled-build automation."));
	return true;
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2SmartObjectPromptTargetsAreDeterministicTest,
	"AINpc.US2.SmartObject.PromptTargetsAreDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2SmartObjectPromptTargetsAreDeterministicTest::RunTest(const FString& Parameters)
{
	UAINpcComponent* Component = NewObject<UAINpcComponent>();
	TestNotNull(TEXT("Component should be available for prompt target test."), Component);
	if (!Component)
	{
		return false;
	}

	UAINpcComponent::SetSmartObjectTargetsForPromptForTest({TEXT("Slot.B"), TEXT("Slot.A"), TEXT("Slot.B")});
	const TArray<FString> Targets = Component->GetAvailableSmartObjectTargetsForPromptForTest();
	UAINpcComponent::ClearSmartObjectTargetsForPromptForTest();

	TestEqual(TEXT("Prompt targets should sort deterministically."), Targets.Num() > 0 ? Targets[0] : FString(), FString(TEXT("Slot.A")));
	TestEqual(TEXT("Prompt targets should omit duplicate entries."), Targets.Num(), 2);
	TestFalse(TEXT("Prompt targets should omit blank entries."), Targets.Contains(FString()));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2SmartObjectInvalidTargetRejectedBeforeClaimTest,
	"AINpc.US2.SmartObject.InvalidTargetRejectedBeforeClaim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2SmartObjectInvalidTargetRejectedBeforeClaimTest::RunTest(const FString& Parameters)
{
	FNpcAction ActionIntent;
	ActionIntent.ActionType = TEXT("use_smart_object");
	ActionIntent.Target = TEXT("Slot.NotLegal");

	FString RequestedTarget;
	FString FailureReason;
	const bool bValid = FAINpcSmartObjectRuntimeExecutor::ValidateSmartObjectActionIntent(
		ActionIntent,
		{TEXT("Slot.Legal")},
		RequestedTarget,
		FailureReason);

	TestFalse(TEXT("Invalid SmartObject targets must be rejected before bridge claim/use."), bValid);
	TestTrue(TEXT("Invalid rejection should be diagnosable."), FailureReason.Contains(TEXT("not in the legal runtime whitelist")));
	TestTrue(TEXT("Rejected validation must not report a requested target."), RequestedTarget.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2SmartObjectValidTargetAcceptedTest,
	"AINpc.US2.SmartObject.ValidTargetAccepted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2SmartObjectValidTargetAcceptedTest::RunTest(const FString& Parameters)
{
	FNpcAction ActionIntent;
	ActionIntent.ActionType = TEXT("use_smart_object");
	ActionIntent.Target = TEXT("  Slot.Legal  ");

	FString RequestedTarget;
	FString FailureReason;
	const bool bValid = FAINpcSmartObjectRuntimeExecutor::ValidateSmartObjectActionIntent(
		ActionIntent,
		{TEXT("Slot.Legal")},
		RequestedTarget,
		FailureReason);

	TestTrue(TEXT("Legal SmartObject target should pass inline whitelist validation."), bValid);
	TestEqual(TEXT("Validation should trim requested target before execution."), RequestedTarget, FString(TEXT("Slot.Legal")));
	TestTrue(TEXT("Accepted validation should clear failure reason."), FailureReason.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcUs2SmartObjectClaimCleanupForWorldTest,
	"AINpc.US2.SmartObject.ClaimCleanupForWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcUs2SmartObjectClaimCleanupForWorldTest::RunTest(const FString& Parameters)
{
	UWorld* WorldA = UWorld::CreateWorld(EWorldType::Game, false, TEXT("AINpcUs2SmartObjectCleanupA"));
	UWorld* WorldB = UWorld::CreateWorld(EWorldType::Game, false, TEXT("AINpcUs2SmartObjectCleanupB"));
	TestNotNull(TEXT("World A should be available for cleanup test."), WorldA);
	TestNotNull(TEXT("World B should be available for cleanup test."), WorldB);
	if (!WorldA || !WorldB)
	{
		if (WorldA)
		{
			WorldA->DestroyWorld(false);
		}
		if (WorldB)
		{
			WorldB->DestroyWorld(false);
		}
		return false;
	}

	UGameInstance* GameInstance = NewObject<UGameInstance>();
	USmartObjectBridgeContext* BridgeContext = NewObject<USmartObjectBridgeContext>(GameInstance);
	TestNotNull(TEXT("Bridge context should be available for cleanup test."), BridgeContext);
	if (!BridgeContext)
	{
		WorldA->DestroyWorld(false);
		WorldB->DestroyWorld(false);
		return false;
	}

	BridgeContext->AddTrackedClaimForTest(WorldA);
	BridgeContext->AddTrackedClaimForTest(WorldB);
	TestEqual(TEXT("Setup should track two claims."), BridgeContext->GetTrackedClaimCountForTest(), 2);

	BridgeContext->TriggerWorldCleanupForTest(WorldA);
	TestEqual(TEXT("World cleanup should remove only claims for the cleaned world."), BridgeContext->GetTrackedClaimCountForTest(), 1);
	TestEqual(TEXT("World B claim should remain after World A cleanup."), BridgeContext->GetTrackedClaimCountForWorldForTest(WorldB), 1);

	BridgeContext->TriggerWorldCleanupForTest(nullptr);
	TestEqual(TEXT("Global cleanup should remove all tracked claims."), BridgeContext->GetTrackedClaimCountForTest(), 0);

	WorldA->DestroyWorld(false);
	WorldB->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
