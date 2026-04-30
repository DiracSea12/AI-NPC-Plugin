#pragma once

#include "CoreMinimal.h"

class UAINpcComponent;
class UAnimMontage;
struct FNpcEventMessage;

class FAINpcDelayMaskingHandler
{
public:
	static void Schedule(UAINpcComponent& Component);
	static void ClearTimer(UAINpcComponent& Component);
	static void HandleThresholdReached(UAINpcComponent& Component);
	static void BroadcastStart(UAINpcComponent& Component, UAnimMontage* Montage, const FText& FillerText);
	static void Start(UAINpcComponent& Component);
	static void End(UAINpcComponent& Component);
	static float GetThresholdSeconds(const UAINpcComponent& Component);
	static UAnimMontage* SelectMontage(const UAINpcComponent& Component);
	static UAnimMontage* SelectRandomMontage(const TArray<TSoftObjectPtr<UAnimMontage>>& MontageOptions);
	static UAnimMontage* SelectEventDrivenMontage(const UAINpcComponent& Component, const FNpcEventMessage& EventMessage);
	static bool IsEventRelevantForImmediate(const UAINpcComponent& Component, const FNpcEventMessage& EventMessage);
	static FText SelectFillerText(const UAINpcComponent& Component);
	static void ProcessNpcEvent(UAINpcComponent& Component, const FNpcEventMessage& EventMessage);
};
