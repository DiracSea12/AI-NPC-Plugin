#pragma once

#include "CoreMinimal.h"
#include "Events/NpcEventSubsystem.h"
#include "AINpcUs2EventRoutingAutomationTests.generated.h"

UCLASS()
class UAINpcUs2EventDispatchRecorder : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void RecordEventStage(const FNpcEventMessage& EventMessage, ENpcEventDispatchStage DispatchStage);

	TArray<FNpcEventMessage> Messages;
	TArray<ENpcEventDispatchStage> Stages;
};
