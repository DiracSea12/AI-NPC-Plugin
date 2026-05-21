#pragma once

#include "CoreMinimal.h"

class AAINpcTestCharacter;
class AAINpcTestSmartObjectActor;

struct FAINpcVisualTestObservations
{
	TMap<FString, bool> BooleanFields;
	TMap<FString, int32> IntegerFields;
	TMap<FString, double> NumberFields;
	TMap<FString, FString> StringFields;
};

struct FAINpcVisualTestFixture
{
	AAINpcTestCharacter* Npc = nullptr;
	AAINpcTestSmartObjectActor* SmartObject = nullptr;
};

enum class EAINpcVisualTestFixtureKind : uint8
{
	NpcOnly,
	NpcWithSmartObject
};

struct FAINpcVisualTestContext
{
	FAINpcVisualTestFixture& Fixture;
};

class IAINpcVisualTest
{
public:
	virtual ~IAINpcVisualTest() = default;

	virtual bool Start(FString& OutFailureReason) = 0;
	virtual void Poll() = 0;
	virtual bool IsComplete() const = 0;
	virtual bool HasFailed() const = 0;
	virtual const FString& GetFailureReason() const = 0;
	virtual FString BuildSummary() const = 0;
	virtual FAINpcVisualTestObservations BuildObservations() const = 0;
};

struct FAINpcVisualTestDescriptor
{
	FString TestId;
	TArray<FString> StoryIds;
	TArray<FString> PhaseIds;
	EAINpcVisualTestFixtureKind FixtureKind = EAINpcVisualTestFixtureKind::NpcOnly;
	TUniquePtr<IAINpcVisualTest> (*CreateTest)(FAINpcVisualTestContext& Context) = nullptr;
};
