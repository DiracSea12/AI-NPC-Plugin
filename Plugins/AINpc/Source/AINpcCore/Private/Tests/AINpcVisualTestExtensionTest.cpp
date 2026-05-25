#include "Test/AINpcVisualTestExtension.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAINpcVisualPhase29A1ExtensionRegistryBoundaryTest,
	"AINpc.Visual.Phase29A1.ExtensionRegistryBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcVisualPhase29A1ExtensionRegistryBoundaryTest::RunTest(const FString& Parameters)
{
	const FName OwnerA(TEXT("Phase29A1OwnerA"));
	const FName OwnerB(TEXT("Phase29A1OwnerB"));
	const FName AdapterId(TEXT("phase29A1.adapter"));
	const FName DifferentCategorySameId(TEXT("phase29A1.duplicateAcrossCategory"));

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
	EmptyCapabilitiesDescriptor.AdapterId = FName(TEXT("phase29A1.emptyCapabilities"));
	EmptyCapabilitiesDescriptor.OwnerModuleName = OwnerA;
	EmptyCapabilitiesDescriptor.CreateFixtureResolver = [](const FAINpcVisualAdapterCreateContext&) { return FAINpcVisualAdapterFactoryResult::Failure(TEXT("not used")); };
	TestFalse(TEXT("Registry rejects descriptors with no capabilities."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(EmptyCapabilitiesDescriptor).IsSuccess());

	FAINpcVisualAdapterDescriptor EmptyCapabilityDescriptor;
	EmptyCapabilityDescriptor.Category = EAINpcVisualAdapterCategory::ActionAdapter;
	EmptyCapabilityDescriptor.AdapterId = FName(TEXT("phase29A1.emptyCapability"));
	EmptyCapabilityDescriptor.OwnerModuleName = OwnerA;
	EmptyCapabilityDescriptor.Capabilities = { FString() };
	EmptyCapabilityDescriptor.CreateActionAdapter = [](const FAINpcVisualAdapterCreateContext&) { return FAINpcVisualAdapterFactoryResult::Failure(TEXT("not used")); };
	TestFalse(TEXT("Registry rejects empty capability entries."), FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(EmptyCapabilityDescriptor).IsSuccess());

	FAINpcVisualAdapterDescriptor ExtraFactoryDescriptor;
	ExtraFactoryDescriptor.Category = EAINpcVisualAdapterCategory::FixtureResolver;
	ExtraFactoryDescriptor.AdapterId = FName(TEXT("phase29A1.extraFactory"));
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

#endif
