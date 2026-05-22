#pragma once

#include "CoreMinimal.h"

class AAINpcTestCharacter;
class AAINpcTestSmartObjectActor;

struct FAINpcVisualScenarioFixtureSpec
{
	FString Type;
};

struct FAINpcVisualScenarioPersonaSpec
{
	FString File;
	FString DelayFillerFile;
	float DelayFillerThreshold = 0.0f;
};

struct FAINpcVisualScenarioPromptSpec
{
	FString File;
	TMap<FString, FString> Variables;
};

struct FAINpcVisualScenarioStepPayload
{
	FString PromptRef;
	FString EventTag;
	FString EventId;
	FString Observation;
	FString AssertionScope;
	float TimeoutSec = 0.0f;
	float DurationSec = 0.0f;
	bool bAllowActionRejection = false;
};

struct FAINpcVisualScenarioAssertion
{
	FString Operator;
	FString Observation;
	FString EqualsString;
	bool bHasEqualsBool = false;
	bool EqualsBool = false;
	TArray<FAINpcVisualScenarioAssertion> Children;
};

struct FAINpcVisualScenarioStep
{
	FString Type;
	FAINpcVisualScenarioStepPayload Payload;
	FAINpcVisualScenarioAssertion Condition;
};

struct FAINpcVisualScenarioExpectation
{
	FAINpcVisualScenarioAssertion Assertion;
};

struct FAINpcVisualScenarioConfig
{
	int32 SchemaVersion = 0;
	FString TestId;
	FString Map;
	int32 TimeoutSec = 0;
	TArray<FString> StoryIds;
	TArray<FString> PhaseIds;
	FAINpcVisualScenarioFixtureSpec Fixture;
	FAINpcVisualScenarioPersonaSpec Persona;
	FAINpcVisualScenarioPromptSpec Prompt;
	TArray<FAINpcVisualScenarioStep> Steps;
	FAINpcVisualScenarioExpectation Expect;
};

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
	FString TestId;
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
	TOptional<FAINpcVisualScenarioConfig> ScenarioConfig;
};
