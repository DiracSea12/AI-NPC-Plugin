#pragma once

#include "CoreMinimal.h"

class UAINpcComponent;
enum class ENpcEventDispatchStage : uint8;
struct FNpcEventMessage;

class FAINpcEventRoutingHandler
{
public:
	static void Bind(UAINpcComponent& Component);
	static void Unbind(UAINpcComponent& Component);
	static void HandleStageDispatched(UAINpcComponent& Component, const FNpcEventMessage& EventMessage, ENpcEventDispatchStage DispatchStage);
	static bool ShouldProcess(const UAINpcComponent& Component, const FNpcEventMessage& EventMessage);
	static void ProcessDelayMasking(UAINpcComponent& Component, const FNpcEventMessage& EventMessage);
	static void ProcessEmotionAppraisal(UAINpcComponent& Component, const FNpcEventMessage& EventMessage);
	static void ProcessMemoryWrite(UAINpcComponent& Component, const FNpcEventMessage& EventMessage);
	static void ProcessPromptUpdate(UAINpcComponent& Component, const FNpcEventMessage& EventMessage);
};
