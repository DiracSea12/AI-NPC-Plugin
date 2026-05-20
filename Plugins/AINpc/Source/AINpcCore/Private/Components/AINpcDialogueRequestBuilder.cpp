#include "Components/AINpcDialogueRequestBuilder.h"

#include "Components/AINpcComponent.h"
#include "AINpcProviderConfigResolver.h"
#include "Components/AINpcSmartObjectPromptHandler.h"
#include "Async/Async.h"
#include "Prompt/PromptBuilder.h"

namespace
{
	constexpr int32 DefaultRequestPromptTokens = 512;
}

FString FAINpcDialogueRequestBuilder::BuildSystemPrompt(const UAINpcComponent& Component)
{
	FPromptBuilderConfig BuilderConfig;
	BuilderConfig.MaxPromptTokens = DefaultRequestPromptTokens;
	BuilderConfig.AvailableSmartObjectTargets = FAINpcSmartObjectPromptHandler::GetAvailableTargets(Component);
	return FPromptBuilder::BuildSystemPrompt(Component.PersonaDataAsset, BuilderConfig);
}

FLLMRequest FAINpcDialogueRequestBuilder::BuildRequest(const UAINpcComponent& Component)
{
	FLLMRequest Request;
	Request.Messages = Component.ConversationHistory;
	FAINpcProviderConfigResolver::ApplyRequestConfig(Component, Request);
	return Request;
}

void FAINpcDialogueRequestBuilder::ConfigureStreamingRequest(UAINpcComponent& Component, FLLMRequest& Request)
{
	Request.bUseStreaming = true;
	Request.StreamCallback = [WeakThis = TWeakObjectPtr<UAINpcComponent>(&Component)](const FLLMStreamChunk& Chunk)
	{
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Chunk]()
		{
			UAINpcComponent* Component = WeakThis.Get();
			if (!Component || !Component->IsDialogueActive())
			{
				return;
			}

			if (Chunk.bIsFinal || Chunk.Content.IsEmpty())
			{
				return;
			}

			Component->OnPartialResponse.Broadcast(Chunk.Content);
			Component->OnDialoguePartialResponseNative().Broadcast(Chunk.Content);
		});
	};
}
