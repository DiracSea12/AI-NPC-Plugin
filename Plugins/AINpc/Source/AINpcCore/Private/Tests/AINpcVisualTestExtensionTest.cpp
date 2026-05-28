#include "Test/AINpcVisualTestExtension.h"

#include "Test/AINpcDataDrivenVisualScenarioTest.h"
#include "Test/AINpcTestSmartObjectActor.h"
#include "Test/AINpcVisualTestExtensionInternal.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

namespace
{
	struct FLifecycleCountingAdapter : IAINpcVisualActionAdapter
	{
		explicit FLifecycleCountingAdapter(int32* InDestroyCount)
			: DestroyCount(InDestroyCount)
		{
		}

		~FLifecycleCountingAdapter() override
		{
			if (DestroyCount != nullptr)
			{
				++(*DestroyCount);
			}
		}

		int32* DestroyCount = nullptr;

		FAINpcVisualActionExecuteResult ExecuteAction(const FAINpcVisualActionExecuteRequest&) override
		{
			FAINpcVisualActionExecuteResult Result;
			Result.bAccepted = true;
			Result.bSucceeded = true;
			return Result;
		}
	};

	struct FLifecycleFixtureAdapter : IAINpcVisualFixtureResolverAdapter
	{
		FAINpcVisualFixtureResolveResult ResolveFixture(const FAINpcVisualFixtureResolveRequest&) override
		{
			FAINpcVisualFixtureResolveResult Result;
			Result.bSuccess = true;
			Result.TargetRef = TEXT("fixture.actor");
			return Result;
		}
	};

	struct FLifecycleObservationAdapter : IAINpcVisualObservationProviderAdapter
	{
		explicit FLifecycleObservationAdapter(int32* InDestroyCount)
			: DestroyCount(InDestroyCount)
		{
		}

		~FLifecycleObservationAdapter() override
		{
			if (DestroyCount != nullptr)
			{
				++(*DestroyCount);
			}
		}

		int32* DestroyCount = nullptr;

		FAINpcVisualObservationSampleResult SampleObservation(const FAINpcVisualObservationSampleRequest& Request) override
		{
			FAINpcVisualObservationSampleResult Result;
			Result.bSuccess = true;
			Result.Observation.Name = Request.ObservationName;
			Result.Observation.ValueType = EAINpcVisualObservationValueType::Boolean;
			Result.Observation.BoolValue = true;
			Result.Observation.SourceKind = TEXT("observation-provider");
			Result.Observation.SourceIdentity = TEXT("lifecycle.observation");
			Result.Observation.SourceObjectPath = TEXT("/Temp/LifecycleActor");
			Result.Observation.SourceClass = TEXT("AActor");
			Result.Observation.SamplingMethod = TEXT("state-read");
			Result.Observation.AdapterOrProviderId = Request.AdapterId.ToString();
			return Result;
		}
	};

	TWeakObjectPtr<AActor> GPhase29BFixtureActor;
	TWeakObjectPtr<AActor> GPhase29BLastTarget;
	TWeakObjectPtr<AActor> GPhase29BLastObservationSource;
	FString GPhase29BLastActionName;
	int32 GPhase29BActionCalls = 0;
	int32 GPhase29BObservationCalls = 0;
	bool GPhase29BDoorOpen = false;
	bool GPhase29BFixtureReturnsWrongActor = false;
	FString GPhase29BObservationSourceKind = TEXT("observation-provider");
	FString GPhase29BObservationSamplingMethod = TEXT("state-read");
	FString GPhase29BObservationProviderIdOverride;
	bool GPhase29BObservationOmitsSourceObjectPath = false;
	bool GPhase29BObservationOmitsSourceClass = false;
	bool GPhase29BFixtureFactoryFails = false;
	bool GPhase29BActionFactoryFails = false;
	bool GPhase29BObservationFactoryFails = false;

	struct FPhase29BFixtureAdapter : IAINpcVisualFixtureResolverAdapter
	{
		FAINpcVisualFixtureResolveResult ResolveFixture(const FAINpcVisualFixtureResolveRequest& Request) override
		{
			FAINpcVisualFixtureResolveResult Result;
			Result.bSuccess = GPhase29BFixtureActor.IsValid() && Request.TargetRef == TEXT("fixture.actor");
			Result.Actor = GPhase29BFixtureActor;
			Result.TargetRef = Request.TargetRef;
			if (GPhase29BFixtureReturnsWrongActor)
			{
				Result.Actor.Reset();
			}
			Result.Diagnostic = Result.bSuccess ? FString() : TEXT("phase29b fixture resolver rejected request");
			return Result;
		}
	};

	struct FPhase29BActionAdapter : IAINpcVisualActionAdapter
	{
		FAINpcVisualActionExecuteResult ExecuteAction(const FAINpcVisualActionExecuteRequest& Request) override
		{
			GPhase29BActionCalls++;
			GPhase29BLastActionName = Request.ActionName;
			GPhase29BLastTarget = Request.TargetActor;
			FAINpcVisualActionExecuteResult Result;
			Result.bAccepted = Request.ActionName == TEXT("Interact");
			Result.bSucceeded = Result.bAccepted;
			Result.FailureReason = Result.bAccepted ? FString() : TEXT("unsupported action");
			return Result;
		}
	};

	struct FPhase29BObservationAdapter : IAINpcVisualObservationProviderAdapter
	{
		FAINpcVisualObservationSampleResult SampleObservation(const FAINpcVisualObservationSampleRequest& Request) override
		{
			GPhase29BObservationCalls++;
			GPhase29BLastObservationSource = Request.SourceActor;
			FAINpcVisualObservationSampleResult Result;
			Result.bSuccess = GPhase29BDoorOpen && Request.ObservationName == TEXT("project.door.isOpen") && Request.SourceActor.IsValid();
			Result.FailureReason = Result.bSuccess ? FString() : TEXT("door observation unavailable");
			Result.Observation.Name = Request.ObservationName;
			Result.Observation.ValueType = EAINpcVisualObservationValueType::Boolean;
			Result.Observation.BoolValue = GPhase29BDoorOpen;
			Result.Observation.SourceKind = GPhase29BObservationSourceKind;
			Result.Observation.SourceIdentity = TEXT("phase29b.observation");
			Result.Observation.SourceObjectPath = !GPhase29BObservationOmitsSourceObjectPath && Request.SourceActor.IsValid() ? Request.SourceActor->GetPathName() : FString();
			Result.Observation.SourceClass = !GPhase29BObservationOmitsSourceClass && Request.SourceActor.IsValid() ? Request.SourceActor->GetClass()->GetName() : FString();
			Result.Observation.SamplingMethod = GPhase29BObservationSamplingMethod;
			Result.Observation.AdapterOrProviderId = GPhase29BObservationProviderIdOverride.IsEmpty() ? Request.AdapterId.ToString() : GPhase29BObservationProviderIdOverride;
			return Result;
		}
	};

	int32 GLifecycleFactoryCalls = 0;
	int32 GLifecycleDestroyCalls = 0;
	int32 GLifecycleReleaseCalls = 0;
	int32 GLifecycleGuardedUseCalls = 0;
	UWorld* GLifecycleCapturedWorld = nullptr;
	FString GLifecycleCapturedTestId;
	FString GLifecycleCapturedRunId;
	TArray<FString> GLifecycleCapturedStoryIds;
	TArray<FString> GLifecycleCapturedPhaseIds;
	bool GLifecycleCapturedDiagnosticSink = false;

	FAINpcVisualAdapterFactoryResult MakeLifecycleActionAdapter(const FAINpcVisualAdapterCreateContext& Context)
	{
		++GLifecycleFactoryCalls;
		GLifecycleCapturedWorld = Context.World;
		GLifecycleCapturedTestId = Context.TestId;
		GLifecycleCapturedRunId = Context.RunId;
		GLifecycleCapturedStoryIds = Context.StoryIds;
		GLifecycleCapturedPhaseIds = Context.PhaseIds;
		GLifecycleCapturedDiagnosticSink = Context.DiagnosticSink != nullptr;

		FAINpcVisualAdapterFactoryResult Result;
		Result.Adapter = MakeShared<FLifecycleCountingAdapter>(&GLifecycleDestroyCalls);
		return Result;
	}

	FAINpcVisualAdapterFactoryResult MakeLifecycleFixtureAdapter(const FAINpcVisualAdapterCreateContext&)
	{
		++GLifecycleFactoryCalls;
		FAINpcVisualAdapterFactoryResult Result;
		Result.Adapter = MakeShared<FLifecycleFixtureAdapter>();
		return Result;
	}

	FAINpcVisualAdapterFactoryResult MakeLifecycleObservationAdapter(const FAINpcVisualAdapterCreateContext&)
	{
		++GLifecycleFactoryCalls;
		FAINpcVisualAdapterFactoryResult Result;
		Result.Adapter = MakeShared<FLifecycleObservationAdapter>(&GLifecycleDestroyCalls);
		return Result;
	}

	FAINpcVisualAdapterFactoryResult MakePhase29BFixtureAdapter(const FAINpcVisualAdapterCreateContext&)
	{
		if (GPhase29BFixtureFactoryFails)
		{
			return FAINpcVisualAdapterFactoryResult::Failure(TEXT("stage=RuntimeStartup category=FixtureResolver adapter=phase29b.fixture reason=fixture factory failed"));
		}
		FAINpcVisualAdapterFactoryResult Result;
		Result.Adapter = MakeShared<FPhase29BFixtureAdapter>();
		return Result;
	}

	FAINpcVisualAdapterFactoryResult MakePhase29BActionAdapter(const FAINpcVisualAdapterCreateContext&)
	{
		if (GPhase29BActionFactoryFails)
		{
			return FAINpcVisualAdapterFactoryResult::Failure(TEXT("stage=RuntimeStartup category=ActionAdapter adapter=phase29b.action reason=action factory failed"));
		}
		FAINpcVisualAdapterFactoryResult Result;
		Result.Adapter = MakeShared<FPhase29BActionAdapter>();
		return Result;
	}

	FAINpcVisualAdapterFactoryResult MakePhase29BObservationAdapter(const FAINpcVisualAdapterCreateContext&)
	{
		if (GPhase29BObservationFactoryFails)
		{
			return FAINpcVisualAdapterFactoryResult::Failure(TEXT("stage=RuntimeStartup category=ObservationProvider adapter=phase29b.observation reason=observation factory failed"));
		}
		FAINpcVisualAdapterFactoryResult Result;
		Result.Adapter = MakeShared<FPhase29BObservationAdapter>();
		return Result;
	}

	FAINpcVisualAdapterDescriptor MakeActionDescriptor(const FName AdapterId, const FName Owner)
	{
		FAINpcVisualAdapterDescriptor Descriptor;
		Descriptor.Category = EAINpcVisualAdapterCategory::ActionAdapter;
		Descriptor.AdapterId = AdapterId;
		Descriptor.OwnerModuleName = Owner;
		Descriptor.Capabilities = { TEXT("projectAction.doorInteract") };
		Descriptor.CreateActionAdapter = &MakeLifecycleActionAdapter;
		return Descriptor;
	}

	FAINpcVisualObservationDeclaration MakeDoorObservationDeclaration()
	{
		FAINpcVisualObservationDeclaration Declaration;
		Declaration.ObservationName = TEXT("project.door.isOpen");
		Declaration.ValueType = EAINpcVisualObservationValueType::Boolean;
		Declaration.SourceKind = TEXT("observation-provider");
		Declaration.SamplingMethod = TEXT("state-read");
		Declaration.Capability = TEXT("observation.project.door.isOpen");
		Declaration.bRequiresSourceObjectPath = true;
		Declaration.bRequiresSourceClass = true;
		return Declaration;
	}

	bool ContainsAll(const FString& Diagnostic, const TArray<FString>& Terms)
	{
		for (const FString& Term : Terms)
		{
			if (!Diagnostic.Contains(Term))
			{
				return false;
			}
		}
		return true;
	}

	void ResetLifecycleCounters()
	{
		GLifecycleFactoryCalls = 0;
		GLifecycleDestroyCalls = 0;
		GLifecycleReleaseCalls = 0;
		GLifecycleGuardedUseCalls = 0;
		GLifecycleCapturedWorld = nullptr;
		GLifecycleCapturedTestId.Reset();
		GLifecycleCapturedRunId.Reset();
		GLifecycleCapturedStoryIds.Reset();
		GLifecycleCapturedPhaseIds.Reset();
		GLifecycleCapturedDiagnosticSink = false;
	}

	void ResetPhase29BState()
	{
		GPhase29BFixtureActor.Reset();
		GPhase29BLastTarget.Reset();
		GPhase29BLastObservationSource.Reset();
		GPhase29BLastActionName.Reset();
		GPhase29BActionCalls = 0;
		GPhase29BObservationCalls = 0;
		GPhase29BDoorOpen = false;
		GPhase29BFixtureReturnsWrongActor = false;
		GPhase29BObservationSourceKind = TEXT("observation-provider");
		GPhase29BObservationSamplingMethod = TEXT("state-read");
		GPhase29BObservationProviderIdOverride.Reset();
		GPhase29BObservationOmitsSourceObjectPath = false;
		GPhase29BObservationOmitsSourceClass = false;
		GPhase29BFixtureFactoryFails = false;
		GPhase29BActionFactoryFails = false;
		GPhase29BObservationFactoryFails = false;
	}

	FAINpcVisualScenarioConfig MakePhase29BScenario(const FString& ActorClassPath, const FString& ActorTag)
	{
		FAINpcVisualScenarioConfig Config;
		Config.SchemaVersion = 2;
		Config.TestId = TEXT("phase29b.runtime");
		Config.Map = TEXT("/Game/Maps/AINpcTestMap");
		Config.TimeoutSec = 1;
		Config.StoryIds = { TEXT("TEST") };
		Config.PhaseIds = { TEXT("phase2.9b") };
		Config.Fixture.AdapterId = TEXT("phase29b.fixture");
		Config.Fixture.Kind = TEXT("existingActor");
		Config.Fixture.ActorClass = ActorClassPath;
		Config.Fixture.ActorTag = ActorTag;
		FAINpcVisualScenarioStep Step;
		Step.Type = TEXT("project.action.execute");
		Step.Payload.AdapterId = TEXT("phase29b.action");
		Step.Payload.ActionName = TEXT("Interact");
		Step.Payload.TargetRef = TEXT("fixture.actor");
		Config.Steps.Add(Step);
		Config.Expect.Assertion.Operator = TEXT("equals");
		Config.Expect.Assertion.Scope = EAINpcVisualObservationScope::ScenarioHistory;
		Config.Expect.Assertion.Observation = TEXT("project.door.isOpen");
		Config.Expect.Assertion.bHasEqualsBool = true;
		Config.Expect.Assertion.EqualsBool = true;
		return Config;
	}

	void RegisterPhase29BDescriptors(FAutomationTestBase& Test, const FName Owner, const FName FixtureId, const FName ActionId, const FName ObservationId)
	{
		FAINpcVisualAdapterDescriptor FixtureDescriptor;
		FixtureDescriptor.Category = EAINpcVisualAdapterCategory::FixtureResolver;
		FixtureDescriptor.AdapterId = FixtureId;
		FixtureDescriptor.OwnerModuleName = Owner;
		FixtureDescriptor.Capabilities = { TEXT("existingActor.classTag") };
		FixtureDescriptor.CreateFixtureResolver = &MakePhase29BFixtureAdapter;
		FAINpcVisualAdapterDescriptor ActionDescriptor;
		ActionDescriptor.Category = EAINpcVisualAdapterCategory::ActionAdapter;
		ActionDescriptor.AdapterId = ActionId;
		ActionDescriptor.OwnerModuleName = Owner;
		ActionDescriptor.Capabilities = { TEXT("projectAction.doorInteract") };
		ActionDescriptor.CreateActionAdapter = &MakePhase29BActionAdapter;
		FAINpcVisualAdapterDescriptor ObservationDescriptor;
		ObservationDescriptor.Category = EAINpcVisualAdapterCategory::ObservationProvider;
		ObservationDescriptor.AdapterId = ObservationId;
		ObservationDescriptor.OwnerModuleName = Owner;
		ObservationDescriptor.Capabilities = { TEXT("observation.project.door.isOpen") };
		ObservationDescriptor.ObservationDeclarations = { MakeDoorObservationDeclaration() };
		ObservationDescriptor.CreateObservationProvider = &MakePhase29BObservationAdapter;
		Test.TestTrue(TEXT("Phase 2.9B fixture descriptor registers."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(FixtureDescriptor).IsSuccess());
		Test.TestTrue(TEXT("Phase 2.9B action descriptor registers."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(ActionDescriptor).IsSuccess());
		Test.TestTrue(TEXT("Phase 2.9B observation descriptor registers."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(ObservationDescriptor).IsSuccess());
	}

	void CleanupPhase29BDescriptors(const FName Owner, const FName FixtureId, const FName ActionId, const FName ObservationId)
	{
		FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::FixtureResolver, FixtureId, Owner);
		FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::ActionAdapter, ActionId, Owner);
		FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::ObservationProvider, ObservationId, Owner);
	}

	struct FPhase29BRunResult
	{
		bool bStarted = false;
		bool bComplete = false;
		bool bFailed = false;
		FString StartFailure;
		FString FailureReason;
		TArray<FAINpcVisualTestStepDiagnostic> Diagnostics;
	};

	FString DescribeDiagnostic(const FAINpcVisualTestStepDiagnostic& Diagnostic)
	{
		return FString::Printf(TEXT("stage=%s category=%s adapter=%s testId=%s actorClass=%s actorTag=%s targetRef=%s actionName=%s field=%s capability=%s observation=%s sourceKind=%s sourceId=%s reason=%s"),
			*Diagnostic.Stage,
			*Diagnostic.AdapterCategory,
			*Diagnostic.AdapterId,
			*Diagnostic.TestId,
			*Diagnostic.ActorClass,
			*Diagnostic.ActorTag,
			*Diagnostic.TargetRef,
			*Diagnostic.ActionName,
			*Diagnostic.FieldName,
			*Diagnostic.Capability,
			*Diagnostic.ObservationName,
			*Diagnostic.SourceKind,
			*Diagnostic.SourceId,
			*Diagnostic.FailureReason);
	}

	bool HasDiagnostic(const TArray<FAINpcVisualTestStepDiagnostic>& Diagnostics, const TArray<FString>& Terms)
	{
		for (const FAINpcVisualTestStepDiagnostic& Diagnostic : Diagnostics)
		{
			if (ContainsAll(DescribeDiagnostic(Diagnostic), Terms))
			{
				return true;
			}
		}
		return false;
	}

	FPhase29BRunResult RunPhase29BScenario(UWorld& World, const FAINpcVisualScenarioConfig& Config)
	{
		FAINpcVisualTestFixture Fixture;
		FAINpcVisualTestContext Context{ Fixture, &World, Config.TestId, TEXT("phase29b.run") };
		FAINpcDataDrivenVisualScenarioTest Test(Context, Config);
		FPhase29BRunResult Result;
		Result.bStarted = Test.Start(Result.StartFailure);
		World.GetTimerManager().Tick(3.0f);
		Test.Poll();
		World.GetTimerManager().Tick(0.1f);
		Test.Poll();
		Result.bComplete = Test.IsComplete();
		Result.bFailed = Test.HasFailed();
		Result.FailureReason = Test.GetFailureReason();
		Result.Diagnostics = Test.BuildStepDiagnostics();
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAINpcVisualExtensionRegistryBoundaryTest,
	"AINpc.Visual.Extension.RegistryBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcVisualExtensionRegistryBoundaryTest::RunTest(const FString& Parameters)
{
	const FName OwnerA(TEXT("RegistryOwnerA"));
	const FName OwnerB(TEXT("RegistryOwnerB"));
	const FName AdapterId(TEXT("registry.adapter"));
	const FName DifferentCategorySameId(TEXT("registry.duplicateAcrossCategory"));

	FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::FixtureResolver, AdapterId, OwnerA);
	FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::ActionAdapter, AdapterId, OwnerA);
	FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::FixtureResolver, DifferentCategorySameId, OwnerA);
	FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::ObservationProvider, DifferentCategorySameId, OwnerA);

	FAINpcVisualAdapterDescriptor EmptyIdDescriptor;
	EmptyIdDescriptor.Category = EAINpcVisualAdapterCategory::FixtureResolver;
	EmptyIdDescriptor.OwnerModuleName = OwnerA;
	EmptyIdDescriptor.Capabilities = { TEXT("existingActor.classTag") };
	EmptyIdDescriptor.CreateFixtureResolver = [](const FAINpcVisualAdapterCreateContext&) { return FAINpcVisualAdapterFactoryResult::Failure(TEXT("not used")); };
	TestFalse(TEXT("Registry rejects empty adapter id."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(EmptyIdDescriptor).IsSuccess());

	FAINpcVisualAdapterDescriptor EmptyCapabilitiesDescriptor;
	EmptyCapabilitiesDescriptor.Category = EAINpcVisualAdapterCategory::FixtureResolver;
	EmptyCapabilitiesDescriptor.AdapterId = FName(TEXT("registry.emptyCapabilities"));
	EmptyCapabilitiesDescriptor.OwnerModuleName = OwnerA;
	EmptyCapabilitiesDescriptor.CreateFixtureResolver = [](const FAINpcVisualAdapterCreateContext&) { return FAINpcVisualAdapterFactoryResult::Failure(TEXT("not used")); };
	TestFalse(TEXT("Registry rejects descriptors with no capabilities."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(EmptyCapabilitiesDescriptor).IsSuccess());

	FAINpcVisualAdapterDescriptor EmptyCapabilityDescriptor;
	EmptyCapabilityDescriptor.Category = EAINpcVisualAdapterCategory::ActionAdapter;
	EmptyCapabilityDescriptor.AdapterId = FName(TEXT("registry.emptyCapability"));
	EmptyCapabilityDescriptor.OwnerModuleName = OwnerA;
	EmptyCapabilityDescriptor.Capabilities = { FString() };
	EmptyCapabilityDescriptor.CreateActionAdapter = [](const FAINpcVisualAdapterCreateContext&) { return FAINpcVisualAdapterFactoryResult::Failure(TEXT("not used")); };
	TestFalse(TEXT("Registry rejects empty capability entries."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(EmptyCapabilityDescriptor).IsSuccess());

	FAINpcVisualAdapterDescriptor ExtraFactoryDescriptor;
	ExtraFactoryDescriptor.Category = EAINpcVisualAdapterCategory::FixtureResolver;
	ExtraFactoryDescriptor.AdapterId = FName(TEXT("registry.extraFactory"));
	ExtraFactoryDescriptor.OwnerModuleName = OwnerA;
	ExtraFactoryDescriptor.Capabilities = { TEXT("existingActor.classTag") };
	ExtraFactoryDescriptor.CreateFixtureResolver = [](const FAINpcVisualAdapterCreateContext&) { return FAINpcVisualAdapterFactoryResult::Failure(TEXT("not used")); };
	ExtraFactoryDescriptor.CreateActionAdapter = [](const FAINpcVisualAdapterCreateContext&) { return FAINpcVisualAdapterFactoryResult::Failure(TEXT("not used")); };
	TestFalse(TEXT("Registry rejects descriptors with factories outside the selected category."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(ExtraFactoryDescriptor).IsSuccess());

	FAINpcVisualAdapterDescriptor FixtureDescriptor;
	FixtureDescriptor.Category = EAINpcVisualAdapterCategory::FixtureResolver;
	FixtureDescriptor.AdapterId = AdapterId;
	FixtureDescriptor.OwnerModuleName = OwnerA;
	FixtureDescriptor.Capabilities = { TEXT("existingActor.classTag") };
	FixtureDescriptor.CreateFixtureResolver = [](const FAINpcVisualAdapterCreateContext&) { return FAINpcVisualAdapterFactoryResult::Failure(TEXT("no runtime instance in registry boundary test")); };
	TestTrue(TEXT("Registry accepts valid fixture descriptor."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(FixtureDescriptor).IsSuccess());
	TestFalse(TEXT("Registry rejects duplicate id in the same category."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(FixtureDescriptor).IsSuccess());

	FAINpcVisualAdapterDescriptor ActionDuplicateDescriptor;
	ActionDuplicateDescriptor.Category = EAINpcVisualAdapterCategory::ActionAdapter;
	ActionDuplicateDescriptor.AdapterId = AdapterId;
	ActionDuplicateDescriptor.OwnerModuleName = OwnerB;
	ActionDuplicateDescriptor.Capabilities = { TEXT("projectAction.doorInteract") };
	ActionDuplicateDescriptor.CreateActionAdapter = [](const FAINpcVisualAdapterCreateContext&) { return FAINpcVisualAdapterFactoryResult::Failure(TEXT("no runtime instance in registry boundary test")); };
	TestFalse(TEXT("Registry rejects duplicate id in a different category."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(ActionDuplicateDescriptor).IsSuccess());
	TestFalse(TEXT("Unregister rejects the wrong owner."), FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::FixtureResolver, AdapterId, OwnerB).IsSuccess());
	TestFalse(TEXT("Still-registered descriptor rejects duplicate registration after wrong-owner unregister."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(FixtureDescriptor).IsSuccess());
	TestTrue(TEXT("Unregister accepts the matching owner."), FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::FixtureResolver, AdapterId, OwnerA).IsSuccess());
	TestTrue(TEXT("Matching-owner unregister removes descriptor so registration can succeed again."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(FixtureDescriptor).IsSuccess());
	TestTrue(TEXT("Cleanup unregister succeeds after re-registration."), FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::FixtureResolver, AdapterId, OwnerA).IsSuccess());

	FAINpcVisualAdapterDescriptor FixtureDuplicateAcrossCategory;
	FixtureDuplicateAcrossCategory.Category = EAINpcVisualAdapterCategory::FixtureResolver;
	FixtureDuplicateAcrossCategory.AdapterId = DifferentCategorySameId;
	FixtureDuplicateAcrossCategory.OwnerModuleName = OwnerA;
	FixtureDuplicateAcrossCategory.Capabilities = { TEXT("existingActor.classTag") };
	FixtureDuplicateAcrossCategory.CreateFixtureResolver = [](const FAINpcVisualAdapterCreateContext&) { return FAINpcVisualAdapterFactoryResult::Failure(TEXT("no runtime instance in registry boundary test")); };
	FAINpcVisualAdapterDescriptor ObservationDuplicateAcrossCategory;
	ObservationDuplicateAcrossCategory.Category = EAINpcVisualAdapterCategory::ObservationProvider;
	ObservationDuplicateAcrossCategory.AdapterId = DifferentCategorySameId;
	ObservationDuplicateAcrossCategory.OwnerModuleName = OwnerA;
	ObservationDuplicateAcrossCategory.Capabilities = { TEXT("observation.project.door.isOpen") };
	ObservationDuplicateAcrossCategory.ObservationDeclarations = { MakeDoorObservationDeclaration() };
	ObservationDuplicateAcrossCategory.CreateObservationProvider = [](const FAINpcVisualAdapterCreateContext&) { return FAINpcVisualAdapterFactoryResult::Failure(TEXT("no runtime instance in registry boundary test")); };
	TestTrue(TEXT("Registry accepts first descriptor for cross-category duplicate check."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(FixtureDuplicateAcrossCategory).IsSuccess());
	TestFalse(TEXT("Registry rejects same id even when category differs."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(ObservationDuplicateAcrossCategory).IsSuccess());
	TestTrue(TEXT("Cleanup unregister succeeds."), FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::FixtureResolver, DifferentCategorySameId, OwnerA).IsSuccess());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAINpcVisualLifecycleLifecycleTest,
	"AINpc.Visual.Lifecycle.LifecycleAndStaleOwner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcVisualLifecycleLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace AINpc::Visual::TestInternal;

	const FName Owner(TEXT("LifecycleOwner"));
	const FName WrongOwner(TEXT("LifecycleWrongOwner"));
	const FName ActionId(TEXT("lifecycle.action"));
	ResetLifecycleCounters();
	FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::ActionAdapter, ActionId, Owner);

	FAINpcVisualAdapterDescriptor Descriptor = MakeActionDescriptor(ActionId, Owner);
	TestTrue(TEXT("ModuleStartup registration succeeds."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(Descriptor).IsSuccess());

	const FAINpcVisualAdapterRegistrationResult WrongOwnerResult = FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::ActionAdapter, ActionId, WrongOwner);
	TestFalse(TEXT("ModuleShutdown rejects wrong owner."), WrongOwnerResult.IsSuccess());
	TestTrue(TEXT("Wrong-owner diagnostic carries lifecycle identity."), ContainsAll(WrongOwnerResult.Diagnostic, { TEXT("stage=ModuleShutdown"), TEXT("owner=LifecycleOwner"), TEXT("category=ActionAdapter"), TEXT("adapter=lifecycle.action") }));

	FAINpcVisualAdapterCreateContext NullWorldContext;
	NullWorldContext.TestId = TEXT("LifecycleNullWorld");
	NullWorldContext.RunId = TEXT("RunNullWorld");
	TSharedPtr<FAdapterRunView> NullWorldView;
	const FAdapterRunViewCreateResult NullWorldResult = FAdapterRunView::Create(NullWorldContext, NullWorldView);
	TestFalse(TEXT("RuntimeStartup rejects null world before factory invocation."), NullWorldResult.IsSuccess());
	TestEqual(TEXT("Null world path does not invoke factories."), GLifecycleFactoryCalls, 0);
	TestTrue(TEXT("Null-world diagnostic carries stage and test id."), ContainsAll(NullWorldResult.Diagnostic, { TEXT("stage=RuntimeStartup"), TEXT("testId=LifecycleNullWorld") }));

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("LifecycleLifecycleWorld"));
	FAINpcVisualAdapterCreateContext Context;
	Context.World = World;
	Context.TestId = TEXT("LifecycleRun");
	Context.RunId = TEXT("RunA");
	Context.StoryIds = { TEXT("AdapterLifecycle") };
	Context.PhaseIds = { TEXT("LifecycleHardening") };
	FAINpcVisualAdapterDiagnosticSink DiagnosticSink;
	Context.DiagnosticSink = &DiagnosticSink;

	TSharedPtr<FAdapterRunView> RunA;
	const FAdapterRunViewCreateResult RunAResult = FAdapterRunView::Create(Context, RunA);
	TestTrue(TEXT("ScenarioStart creates per-run view after world is available."), RunAResult.IsSuccess());
	TestEqual(TEXT("Factory invoked once for RunA."), GLifecycleFactoryCalls, 1);
	TestEqual(TEXT("Factory receives current world."), GLifecycleCapturedWorld, World);
	TestEqual(TEXT("Factory receives test id."), GLifecycleCapturedTestId, FString(TEXT("LifecycleRun")));
	TestEqual(TEXT("Factory receives run id."), GLifecycleCapturedRunId, FString(TEXT("RunA")));
	TestEqual(TEXT("Factory receives one story metadata entry."), GLifecycleCapturedStoryIds.Num(), 1);
	TestTrue(TEXT("Factory receives story metadata."), GLifecycleCapturedStoryIds.Contains(TEXT("AdapterLifecycle")));
	TestEqual(TEXT("Factory receives one phase metadata entry."), GLifecycleCapturedPhaseIds.Num(), 1);
	TestTrue(TEXT("Factory receives phase metadata."), GLifecycleCapturedPhaseIds.Contains(TEXT("LifecycleHardening")));
	TestTrue(TEXT("Factory receives diagnostic sink."), GLifecycleCapturedDiagnosticSink);

	const FAdapterViewLookupResult CachedLookupBeforeUnregister = RunA->FindAdapter(EAINpcVisualAdapterCategory::ActionAdapter, ActionId, TEXT("StepExecution"));
	TestTrue(TEXT("Lookup succeeds before owner unregister."), CachedLookupBeforeUnregister.IsSuccess());
	TestTrue(TEXT("Cached lookup permits guarded action use before unregister."), CachedLookupBeforeUnregister.UseAdapter([](IAINpcVisualAdapterInstance&) { ++GLifecycleGuardedUseCalls; }));
	TestEqual(TEXT("Guarded action use invokes domain behavior before unregister."), GLifecycleGuardedUseCalls, 1);

	TestTrue(TEXT("ModuleShutdown unregister succeeds for exact owner/category/id."), FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::ActionAdapter, ActionId, Owner).IsSuccess());
	TestFalse(TEXT("Cached lookup blocks existing instance use after unregister."), CachedLookupBeforeUnregister.IsSuccess());
	FString CachedActionDiagnostic;
	TestFalse(TEXT("Cached lookup refuses guarded action use after unregister."), CachedLookupBeforeUnregister.UseAdapter([](IAINpcVisualAdapterInstance&) { ++GLifecycleGuardedUseCalls; }, &CachedActionDiagnostic));
	TestEqual(TEXT("Guarded action use does not invoke domain behavior after unregister."), GLifecycleGuardedUseCalls, 1);
	TestTrue(TEXT("Cached action guarded-use diagnostic carries lifecycle identity."), ContainsAll(CachedActionDiagnostic, { TEXT("stage=StepExecution"), TEXT("owner=LifecycleOwner"), TEXT("category=ActionAdapter"), TEXT("adapter=lifecycle.action"), TEXT("testId=LifecycleRun"), TEXT("owner unavailable") }));
	const FAdapterViewLookupResult LookupAfterUnregister = RunA->FindAdapter(EAINpcVisualAdapterCategory::ActionAdapter, ActionId, TEXT("StepExecution"));
	TestFalse(TEXT("Active run blocks stale owner lookup after unregister."), LookupAfterUnregister.IsSuccess());
	TestTrue(TEXT("Stale-owner diagnostic carries step identity."), ContainsAll(LookupAfterUnregister.Diagnostic, { TEXT("stage=StepExecution"), TEXT("owner=LifecycleOwner"), TEXT("category=ActionAdapter"), TEXT("adapter=lifecycle.action"), TEXT("testId=LifecycleRun"), TEXT("owner unavailable") }));

	const int32 DestroyBeforeRelease = GLifecycleDestroyCalls;
	RunA->EndScenario(TEXT("ScenarioEnd"));
	TestEqual(TEXT("ScenarioEnd releases adapter instance."), GLifecycleDestroyCalls, DestroyBeforeRelease + 1);
	const FAdapterViewLookupResult LookupAfterEnd = RunA->FindAdapter(EAINpcVisualAdapterCategory::ActionAdapter, ActionId, TEXT("FinalAssertion"));
	TestFalse(TEXT("Ended run view cannot provide adapters."), LookupAfterEnd.IsSuccess());
	TestTrue(TEXT("Ended-view diagnostic carries final assertion stage."), ContainsAll(LookupAfterEnd.Diagnostic, { TEXT("stage=FinalAssertion"), TEXT("category=ActionAdapter"), TEXT("adapter=lifecycle.action"), TEXT("testId=LifecycleRun") }));
	RunA.Reset();

	Context.RunId = TEXT("RunB");
	TestTrue(TEXT("Re-registration after shutdown creates fresh owner state."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(Descriptor).IsSuccess());
	TSharedPtr<FAdapterRunView> RunB;
	const FAdapterRunViewCreateResult RunBResult = FAdapterRunView::Create(Context, RunB);
	TestTrue(TEXT("Second scenario run creates a fresh view."), RunBResult.IsSuccess());
	TestEqual(TEXT("Factory invoked again for fresh run isolation."), GLifecycleFactoryCalls, 2);
	bool bFixtureRefResolvable = true;
	bool bObservationStoreWritable = true;
	bool bReleaseCallbackCouldUseAdapter = true;
	RunB->AddReleaseCallback([&bFixtureRefResolvable, &bObservationStoreWritable, &bReleaseCallbackCouldUseAdapter, RunB, ActionId]()
	{
		bReleaseCallbackCouldUseAdapter = RunB->FindAdapter(EAINpcVisualAdapterCategory::ActionAdapter, ActionId, TEXT("WorldTeardown")).UseAdapter([](IAINpcVisualAdapterInstance&) { ++GLifecycleGuardedUseCalls; });
		bFixtureRefResolvable = false;
		bObservationStoreWritable = false;
		++GLifecycleReleaseCalls;
	});
	const int32 DestroyBeforeWorldTeardown = GLifecycleDestroyCalls;
	const int32 ReleaseBeforeWorldTeardown = GLifecycleReleaseCalls;
	RunB->EndScenario(TEXT("WorldTeardown"));
	TestEqual(TEXT("WorldTeardown releases fresh run adapter instance."), GLifecycleDestroyCalls, DestroyBeforeWorldTeardown + 1);
	TestEqual(TEXT("WorldTeardown releases fixture and observation owner state."), GLifecycleReleaseCalls, ReleaseBeforeWorldTeardown + 1);
	TestFalse(TEXT("WorldTeardown release callback cannot use adapters during teardown."), bReleaseCallbackCouldUseAdapter);
	TestFalse(TEXT("WorldTeardown makes fixture refs unavailable."), bFixtureRefResolvable);
	TestFalse(TEXT("WorldTeardown makes live observation store unavailable for writes."), bObservationStoreWritable);
	const FAdapterViewLookupResult LookupAfterWorldTeardown = RunB->FindAdapter(EAINpcVisualAdapterCategory::ActionAdapter, ActionId, TEXT("WorldTeardown"));
	TestFalse(TEXT("WorldTeardown-ended view cannot provide adapters."), LookupAfterWorldTeardown.IsSuccess());
	TestTrue(TEXT("WorldTeardown diagnostic carries lifecycle identity."), ContainsAll(LookupAfterWorldTeardown.Diagnostic, { TEXT("stage=WorldTeardown"), TEXT("owner=LifecycleOwner"), TEXT("category=ActionAdapter"), TEXT("adapter=lifecycle.action"), TEXT("testId=LifecycleRun") }));
	RunB.Reset();
	FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::ActionAdapter, ActionId, Owner);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAINpcVisualLifecycleModuleShutdownLifecycleTest,
	"AINpc.Visual.Lifecycle.ModuleStartupShutdownLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcVisualLifecycleModuleShutdownLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace AINpc::Visual::TestInternal;

	const FName Owner(TEXT("LifecycleOwnerAllCategories"));
	const FName FixtureId(TEXT("lifecycle.fixture"));
	const FName ObservationId(TEXT("lifecycle.observation"));
	const FName ActionId(TEXT("lifecycle.action.lifecycle"));
	ResetLifecycleCounters();

	FAINpcVisualAdapterDescriptor FixtureDescriptor;
	FixtureDescriptor.Category = EAINpcVisualAdapterCategory::FixtureResolver;
	FixtureDescriptor.AdapterId = FixtureId;
	FixtureDescriptor.OwnerModuleName = Owner;
	FixtureDescriptor.Capabilities = { TEXT("existingActor.classTag") };
	FixtureDescriptor.CreateFixtureResolver = &MakeLifecycleFixtureAdapter;

	FAINpcVisualAdapterDescriptor ObservationDescriptor;
	ObservationDescriptor.Category = EAINpcVisualAdapterCategory::ObservationProvider;
	ObservationDescriptor.AdapterId = ObservationId;
	ObservationDescriptor.OwnerModuleName = Owner;
	ObservationDescriptor.Capabilities = { TEXT("observation.project.door.isOpen") };
	ObservationDescriptor.ObservationDeclarations = { MakeDoorObservationDeclaration() };
	ObservationDescriptor.CreateObservationProvider = &MakeLifecycleObservationAdapter;

	FAINpcVisualAdapterDescriptor ActionDescriptor = MakeActionDescriptor(ActionId, Owner);
	TestTrue(TEXT("Fixture startup registration succeeds."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(FixtureDescriptor).IsSuccess());
	TestTrue(TEXT("Observation startup registration succeeds."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(ObservationDescriptor).IsSuccess());
	TestTrue(TEXT("Action startup registration succeeds."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(ActionDescriptor).IsSuccess());

	UWorld* ActiveWorld = UWorld::CreateWorld(EWorldType::Game, false, TEXT("LifecycleActiveObservationWorld"));
	FAINpcVisualAdapterCreateContext ActiveContext;
	ActiveContext.World = ActiveWorld;
	ActiveContext.TestId = TEXT("ActiveObservationRun");
	ActiveContext.RunId = TEXT("ActiveObservationRun");
	TSharedPtr<FAdapterRunView> ActiveView;
	const FAdapterRunViewCreateResult ActiveResult = FAdapterRunView::Create(ActiveContext, ActiveView);
	TestTrue(TEXT("Active run creates all category instances."), ActiveResult.IsSuccess());
	TestEqual(TEXT("Active run invokes all registered factories."), GLifecycleFactoryCalls, 3);
	const int32 ObservationUseBeforeLookup = GLifecycleGuardedUseCalls;
	const FAdapterViewLookupResult CachedObservationLookup = ActiveView->FindAdapter(EAINpcVisualAdapterCategory::ObservationProvider, ObservationId, TEXT("FinalAssertion"));
	TestTrue(TEXT("Observation lookup succeeds before unregister."), CachedObservationLookup.IsSuccess());
	TestTrue(TEXT("Observation lookup permits guarded sample before unregister."), CachedObservationLookup.UseAdapter([](IAINpcVisualAdapterInstance&) { ++GLifecycleGuardedUseCalls; }));
	TestEqual(TEXT("Guarded observation sample invokes domain behavior before unregister."), GLifecycleGuardedUseCalls, ObservationUseBeforeLookup + 1);
	TestTrue(TEXT("Observation shutdown unregister succeeds while run is active."), FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::ObservationProvider, ObservationId, Owner).IsSuccess());
	TestFalse(TEXT("Cached observation lookup blocks existing instance use after unregister."), CachedObservationLookup.IsSuccess());
	FString CachedObservationDiagnostic;
	TestFalse(TEXT("Cached observation lookup refuses guarded sample after unregister."), CachedObservationLookup.UseAdapter([](IAINpcVisualAdapterInstance&) { ++GLifecycleGuardedUseCalls; }, &CachedObservationDiagnostic));
	TestEqual(TEXT("Guarded observation sample does not invoke domain behavior after unregister."), GLifecycleGuardedUseCalls, ObservationUseBeforeLookup + 1);
	TestTrue(TEXT("Cached observation guarded-use diagnostic carries lifecycle identity."), ContainsAll(CachedObservationDiagnostic, { TEXT("stage=FinalAssertion"), TEXT("owner=LifecycleOwnerAllCategories"), TEXT("category=ObservationProvider"), TEXT("adapter=lifecycle.observation"), TEXT("testId=ActiveObservationRun"), TEXT("owner unavailable") }));
	const FAdapterViewLookupResult FreshObservationLookup = ActiveView->FindAdapter(EAINpcVisualAdapterCategory::ObservationProvider, ObservationId, TEXT("FinalAssertion"));
	TestFalse(TEXT("Fresh observation lookup blocks unavailable owner."), FreshObservationLookup.IsSuccess());
	TestTrue(TEXT("Observation unavailable diagnostic carries final assertion identity."), ContainsAll(FreshObservationLookup.Diagnostic, { TEXT("stage=FinalAssertion"), TEXT("owner=LifecycleOwnerAllCategories"), TEXT("category=ObservationProvider"), TEXT("adapter=lifecycle.observation"), TEXT("testId=ActiveObservationRun"), TEXT("owner unavailable") }));
	bool bActiveFixtureRefResolvable = true;
	bool bActiveObservationStoreWritable = true;
	bool bActiveReleaseCallbackCouldReenter = true;
	ActiveView->AddReleaseCallback([&bActiveFixtureRefResolvable, &bActiveObservationStoreWritable, &bActiveReleaseCallbackCouldReenter, ActiveView]()
	{
		const int32 ReleaseCallsBeforeReentry = GLifecycleReleaseCalls;
		ActiveView->EndScenario(TEXT("ScenarioEnd"));
		bActiveReleaseCallbackCouldReenter = GLifecycleReleaseCalls != ReleaseCallsBeforeReentry;
		bActiveFixtureRefResolvable = false;
		bActiveObservationStoreWritable = false;
		++GLifecycleReleaseCalls;
	});
	const int32 DestroyBeforeActiveRelease = GLifecycleDestroyCalls;
	const int32 ReleaseBeforeActiveEnd = GLifecycleReleaseCalls;
	ActiveView->EndScenario(TEXT("ScenarioEnd"));
	TestEqual(TEXT("Active view release destroys counted action and observation instances."), GLifecycleDestroyCalls, DestroyBeforeActiveRelease + 2);
	TestEqual(TEXT("ScenarioEnd releases fixture and observation owner state."), GLifecycleReleaseCalls, ReleaseBeforeActiveEnd + 1);
	TestFalse(TEXT("ScenarioEnd release callback cannot reenter teardown."), bActiveReleaseCallbackCouldReenter);
	TestFalse(TEXT("ScenarioEnd makes fixture refs unavailable."), bActiveFixtureRefResolvable);
	TestFalse(TEXT("ScenarioEnd makes live observation store unavailable for writes."), bActiveObservationStoreWritable);
	ActiveView.Reset();
	ActiveWorld->DestroyWorld(false);

	TestTrue(TEXT("Fixture shutdown unregister succeeds."), FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::FixtureResolver, FixtureId, Owner).IsSuccess());
	TestTrue(TEXT("Action shutdown unregister succeeds."), FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::ActionAdapter, ActionId, Owner).IsSuccess());

	const int32 FactoryCallsBeforePostShutdown = GLifecycleFactoryCalls;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("LifecycleShutdownWorld"));
	FAINpcVisualAdapterCreateContext Context;
	Context.World = World;
	Context.TestId = TEXT("PostShutdownRun");
	Context.RunId = TEXT("PostShutdownRun");
	TSharedPtr<FAdapterRunView> View;
	const FAdapterRunViewCreateResult Result = FAdapterRunView::Create(Context, View);
	TestTrue(TEXT("Post-shutdown run view can be empty without invoking removed factories."), Result.IsSuccess());
	TestEqual(TEXT("Removed descriptors do not invoke factories."), GLifecycleFactoryCalls, FactoryCallsBeforePostShutdown);
	const FAdapterViewLookupResult MissingFixtureLookup = View->FindAdapter(EAINpcVisualAdapterCategory::FixtureResolver, FixtureId, TEXT("RuntimeStartup"));
	TestFalse(TEXT("Post-shutdown fixture lookup cannot find removed adapter."), MissingFixtureLookup.IsSuccess());
	TestTrue(TEXT("Missing fixture lookup diagnostic carries owner, category, adapter, test, and stage."), ContainsAll(MissingFixtureLookup.Diagnostic, { TEXT("stage=RuntimeStartup"), TEXT("owner=LifecycleOwnerAllCategories"), TEXT("category=FixtureResolver"), TEXT("adapter=lifecycle.fixture"), TEXT("testId=PostShutdownRun") }));
	const FAdapterViewLookupResult MissingObservationLookup = View->FindAdapter(EAINpcVisualAdapterCategory::ObservationProvider, ObservationId, TEXT("FinalAssertion"));
	TestFalse(TEXT("Post-shutdown observation lookup cannot find removed adapter."), MissingObservationLookup.IsSuccess());
	TestTrue(TEXT("Missing observation lookup diagnostic carries owner, category, adapter, test, and stage."), ContainsAll(MissingObservationLookup.Diagnostic, { TEXT("stage=FinalAssertion"), TEXT("owner=LifecycleOwnerAllCategories"), TEXT("category=ObservationProvider"), TEXT("adapter=lifecycle.observation"), TEXT("testId=PostShutdownRun") }));
	const FAdapterViewLookupResult MissingActionLookup = View->FindAdapter(EAINpcVisualAdapterCategory::ActionAdapter, ActionId, TEXT("StepExecution"));
	TestFalse(TEXT("Post-shutdown action lookup cannot find removed adapter."), MissingActionLookup.IsSuccess());
	TestTrue(TEXT("Missing action lookup diagnostic carries owner, category, adapter, test, and stage."), ContainsAll(MissingActionLookup.Diagnostic, { TEXT("stage=StepExecution"), TEXT("owner=LifecycleOwnerAllCategories"), TEXT("category=ActionAdapter"), TEXT("adapter=lifecycle.action.lifecycle"), TEXT("testId=PostShutdownRun") }));
	View.Reset();
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAINpcVisualPhase29BProjectAdapterRuntimeTest,
	"AINpc.Visual.Phase29B.ProjectAdapterRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcVisualPhase29BProjectAdapterRuntimeTest::RunTest(const FString& Parameters)
{
	const FName Owner(TEXT("Phase29BOwner"));
	const FName FixtureId(TEXT("phase29b.fixture"));
	const FName ActionId(TEXT("phase29b.action"));
	const FName ObservationId(TEXT("phase29b.observation"));
	CleanupPhase29BDescriptors(Owner, FixtureId, ActionId, ObservationId);
	ResetPhase29BState();

	RegisterPhase29BDescriptors(*this, Owner, FixtureId, ActionId, ObservationId);

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("Phase29BProjectAdapterWorld"));
	AActor* DoorActor = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	TestNotNull(TEXT("Focused test owns a runtime source actor."), DoorActor);
	DoorActor->Tags.Add(FName(TEXT("Phase29BDoor")));
	GPhase29BFixtureActor = DoorActor;
	GPhase29BDoorOpen = true;

	FAINpcVisualTestFixture Fixture;
	FAINpcVisualTestContext Context{ Fixture, World, TEXT("phase29b.runtime"), TEXT("phase29b.run") };
	FAINpcDataDrivenVisualScenarioTest Test(Context, MakePhase29BScenario(TEXT("/Script/Engine.Actor"), TEXT("Phase29BDoor")));
	FString FailureReason;
	TestTrue(TEXT("Phase 2.9B project scenario starts with exact class/tag fixture."), Test.Start(FailureReason));
	TestTrue(TEXT("No startup failure is reported."), FailureReason.IsEmpty());
	World->GetTimerManager().Tick(3.0f);
	Test.Poll();
	TestTrue(TEXT("Project action adapter is invoked exactly once."), GPhase29BActionCalls == 1);
	TestEqual(TEXT("Action request carries actionName without core capability matching."), GPhase29BLastActionName, FString(TEXT("Interact")));
	TestEqual(TEXT("Action request receives fixture.actor target."), GPhase29BLastTarget.Get(), DoorActor);
	Test.Poll();
	TestTrue(TEXT("Final assertion completes after provider state-read observation."), Test.IsComplete());
	TestFalse(TEXT("Project scenario does not fail after state-read observation."), Test.HasFailed());
	TestEqual(TEXT("Observation provider is sampled once."), GPhase29BObservationCalls, 1);
	TestEqual(TEXT("Observation provider receives fixture.actor as source."), GPhase29BLastObservationSource.Get(), DoorActor);
	const FAINpcVisualTestObservations Observations = Test.BuildObservations();
	TestEqual(TEXT("Project observation is recorded as boolean true."), Observations.BooleanFields.FindRef(TEXT("project.door.isOpen")), true);

	World->DestroyWorld(false);
	CleanupPhase29BDescriptors(Owner, FixtureId, ActionId, ObservationId);
	ResetPhase29BState();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAINpcVisualPhase29BRuntimeNegativeMatrixTest,
	"AINpc.Visual.Phase29B.RuntimeNegativeMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcVisualPhase29BRuntimeNegativeMatrixTest::RunTest(const FString& Parameters)
{
	const FName Owner(TEXT("Phase29BNegativeOwner"));
	const FName FixtureId(TEXT("phase29b.fixture"));
	const FName ActionId(TEXT("phase29b.action"));
	const FName ObservationId(TEXT("phase29b.observation"));
	CleanupPhase29BDescriptors(Owner, FixtureId, ActionId, ObservationId);
	RegisterPhase29BDescriptors(*this, Owner, FixtureId, ActionId, ObservationId);

	auto RunCase = [this](const TCHAR* CaseName, TFunction<void(UWorld&, FAINpcVisualScenarioConfig&)> Mutate, const TArray<FString>& ExpectedTerms)
	{
		ResetPhase29BState();
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, CaseName);
		AActor* DoorActor = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		DoorActor->Tags.Add(FName(TEXT("Phase29BDoor")));
		GPhase29BFixtureActor = DoorActor;
		GPhase29BDoorOpen = true;
		FAINpcVisualScenarioConfig Config = MakePhase29BScenario(TEXT("/Script/Engine.Actor"), TEXT("Phase29BDoor"));
		Config.TestId = CaseName;
		Mutate(*World, Config);
		const FPhase29BRunResult Result = RunPhase29BScenario(*World, Config);
		TestTrue(FString(CaseName) + TEXT(" fails as a negative row."), !Result.bStarted || Result.bFailed);
		TestTrue(FString(CaseName) + TEXT(" carries expected stage diagnostics."), HasDiagnostic(Result.Diagnostics, ExpectedTerms) || ContainsAll(Result.StartFailure + Result.FailureReason, ExpectedTerms));
		World->DestroyWorld(false);
		ResetPhase29BState();
	};
	auto RunCaseWithDescriptorSetup = [this, Owner, FixtureId, ActionId, ObservationId, &RunCase](const TCHAR* CaseName, TFunction<void(UWorld&, FAINpcVisualScenarioConfig&)> Mutate, const TArray<FString>& ExpectedTerms)
	{
		CleanupPhase29BDescriptors(Owner, FixtureId, ActionId, ObservationId);
		RegisterPhase29BDescriptors(*this, Owner, FixtureId, ActionId, ObservationId);
		RunCase(CaseName, MoveTemp(Mutate), ExpectedTerms);
	};

	RunCase(TEXT("P29B-FIXTURE-004"),
		[](UWorld&, FAINpcVisualScenarioConfig& Config) { Config.Fixture.ActorTag = TEXT("MissingDoor"); },
		{ TEXT("stage=RuntimeStartup"), TEXT("category=FixtureResolver"), TEXT("actorClass=/Script/Engine.Actor"), TEXT("actorTag=MissingDoor") });
	RunCase(TEXT("P29B-FIXTURE-005"),
		[](UWorld& World, FAINpcVisualScenarioConfig&) { AActor* Duplicate = World.SpawnActor<AActor>(AActor::StaticClass(), FVector(10, 0, 0), FRotator::ZeroRotator); Duplicate->Tags.Add(FName(TEXT("Phase29BDoor"))); },
		{ TEXT("stage=RuntimeStartup"), TEXT("expected exactly one matching actor but found 2") });
	RunCase(TEXT("P29B-FIXTURE-006"),
		[](UWorld& World, FAINpcVisualScenarioConfig& Config)
		{
			for (TActorIterator<AActor> It(&World); It; ++It) { It->Tags.Reset(); }
			AActor* SubclassActor = World.SpawnActor<AAINpcTestSmartObjectActor>(AAINpcTestSmartObjectActor::StaticClass(), FVector(20, 0, 0), FRotator::ZeroRotator);
			check(SubclassActor);
			SubclassActor->Tags.Add(FName(TEXT("Phase29BDoor")));
			Config.Fixture.ActorClass = TEXT("/Script/Engine.Actor");
		},
		{ TEXT("stage=RuntimeStartup"), TEXT("expected exactly one matching actor but found 0") });
	RunCase(TEXT("P29B-FIXTURE-009"),
		[](UWorld& World, FAINpcVisualScenarioConfig&)
		{
			for (TActorIterator<AActor> It(&World); It; ++It) { It->Destroy(); }
		},
		{ TEXT("stage=RuntimeStartup"), TEXT("actorClass=/Script/Engine.Actor"), TEXT("actorTag=Phase29BDoor"), TEXT("expected exactly one matching actor but found 0") });
	RunCase(TEXT("P29B-FIXTURE-010"),
		[](UWorld&, FAINpcVisualScenarioConfig& Config) { Config.Fixture.ActorClass = TEXT("/Script/Engine.AINpcMissingNativeClass"); },
		{ TEXT("stage=RuntimeStartup"), TEXT("actorClass=/Script/Engine.AINpcMissingNativeClass"), TEXT("native class is not loaded") });
	RunCase(TEXT("P29B-FIXTURE-001-wrong-adapter-return"),
		[](UWorld&, FAINpcVisualScenarioConfig&) { GPhase29BFixtureReturnsWrongActor = true; },
		{ TEXT("stage=RuntimeStartup"), TEXT("field=targetRef"), TEXT("targetRef=fixture.actor") });
	RunCaseWithDescriptorSetup(TEXT("P29B-LIFECYCLE-001-fixture-factory"),
		[](UWorld&, FAINpcVisualScenarioConfig&) { GPhase29BFixtureFactoryFails = true; },
		{ TEXT("stage=RuntimeStartup"), TEXT("category=FixtureResolver"), TEXT("adapter=phase29b.fixture"), TEXT("fixture factory failed") });
	RunCaseWithDescriptorSetup(TEXT("P29B-LIFECYCLE-001-fixture-owner"),
		[Owner, FixtureId](UWorld&, FAINpcVisualScenarioConfig&) { FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::FixtureResolver, FixtureId, Owner); },
		{ TEXT("stage=RuntimeStartup"), TEXT("category=FixtureResolver"), TEXT("adapter=phase29b.fixture") });
	RunCaseWithDescriptorSetup(TEXT("P29B-LIFECYCLE-002-action-factory"),
		[](UWorld&, FAINpcVisualScenarioConfig&) { GPhase29BActionFactoryFails = true; },
		{ TEXT("stage=RuntimeStartup"), TEXT("category=ActionAdapter"), TEXT("adapter=phase29b.action"), TEXT("action factory failed") });
	RunCaseWithDescriptorSetup(TEXT("P29B-LIFECYCLE-002-action-owner"),
		[Owner, ActionId](UWorld&, FAINpcVisualScenarioConfig&) { FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory::ActionAdapter, ActionId, Owner); },
		{ TEXT("stage=StepExecution"), TEXT("category=ActionAdapter"), TEXT("adapter=phase29b.action") });
	RunCaseWithDescriptorSetup(TEXT("P29B-LIFECYCLE-003-observation-factory"),
		[](UWorld&, FAINpcVisualScenarioConfig&) { GPhase29BObservationFactoryFails = true; },
		{ TEXT("stage=RuntimeStartup"), TEXT("category=ObservationProvider"), TEXT("adapter=phase29b.observation"), TEXT("observation factory failed") });
	RunCaseWithDescriptorSetup(TEXT("P29B-LIFECYCLE-003-observation-owner"),
		[this, Owner, ObservationId](UWorld&, FAINpcVisualScenarioConfig&)
		{
			TestTrue(TEXT("P29B-LIFECYCLE-003 can mark observation owner unavailable before startup."),
				AINpc::Visual::TestInternal::SetRegisteredAdapterOwnerAvailabilityForTest(EAINpcVisualAdapterCategory::ObservationProvider, ObservationId, Owner, false));
		},
		{ TEXT("stage=RuntimeStartup"), TEXT("category=ObservationProvider"), TEXT("adapter=phase29b.observation"), TEXT("owner unavailable") });
	CleanupPhase29BDescriptors(Owner, FixtureId, ActionId, ObservationId);
	RegisterPhase29BDescriptors(*this, Owner, FixtureId, ActionId, ObservationId);
	RunCase(TEXT("P29B-ACTION-007"),
		[](UWorld&, FAINpcVisualScenarioConfig& Config) { Config.Steps[0].Payload.ActionName = TEXT("Kick"); },
		{ TEXT("stage=StepExecution"), TEXT("adapter=phase29b.action"), TEXT("actionName=Kick"), TEXT("targetRef=fixture.actor") });
	RunCase(TEXT("P29B-ACTION-004"),
		[](UWorld&, FAINpcVisualScenarioConfig&) { GPhase29BDoorOpen = false; },
		{ TEXT("stage=FinalAssertion"), TEXT("FailureCategory=project-observation-sample"), TEXT("Observation=project.door.isOpen") });
	for (const TPair<FString, TFunction<void()>> MetadataCase : {
		TPair<FString, TFunction<void()>>(TEXT("source-kind"), []() { GPhase29BObservationSourceKind = TEXT("action-adapter"); }),
		TPair<FString, TFunction<void()>>(TEXT("sampling"), []() { GPhase29BObservationSamplingMethod = TEXT("action-execution"); }),
		TPair<FString, TFunction<void()>>(TEXT("provider-id"), []() { GPhase29BObservationProviderIdOverride = TEXT("wrong.provider"); }),
		TPair<FString, TFunction<void()>>(TEXT("source-object"), []() { GPhase29BObservationOmitsSourceObjectPath = true; }),
		TPair<FString, TFunction<void()>>(TEXT("source-class"), []() { GPhase29BObservationOmitsSourceClass = true; })
	})
	{
		RunCase(*FString::Printf(TEXT("P29B-OBS-008-%s"), *MetadataCase.Key),
			[&MetadataCase](UWorld&, FAINpcVisualScenarioConfig&) { MetadataCase.Value(); },
			{ TEXT("stage=FinalAssertion"), TEXT("FailureCategory=project-observation-metadata"), TEXT("Observation=project.door.isOpen") });
	}

	CleanupPhase29BDescriptors(Owner, FixtureId, ActionId, ObservationId);
	return true;
}

#endif
