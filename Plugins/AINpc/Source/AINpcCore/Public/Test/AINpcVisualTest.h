#pragma once

#include "CoreMinimal.h"

class AAINpcTestCharacter;
class AAINpcTestSmartObjectActor;
class UWorld;

struct FAINpcVisualScenarioFixtureSpec
{
	FString AdapterId;
	FString Kind;
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
	FString AdapterId;
	FString EventTag;
	FString EventId;
	FString TargetRef;
	FString ActorRef;
	FString Observation;
	float TimeoutSec = 0.0f;
	float DurationSec = 0.0f;
	bool bAllowActionRejection = false;
};

enum class EAINpcVisualObservationScope : uint8
{
	CurrentStep,
	ScenarioHistory
};

struct FAINpcVisualScenarioAssertion
{
	FString Operator;
	EAINpcVisualObservationScope Scope = EAINpcVisualObservationScope::CurrentStep;
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

enum class EAINpcVisualObservationValueType : uint8
{
	Boolean,
	Integer,
	Number,
	String
};

struct FAINpcVisualObservationRecord
{
	FString Name;
	EAINpcVisualObservationValueType ValueType = EAINpcVisualObservationValueType::Boolean;
	bool BoolValue = false;
	int32 IntegerValue = 0;
	double NumberValue = 0.0;
	FString StringValue;
	FString SourceKind;
	FString SourceIdentity;
	FString SourceObjectPath;
	FString SourceClass;
	FString SamplingMethod;
	FString AdapterOrProviderId;
	int32 StepIndex = INDEX_NONE;
	double TimestampSeconds = 0.0;
	double ElapsedSeconds = 0.0;
};

struct FAINpcVisualTestObservations
{
	TMap<FString, bool> BooleanFields;
	TMap<FString, int32> IntegerFields;
	TMap<FString, double> NumberFields;
	TMap<FString, FString> StringFields;
	TArray<FAINpcVisualObservationRecord> Records;
};

struct FAINpcVisualTestStepDiagnostic
{
	FString TestId;
	int32 StepIndex = INDEX_NONE;
	FString StepType;
	FString Status;
	FString FailureReason;
	FString FailureCategory;
	FString ObservationName;
	FString SourceKind;
	FString SourceId;
	double DurationMs = 0.0;
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
	UWorld* World = nullptr;
	FString TestId;
	FString RunId;
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
	virtual TArray<FAINpcVisualTestStepDiagnostic> BuildStepDiagnostics() const = 0;
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
