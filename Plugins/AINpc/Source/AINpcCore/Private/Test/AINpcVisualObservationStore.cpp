#include "AINpcVisualObservationStore.h"

namespace
{
	FString GetSourceId(const FAINpcVisualObservationSourceInfo& SourceInfo)
	{
		return SourceInfo.AdapterOrProviderId.IsEmpty() ? SourceInfo.SourceIdentity : SourceInfo.AdapterOrProviderId;
	}

	FString GetRecordSourceId(const FAINpcVisualObservationRecord& Record)
	{
		return Record.AdapterOrProviderId.IsEmpty() ? Record.SourceIdentity : Record.AdapterOrProviderId;
	}
}

bool FAINpcVisualObservationStore::RecordObservation(FAINpcVisualObservationRecord Record, const bool bRequiresAllowedFinalSource, FString& OutFailureReason)
{
	const FAINpcVisualObservationSourceInfo SourceInfo{ Record.SourceKind, Record.SourceIdentity, Record.SourceObjectPath, Record.SourceClass, Record.SamplingMethod, Record.AdapterOrProviderId };
	if (bRequiresAllowedFinalSource && !IsFinalSuccessSourceAllowed(SourceInfo))
	{
		OutFailureReason = FString::Printf(TEXT("Rejected final success observation '%s' from sourceKind=%s sourceId=%s."), *Record.Name, *Record.SourceKind, *GetRecordSourceId(Record));
		return false;
	}
	FAINpcRecordedVisualObservation Stored;
	Stored.Record = MoveTemp(Record);
	Records.Add(MoveTemp(Stored));
	return true;
}

void FAINpcVisualObservationStore::MarkSourceReady(const FString& Name, const FAINpcVisualObservationSourceInfo& SourceInfo, const int32 StepIndex, const double TimestampSeconds)
{
	ReadinessRecords.Add(MakeReadinessRecord(Name, SourceInfo, StepIndex, TimestampSeconds, TimestampSeconds, TimestampSeconds));
}

void FAINpcVisualObservationStore::MarkSourceReadyForWindow(const FString& Name, const FAINpcVisualObservationSourceInfo& SourceInfo, const FAINpcVisualObservationWindow& Window)
{
	if (Window.bScenarioHistory) { return; }
	ReadinessRecords.Add(MakeReadinessRecord(Name, SourceInfo, Window.StepIndex, Window.EndSeconds, Window.StartSeconds, Window.EndSeconds));
}

void FAINpcVisualObservationStore::RecordBoolSummary(const FString& Name, const bool bValue) { BoolSummaries.Add(Name, bValue); }
void FAINpcVisualObservationStore::RecordIntegerSummary(const FString& Name, const int32 Value) { IntegerSummaries.Add(Name, Value); }
void FAINpcVisualObservationStore::RecordNumberSummary(const FString& Name, const double Value) { NumberSummaries.Add(Name, Value); }
void FAINpcVisualObservationStore::RecordStringSummary(const FString& Name, const FString& Value) { StringSummaries.Add(Name, Value); }

bool FAINpcVisualObservationStore::HasTrueBooleanSummary(const FString& Name) const
{
	const bool* Value = BoolSummaries.Find(Name);
	return Value && *Value;
}

FAINpcVisualTestObservations FAINpcVisualObservationStore::BuildObservations() const
{
	FAINpcVisualTestObservations Observations;
	Observations.BooleanFields = BoolSummaries;
	Observations.IntegerFields = IntegerSummaries;
	Observations.NumberFields = NumberSummaries;
	Observations.StringFields = StringSummaries;
	for (const FAINpcRecordedVisualObservation& Observation : Records)
	{
		Observations.Records.Add(Observation.Record);
	}
	return Observations;
}

bool FAINpcVisualObservationStore::EvaluateAssertion(const FAINpcVisualScenarioAssertion& Assertion, const FAINpcVisualObservationWindow& Window, FAINpcVisualAssertionFailureDetail* OutFailure) const
{
	if (Assertion.Operator == TEXT("all"))
	{
		for (const FAINpcVisualScenarioAssertion& Child : Assertion.Children)
		{
			FAINpcVisualAssertionFailureDetail ChildFailure;
			if (!EvaluateAssertion(Child, ApplyAssertionScope(Child, Window), &ChildFailure))
			{
				if (OutFailure) { *OutFailure = ChildFailure; }
				return false;
			}
		}
		return true;
	}
	if (Assertion.Operator == TEXT("any") || Assertion.Operator == TEXT("anyOf"))
	{
		FAINpcVisualAssertionFailureDetail LastFailure;
		for (const FAINpcVisualScenarioAssertion& Child : Assertion.Children)
		{
			FAINpcVisualAssertionFailureDetail ChildFailure;
			if (EvaluateAssertion(Child, ApplyAssertionScope(Child, Window), &ChildFailure)) { return true; }
			LastFailure = ChildFailure;
		}
		if (OutFailure) { *OutFailure = LastFailure; }
		return false;
	}
	if (Assertion.Operator == TEXT("exists")) { return EvaluateExistsAssertion(Assertion, Window, OutFailure); }
	if (Assertion.Operator == TEXT("notExists")) { return EvaluateNotExistsAssertion(Assertion, Window, OutFailure); }
	if (Assertion.Operator == TEXT("equals")) { return EvaluateEqualsAssertion(Assertion, Window, OutFailure); }
	if (OutFailure)
	{
		OutFailure->Category = TEXT("unsupported-operator");
		OutFailure->ObservationName = Assertion.Observation;
		OutFailure->Message = FString::Printf(TEXT("Unsupported assertion operator '%s'."), *Assertion.Operator);
	}
	return false;
}

bool FAINpcVisualObservationStore::EvaluateEqualsAssertion(const FAINpcVisualScenarioAssertion& Assertion, const FAINpcVisualObservationWindow& Window, FAINpcVisualAssertionFailureDetail* OutFailure) const
{
	const FAINpcVisualObservationWindow ScopedWindow = ApplyAssertionScope(Assertion, Window);
	FAINpcRecordedVisualObservation Observation;
	if (!TryGetLatestObservationInWindow(Assertion.Observation, ScopedWindow, Observation, OutFailure)) { return false; }
	if (Assertion.bHasEqualsBool)
	{
		if (Observation.Record.ValueType != EAINpcVisualObservationValueType::Boolean)
		{
			if (OutFailure)
			{
				OutFailure->Category = TEXT("type-mismatch");
				OutFailure->ObservationName = Assertion.Observation;
				OutFailure->SourceKind = Observation.Record.SourceKind;
				OutFailure->SourceId = GetRecordSourceId(Observation.Record);
				OutFailure->Message = FString::Printf(TEXT("Observation '%s' is not boolean in current step window."), *Assertion.Observation);
			}
			return false;
		}
		if (Observation.Record.BoolValue != Assertion.EqualsBool)
		{
			if (OutFailure)
			{
				OutFailure->Category = TEXT("value-mismatch");
				OutFailure->ObservationName = Assertion.Observation;
				OutFailure->SourceKind = Observation.Record.SourceKind;
				OutFailure->SourceId = GetRecordSourceId(Observation.Record);
				OutFailure->Message = FString::Printf(TEXT("Observation '%s' expected boolean %s but was %s."), *Assertion.Observation, Assertion.EqualsBool ? TEXT("true") : TEXT("false"), Observation.Record.BoolValue ? TEXT("true") : TEXT("false"));
			}
			return false;
		}
		return true;
	}
	if (Observation.Record.ValueType != EAINpcVisualObservationValueType::String)
	{
		if (OutFailure)
		{
			OutFailure->Category = TEXT("type-mismatch");
			OutFailure->ObservationName = Assertion.Observation;
			OutFailure->SourceKind = Observation.Record.SourceKind;
			OutFailure->SourceId = GetRecordSourceId(Observation.Record);
			OutFailure->Message = FString::Printf(TEXT("Observation '%s' is not string-typed in current step window."), *Assertion.Observation);
		}
		return false;
	}
	if (Observation.Record.StringValue != Assertion.EqualsString)
	{
		if (OutFailure)
		{
			OutFailure->Category = TEXT("value-mismatch");
			OutFailure->ObservationName = Assertion.Observation;
			OutFailure->SourceKind = Observation.Record.SourceKind;
			OutFailure->SourceId = GetRecordSourceId(Observation.Record);
			OutFailure->Message = FString::Printf(TEXT("Observation '%s' expected string length %d but actual length was %d."), *Assertion.Observation, Assertion.EqualsString.Len(), Observation.Record.StringValue.Len());
		}
		return false;
	}
	return true;
}

bool FAINpcVisualObservationStore::EvaluateExistsAssertion(const FAINpcVisualScenarioAssertion& Assertion, const FAINpcVisualObservationWindow& Window, FAINpcVisualAssertionFailureDetail* OutFailure) const
{
	const FAINpcVisualObservationWindow ScopedWindow = ApplyAssertionScope(Assertion, Window);
	if (!HasObservationInWindow(Assertion.Observation, ScopedWindow))
	{
		if (OutFailure)
		{
			OutFailure->Category = TEXT("missing");
			OutFailure->ObservationName = Assertion.Observation;
			OutFailure->Message = FString::Printf(TEXT("Observation '%s' does not exist in the current step window."), *Assertion.Observation);
		}
		return false;
	}
	return true;
}

bool FAINpcVisualObservationStore::EvaluateNotExistsAssertion(const FAINpcVisualScenarioAssertion& Assertion, const FAINpcVisualObservationWindow& Window, FAINpcVisualAssertionFailureDetail* OutFailure) const
{
	const FAINpcVisualObservationWindow ScopedWindow = ApplyAssertionScope(Assertion, Window);
	if (!HasReadinessCoverageInWindow(Assertion.Observation, ScopedWindow, OutFailure)) { return false; }
	if (HasObservationInWindow(Assertion.Observation, ScopedWindow))
	{
		if (OutFailure)
		{
			OutFailure->Category = TEXT("unexpected-presence");
			OutFailure->ObservationName = Assertion.Observation;
			OutFailure->Message = FString::Printf(TEXT("Observation '%s' was recorded inside the checked window."), *Assertion.Observation);
		}
		return false;
	}
	return true;
}

bool FAINpcVisualObservationStore::TryGetLatestObservationInWindow(const FString& Name, const FAINpcVisualObservationWindow& Window, FAINpcRecordedVisualObservation& OutObservation, FAINpcVisualAssertionFailureDetail* OutFailure) const
{
	FAINpcVisualAssertionFailureDetail StaleFailure;
	bool bSawStale = false;
	for (int32 Index = Records.Num() - 1; Index >= 0; --Index)
	{
		const FAINpcRecordedVisualObservation& Candidate = Records[Index];
		if (Candidate.Record.Name != Name) { continue; }
		if (!Window.bScenarioHistory && Candidate.Record.StepIndex != Window.StepIndex)
		{
			if (!bSawStale)
			{
				bSawStale = true;
				StaleFailure.Category = TEXT("stale");
				StaleFailure.ObservationName = Name;
				StaleFailure.SourceKind = Candidate.Record.SourceKind;
				StaleFailure.SourceId = GetRecordSourceId(Candidate.Record);
				StaleFailure.Message = FString::Printf(TEXT("Observation '%s' exists only from stale step %d while current step is %d."), *Name, Candidate.Record.StepIndex, Window.StepIndex);
			}
			continue;
		}
		if (Candidate.Record.TimestampSeconds < Window.StartSeconds || Candidate.Record.TimestampSeconds > Window.EndSeconds)
		{
			if (!bSawStale)
			{
				bSawStale = true;
				StaleFailure.Category = TEXT("stale");
				StaleFailure.ObservationName = Name;
				StaleFailure.SourceKind = Candidate.Record.SourceKind;
				StaleFailure.SourceId = GetRecordSourceId(Candidate.Record);
				StaleFailure.Message = FString::Printf(TEXT("Observation '%s' timestamp %.3f is outside checked window %.3f..%.3f."), *Name, Candidate.Record.TimestampSeconds, Window.StartSeconds, Window.EndSeconds);
			}
			continue;
		}
		OutObservation = Candidate;
		return true;
	}
	if (OutFailure)
	{
		if (bSawStale) { *OutFailure = StaleFailure; }
		else
		{
			OutFailure->Category = TEXT("missing");
			OutFailure->ObservationName = Name;
			OutFailure->Message = FString::Printf(TEXT("Observation '%s' is missing from the checked window."), *Name);
		}
	}
	return false;
}

bool FAINpcVisualObservationStore::HasObservationInWindow(const FString& Name, const FAINpcVisualObservationWindow& Window) const
{
	for (const FAINpcRecordedVisualObservation& Candidate : Records)
	{
		if (Candidate.Record.Name == Name && (Window.bScenarioHistory || Candidate.Record.StepIndex == Window.StepIndex) && Candidate.Record.TimestampSeconds >= Window.StartSeconds && Candidate.Record.TimestampSeconds <= Window.EndSeconds)
		{
			return true;
		}
	}
	return false;
}

bool FAINpcVisualObservationStore::HasReadinessCoverageInWindow(const FString& Name, const FAINpcVisualObservationWindow& Window, FAINpcVisualAssertionFailureDetail* OutFailure) const
{
	FAINpcVisualAssertionFailureDetail StaleFailure;
	bool bSawStale = false;
	for (int32 Index = ReadinessRecords.Num() - 1; Index >= 0; --Index)
	{
		const FAINpcVisualObservationReadinessRecord& Candidate = ReadinessRecords[Index];
		if (Candidate.ObservationName != Name) { continue; }
		if (!Window.bScenarioHistory && Candidate.StepIndex != Window.StepIndex)
		{
			if (!bSawStale)
			{
				bSawStale = true;
				StaleFailure.Category = TEXT("readiness-window-coverage");
				StaleFailure.ObservationName = Name;
				StaleFailure.SourceKind = Candidate.SourceKind;
				StaleFailure.SourceId = Candidate.SourceId;
				StaleFailure.Message = FString::Printf(TEXT("Observation '%s' only has stale readiness from step %d while checked step is %d."), *Name, Candidate.StepIndex, Window.StepIndex);
			}
			continue;
		}
		if (Candidate.CoverageStartSeconds > Window.StartSeconds || Candidate.CoverageEndSeconds < Window.EndSeconds)
		{
			if (!bSawStale)
			{
				bSawStale = true;
				StaleFailure.Category = TEXT("readiness-window-coverage");
				StaleFailure.ObservationName = Name;
				StaleFailure.SourceKind = Candidate.SourceKind;
				StaleFailure.SourceId = Candidate.SourceId;
				StaleFailure.Message = FString::Printf(TEXT("Observation '%s' readiness coverage interval %.3f..%.3f does not span checked window %.3f..%.3f."), *Name, Candidate.CoverageStartSeconds, Candidate.CoverageEndSeconds, Window.StartSeconds, Window.EndSeconds);
			}
			continue;
		}
		FAINpcVisualObservationSourceInfo SourceInfo;
		SourceInfo.SourceKind = Candidate.SourceKind;
		SourceInfo.SourceIdentity = Candidate.SourceId;
		SourceInfo.AdapterOrProviderId = Candidate.SourceId;
		if (!IsFinalSuccessSourceAllowed(SourceInfo))
		{
			if (!bSawStale)
			{
				bSawStale = true;
				StaleFailure.Category = TEXT("readiness-source");
				StaleFailure.ObservationName = Name;
				StaleFailure.SourceKind = Candidate.SourceKind;
				StaleFailure.SourceId = Candidate.SourceId;
				StaleFailure.Message = FString::Printf(TEXT("Observation '%s' readiness source '%s/%s' is not supported for notExists absence proof."), *Name, *Candidate.SourceKind, *Candidate.SourceId);
			}
			continue;
		}
		return true;
	}
	if (OutFailure)
	{
		if (bSawStale) { *OutFailure = StaleFailure; }
		else
		{
			OutFailure->Category = TEXT("readiness-window-coverage");
			OutFailure->ObservationName = Name;
			OutFailure->Message = FString::Printf(TEXT("Observation '%s' cannot satisfy notExists because readiness/window coverage is absent."), *Name);
		}
	}
	return false;
}

FAINpcVisualObservationReadinessRecord FAINpcVisualObservationStore::MakeReadinessRecord(const FString& Name, const FAINpcVisualObservationSourceInfo& SourceInfo, const int32 StepIndex, const double TimestampSeconds, const double CoverageStartSeconds, const double CoverageEndSeconds)
{
	FAINpcVisualObservationReadinessRecord Record;
	Record.ObservationName = Name;
	Record.SourceKind = SourceInfo.SourceKind;
	Record.SourceId = GetSourceId(SourceInfo);
	Record.StepIndex = StepIndex;
	Record.TimestampSeconds = TimestampSeconds;
	Record.CoverageStartSeconds = CoverageStartSeconds;
	Record.CoverageEndSeconds = CoverageEndSeconds;
	return Record;
}

FAINpcVisualObservationWindow FAINpcVisualObservationStore::ApplyAssertionScope(const FAINpcVisualScenarioAssertion& Assertion, const FAINpcVisualObservationWindow& Window)
{
	if (Assertion.Scope != EAINpcVisualObservationScope::ScenarioHistory) { return Window; }
	FAINpcVisualObservationWindow ScopedWindow = Window;
	ScopedWindow.StepIndex = INDEX_NONE;
	ScopedWindow.StartSeconds = 0.0;
	ScopedWindow.bScenarioHistory = true;
	return ScopedWindow;
}

bool FAINpcVisualObservationStore::IsFinalSuccessObservationName(const FString& Name)
{
	static const TSet<FString> Names = {
		TEXT("dialogueResponseObserved"),
		TEXT("partialResponseObserved"),
		TEXT("structuredResponseObserved"),
		TEXT("delayMaskingEndObserved"),
		TEXT("actionTargetReached")
	};
	return Names.Contains(Name);
}

bool FAINpcVisualObservationStore::IsFinalSuccessSourceAllowed(const FAINpcVisualObservationSourceInfo& SourceInfo)
{
	if (SourceInfo.SourceKind == TEXT("observation-provider"))
	{
		static const TSet<FString> RejectedSamplingMethods = { TEXT("action-execution"), TEXT("action-attempt"), TEXT("adapter-acceptance"), TEXT("fallback"), TEXT("degraded"), TEXT("failure"), TEXT("failure-path") };
		return !RejectedSamplingMethods.Contains(SourceInfo.SamplingMethod)
			&& !SourceInfo.SourceIdentity.IsEmpty()
			&& !SourceInfo.AdapterOrProviderId.IsEmpty()
			&& (!SourceInfo.SourceObjectPath.IsEmpty() || !SourceInfo.SourceClass.IsEmpty());
	}

	static const TSet<FString> AllowedKinds = { TEXT("callback"), TEXT("actor"), TEXT("component"), TEXT("subsystem"), TEXT("provider-primary"), TEXT("perception-source") };
	if (!AllowedKinds.Contains(SourceInfo.SourceKind)) { return false; }
	return !SourceInfo.SourceIdentity.IsEmpty() || !SourceInfo.AdapterOrProviderId.IsEmpty() || !SourceInfo.SourceObjectPath.IsEmpty() || !SourceInfo.SourceClass.IsEmpty();
}


bool FAINpcVisualObservationStore::IsVisibleActionOutcomeSourceAllowed(const FAINpcVisualObservationRecord& Record)
{
	if (Record.ValueType != EAINpcVisualObservationValueType::Boolean || !Record.BoolValue) { return false; }
	if (Record.Name != TEXT("actionTargetReached") && Record.Name != TEXT("actionTargetReachedHoldElapsed")) { return false; }
	const FAINpcVisualObservationSourceInfo SourceInfo{ Record.SourceKind, Record.SourceIdentity, Record.SourceObjectPath, Record.SourceClass, Record.SamplingMethod, Record.AdapterOrProviderId };
	return IsFinalSuccessSourceAllowed(SourceInfo);
}
