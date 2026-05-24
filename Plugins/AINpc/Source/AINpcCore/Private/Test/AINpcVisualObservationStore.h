#pragma once

#include "CoreMinimal.h"
#include "Test/AINpcVisualTest.h"

struct FAINpcVisualAssertionFailureDetail
{
	FString Category;
	FString ObservationName;
	FString SourceKind;
	FString SourceId;
	FString Message;
};

struct FAINpcVisualObservationSourceInfo
{
	FString SourceKind;
	FString SourceIdentity;
	FString SourceObjectPath;
	FString SourceClass;
	FString SamplingMethod;
	FString AdapterOrProviderId;
};

struct FAINpcVisualObservationWindow
{
	int32 StepIndex = INDEX_NONE;
	double StartSeconds = 0.0;
	double EndSeconds = 0.0;
	bool bScenarioHistory = false;
};

struct FAINpcRecordedVisualObservation
{
	FAINpcVisualObservationRecord Record;
};

struct FAINpcVisualObservationReadinessRecord
{
	FString ObservationName;
	FString SourceKind;
	FString SourceId;
	int32 StepIndex = INDEX_NONE;
	double TimestampSeconds = 0.0;
	double CoverageStartSeconds = 0.0;
	double CoverageEndSeconds = 0.0;
};

class FAINpcVisualObservationStore
{
public:
	bool RecordObservation(FAINpcVisualObservationRecord Record, bool bRequiresAllowedFinalSource, FString& OutFailureReason);
	void MarkSourceReady(const FString& Name, const FAINpcVisualObservationSourceInfo& SourceInfo, int32 StepIndex, double TimestampSeconds);
	void MarkSourceReadyForWindow(const FString& Name, const FAINpcVisualObservationSourceInfo& SourceInfo, const FAINpcVisualObservationWindow& Window);

	void RecordBoolSummary(const FString& Name, bool bValue);
	void RecordIntegerSummary(const FString& Name, int32 Value);
	void RecordNumberSummary(const FString& Name, double Value);
	void RecordStringSummary(const FString& Name, const FString& Value);
	bool HasTrueBooleanSummary(const FString& Name) const;

	FAINpcVisualTestObservations BuildObservations() const;

	bool EvaluateAssertion(const FAINpcVisualScenarioAssertion& Assertion, const FAINpcVisualObservationWindow& Window, FAINpcVisualAssertionFailureDetail* OutFailure) const;
	bool EvaluateEqualsAssertion(const FAINpcVisualScenarioAssertion& Assertion, const FAINpcVisualObservationWindow& Window, FAINpcVisualAssertionFailureDetail* OutFailure) const;
	bool EvaluateExistsAssertion(const FAINpcVisualScenarioAssertion& Assertion, const FAINpcVisualObservationWindow& Window, FAINpcVisualAssertionFailureDetail* OutFailure) const;
	bool EvaluateNotExistsAssertion(const FAINpcVisualScenarioAssertion& Assertion, const FAINpcVisualObservationWindow& Window, FAINpcVisualAssertionFailureDetail* OutFailure) const;
	bool TryGetLatestObservationInWindow(const FString& Name, const FAINpcVisualObservationWindow& Window, FAINpcRecordedVisualObservation& OutObservation, FAINpcVisualAssertionFailureDetail* OutFailure) const;
	bool HasObservationInWindow(const FString& Name, const FAINpcVisualObservationWindow& Window) const;
	bool HasReadinessCoverageInWindow(const FString& Name, const FAINpcVisualObservationWindow& Window, FAINpcVisualAssertionFailureDetail* OutFailure) const;

	static FAINpcVisualObservationReadinessRecord MakeReadinessRecord(const FString& Name, const FAINpcVisualObservationSourceInfo& SourceInfo, int32 StepIndex, double TimestampSeconds, double CoverageStartSeconds, double CoverageEndSeconds);
	static FAINpcVisualObservationWindow ApplyAssertionScope(const FAINpcVisualScenarioAssertion& Assertion, const FAINpcVisualObservationWindow& Window);
	static bool IsFinalSuccessObservationName(const FString& Name);
	static bool IsFinalSuccessSourceAllowed(const FAINpcVisualObservationSourceInfo& SourceInfo);
	static bool IsVisibleActionOutcomeSourceAllowed(const FAINpcVisualObservationRecord& Record);

private:
	TMap<FString, bool> BoolSummaries;
	TMap<FString, int32> IntegerSummaries;
	TMap<FString, double> NumberSummaries;
	TMap<FString, FString> StringSummaries;
	TArray<FAINpcVisualObservationReadinessRecord> ReadinessRecords;
	TArray<FAINpcRecordedVisualObservation> Records;
};
