#include "Components/AINpcComponent.h"
#include "LLM/LLMConcurrencyManager.h"
#include "Misc/AutomationTest.h"

#if defined(WITH_DEV_AUTOMATION_TESTS) && WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAINpcConcurrencyBasicTest, "AINpc.Concurrency.Basic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcConcurrencyBasicTest::RunTest(const FString& Parameters)
{
	UAINpcComponent::ResetConcurrencyStateForTest();

	TestEqual(TEXT("Initial dialogue slots"), UAINpcComponent::GetActiveDialogueRequestSlotsForTest(), 0);
	TestEqual(TEXT("Initial dialogue queue"), UAINpcComponent::GetQueuedDialogueRequestCountForTest(), 0);
	TestEqual(TEXT("Initial memory slots"), UAINpcComponent::GetActiveMemoryMaintenanceSlotsForTest(), 0);
	TestEqual(TEXT("Initial memory queue"), UAINpcComponent::GetQueuedMemoryMaintenanceRequestCountForTest(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAINpcConcurrencyDialogueSaturationTest, "AINpc.Concurrency.DialogueSaturation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcConcurrencyDialogueSaturationTest::RunTest(const FString& Parameters)
{
	UAINpcComponent::ResetConcurrencyStateForTest();

	// Simulate dialogue limit of 2
	UAINpcComponent::SetActiveDialogueRequestSlotsForTest(2);
	TestEqual(TEXT("Active slots at limit"), UAINpcComponent::GetActiveDialogueRequestSlotsForTest(), 2);

	// Manager should queue new requests when at limit
	FLLMConcurrencyManager& Manager = FLLMConcurrencyManager::Get();
	UAINpcComponent* DummyComp = NewObject<UAINpcComponent>();
	uint64 QueueToken = 0;
	bool bAcquired = Manager.TryAcquireDialogueSlot(DummyComp, QueueToken);

	TestFalse(TEXT("Should not acquire slot when at limit"), bAcquired);
	TestTrue(TEXT("Should receive queue token"), QueueToken != 0);
	TestEqual(TEXT("Queue should have 1 entry"), UAINpcComponent::GetQueuedDialogueRequestCountForTest(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAINpcConcurrencyDialoguePumpTest, "AINpc.Concurrency.DialoguePump",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcConcurrencyDialoguePumpTest::RunTest(const FString& Parameters)
{
	UAINpcComponent::ResetConcurrencyStateForTest();

	// Set slots to 1, then release to trigger pump
	UAINpcComponent::SetActiveDialogueRequestSlotsForTest(1);

	FLLMConcurrencyManager& Manager = FLLMConcurrencyManager::Get();
	Manager.ReleaseDialogueSlot();

	TestEqual(TEXT("Slots should be 0 after release"), UAINpcComponent::GetActiveDialogueRequestSlotsForTest(), 0);
	TestEqual(TEXT("Queue should be empty after pump"), UAINpcComponent::GetQueuedDialogueRequestCountForTest(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAINpcConcurrencyMemorySaturationTest, "AINpc.Concurrency.MemorySaturation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcConcurrencyMemorySaturationTest::RunTest(const FString& Parameters)
{
	UAINpcComponent::ResetConcurrencyStateForTest();

	// Simulate memory limit of 1
	UAINpcComponent::SetActiveMemoryMaintenanceSlotsForTest(1);
	TestEqual(TEXT("Active memory slots at limit"), UAINpcComponent::GetActiveMemoryMaintenanceSlotsForTest(), 1);

	FLLMConcurrencyManager& Manager = FLLMConcurrencyManager::Get();
	UAINpcComponent* DummyComp = NewObject<UAINpcComponent>();
	uint64 QueueToken = 0;
	bool bAcquired = Manager.TryAcquireMemorySlot(DummyComp, QueueToken);

	TestFalse(TEXT("Should not acquire memory slot when at limit"), bAcquired);
	TestTrue(TEXT("Should receive memory queue token"), QueueToken != 0);
	TestEqual(TEXT("Memory queue should have 1 entry"), UAINpcComponent::GetQueuedMemoryMaintenanceRequestCountForTest(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAINpcConcurrencyMemoryPumpTest, "AINpc.Concurrency.MemoryPump",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcConcurrencyMemoryPumpTest::RunTest(const FString& Parameters)
{
	UAINpcComponent::ResetConcurrencyStateForTest();

	UAINpcComponent::SetActiveMemoryMaintenanceSlotsForTest(1);

	FLLMConcurrencyManager& Manager = FLLMConcurrencyManager::Get();
	Manager.ReleaseMemorySlot();

	TestEqual(TEXT("Memory slots should be 0 after release"), UAINpcComponent::GetActiveMemoryMaintenanceSlotsForTest(), 0);
	TestEqual(TEXT("Memory queue should be empty after pump"), UAINpcComponent::GetQueuedMemoryMaintenanceRequestCountForTest(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAINpcConcurrencyCancelQueuedTest, "AINpc.Concurrency.CancelQueued",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcConcurrencyCancelQueuedTest::RunTest(const FString& Parameters)
{
	UAINpcComponent::ResetConcurrencyStateForTest();

	UAINpcComponent::SetActiveDialogueRequestSlotsForTest(2);

	FLLMConcurrencyManager& Manager = FLLMConcurrencyManager::Get();
	UAINpcComponent* DummyComp = NewObject<UAINpcComponent>();
	uint64 QueueToken = 0;
	Manager.TryAcquireDialogueSlot(DummyComp, QueueToken);

	TestEqual(TEXT("Queue should have 1 entry before cancel"), UAINpcComponent::GetQueuedDialogueRequestCountForTest(), 1);

	Manager.CancelQueuedDialogueRequest(DummyComp, QueueToken);

	TestEqual(TEXT("Queue should be empty after cancel"), UAINpcComponent::GetQueuedDialogueRequestCountForTest(), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
