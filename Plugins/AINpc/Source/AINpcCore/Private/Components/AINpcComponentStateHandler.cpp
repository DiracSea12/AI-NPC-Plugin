#include "Components/AINpcComponentStateHandler.h"

#include "Components/AINpcComponent.h"
#include "Controllers/AINpcController.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformTime.h"
#include "LLM/LLMResponseParser.h"

bool FAINpcComponentStateHandler::TryGetLatestActionIntent(const UAINpcComponent& Component, FNpcAction& OutActionIntent)
{
	for (const FNpcAction& Action : Component.LastParsedResponse.Actions)
	{
		const FString TrimmedActionType = Action.ActionType.TrimStartAndEnd();
		if (!TrimmedActionType.IsEmpty() && !TrimmedActionType.Equals(AINpc::Actions::DefaultTalkActionType, ESearchCase::CaseSensitive))
		{
			OutActionIntent = Action;
			OutActionIntent.ActionType = TrimmedActionType;
			OutActionIntent.Target = OutActionIntent.Target.TrimStartAndEnd();
			return true;
		}
	}

	OutActionIntent = FNpcAction();
	return false;
}

bool FAINpcComponentStateHandler::HasBeenInDialogueStateLongerThan(const UAINpcComponent& Component, const float DurationSeconds)
{
	if (DurationSeconds <= 0.0f)
	{
		return true;
	}

	return (FPlatformTime::Seconds() - Component.DialogueStateEnterTimeSeconds) >= static_cast<double>(DurationSeconds);
}

void FAINpcComponentStateHandler::EnsureControllerBinding(UAINpcComponent& Component)
{
	APawn* PawnOwner = Cast<APawn>(Component.GetOwner());
	if (!PawnOwner)
	{
		return;
	}

	AController* ActiveController = PawnOwner->GetController();

	if (!ActiveController && Component.bAutoCreateNpcController && PawnOwner->HasAuthority())
	{
		PawnOwner->AIControllerClass = AAINpcController::StaticClass();
		PawnOwner->SpawnDefaultController();
		ActiveController = PawnOwner->GetController();
	}

	if (AAINpcController* NpcController = Cast<AAINpcController>(ActiveController))
	{
		NpcController->ConfigureFromComponent(&Component);
	}
}

void FAINpcComponentStateHandler::SetDialogueState(UAINpcComponent& Component, const ENpcDialogueState NewState)
{
	if (Component.CurrentDialogueState == NewState)
	{
		return;
	}

	if (NewState != ENpcDialogueState::WaitingForLLM)
	{
		Component.ClearDelayMaskingTimer();
		Component.EndDelayMasking();
	}

	Component.CurrentDialogueState = NewState;
	Component.DialogueStateEnterTimeSeconds = FPlatformTime::Seconds();
}
