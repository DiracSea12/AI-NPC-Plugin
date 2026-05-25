#include "Test/AINpcVisualTestExtension.h"

#include "Test/AINpcVisualTestExtensionInternal.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/World.h"
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
	};

	struct FLifecycleFixtureAdapter : IAINpcVisualFixtureResolverAdapter
	{
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
	ObservationDuplicateAcrossCategory.ObservationDeclarations = { TEXT("project.door.isOpen") };
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
	ObservationDescriptor.ObservationDeclarations = { TEXT("project.door.isOpen") };
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

#endif
