#include "Test/AINpcVisualTestRegistry.h"

#include "Test/AINpcTestCharacter.h"
#include "Test/AINpcTestSmartObjectActor.h"
#include "Test/AINpcUs1DialogueActionVisualTest.h"
#include "Test/AINpcUs2PerceptionBehaviorVisualTest.h"

namespace
{
	TUniquePtr<IAINpcVisualTest> CreateUs1DialogueActionVisualTest(FAINpcVisualTestContext& Context)
	{
		if (!Context.Fixture.Npc || !Context.Fixture.SmartObject)
		{
			return nullptr;
		}

		return MakeUnique<FAINpcUs1DialogueActionVisualTest>(*Context.Fixture.Npc, *Context.Fixture.SmartObject);
	}

	TUniquePtr<IAINpcVisualTest> CreateUs2PerceptionBehaviorVisualTest(FAINpcVisualTestContext& Context)
	{
		if (!Context.Fixture.Npc || !Context.Fixture.SmartObject)
		{
			return nullptr;
		}

		return MakeUnique<FAINpcUs2PerceptionBehaviorVisualTest>(*Context.Fixture.Npc, *Context.Fixture.SmartObject);
	}

	const TArray<FAINpcVisualTestDescriptor>& GetDescriptors()
	{
		static const TArray<FAINpcVisualTestDescriptor> Descriptors = {
			FAINpcVisualTestDescriptor{
				TEXT("us1.dialogue-action"),
				{TEXT("US-1")},
				{TEXT("phase2.5")},
				EAINpcVisualTestFixtureKind::NpcWithSmartObject,
				&CreateUs1DialogueActionVisualTest
			},
			FAINpcVisualTestDescriptor{
				TEXT("us2.perception-behavior"),
				{TEXT("US-2")},
				{TEXT("phase2.5")},
				EAINpcVisualTestFixtureKind::NpcWithSmartObject,
				&CreateUs2PerceptionBehaviorVisualTest
			}
		};
		return Descriptors;
	}
}

const FAINpcVisualTestDescriptor* FAINpcVisualTestRegistry::Find(const FString& TestId)
{
	for (const FAINpcVisualTestDescriptor& Descriptor : GetDescriptors())
	{
		if (Descriptor.TestId == TestId)
		{
			return &Descriptor;
		}
	}

	return nullptr;
}

FString FAINpcVisualTestRegistry::GetRegisteredTestIds()
{
	TArray<FString> TestIds;
	for (const FAINpcVisualTestDescriptor& Descriptor : GetDescriptors())
	{
		TestIds.Add(Descriptor.TestId);
	}

	return FString::Join(TestIds, TEXT(", "));
}
