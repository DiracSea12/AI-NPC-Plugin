#include "Components/AINpcDialogueFallbackHandler.h"

#include "AINpcCoreLog.h"
#include "Components/AINpcComponent.h"
#include "LLM/LLMReliabilityManager.h"
#include "Settings/AINpcSettings.h"

bool FAINpcDialogueFallbackHandler::TryHandleFailure(UAINpcComponent& Component, const FLLMResponse& Response)
{
	const FString FallbackResponseText = ResolveFallbackResponseText(Component).TrimStartAndEnd();
	if (FallbackResponseText.IsEmpty())
	{
		return false;
	}

	UE_LOG(LogAINpc, Log, TEXT("Timeout fallback using template response, degradation notification sent"));

	Component.SetDialogueState(ENpcDialogueState::Speaking);

	FLLMMessage AssistantMessage;
	AssistantMessage.Role = TEXT("assistant");
	AssistantMessage.Content = FallbackResponseText;
	Component.ConversationHistory.Add(MoveTemp(AssistantMessage));

	const FString FailureReason = ResolveFailureReason(Response);
	Component.OnDialogueDegraded.Broadcast(FallbackResponseText, FailureReason);
	Component.DialogueDegradedNative.Broadcast(FallbackResponseText, FailureReason);
	return true;
}

FString FAINpcDialogueFallbackHandler::ResolveFallbackResponseText(const UAINpcComponent& Component)
{
	return FLLMReliabilityManager::ResolveFallbackResponseText(
		Component.PersonaDataAsset,
		GetDefault<UAINpcSettings>());
}

FString FAINpcDialogueFallbackHandler::ResolveFailureReason(const FLLMResponse& Response)
{
	return Response.ErrorMessage.IsEmpty()
		? TEXT("Dialogue request failed.")
		: Response.ErrorMessage;
}
