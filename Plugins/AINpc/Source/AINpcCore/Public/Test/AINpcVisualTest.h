#pragma once

#include "CoreMinimal.h"

class AAINpcTestCharacter;
class AAINpcTestSmartObjectActor;

struct FAINpcVisualScenarioConfig
{
    FString TestId;
    FString Map;
    int32 TimeoutSec = 0;
    bool bRequiresProvider = true;
    FString PromptFile;
    FString PersonaFile;
    FString DelayFillerFile;
    float DelayFillerThreshold = 0.0f;
    bool bRequireEventTrigger = false;
    FString EventTag;
    FString EventTriggerId;
    bool bRequirePartialResponse = false;
    bool bRequireStructuredResponse = false;
    bool bRequireActionIntent = false;
    bool bAllowActionRejection = false;
    TArray<FString> StoryIds;
    TArray<FString> PhaseIds;
    TArray<FString> RequiredObservations;
    TArray<FString> AllowedTerminalOutcomes;
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
