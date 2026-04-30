#include "AINpcCoreLog.h"
#include "Components/AINpcComponent.h"
#include "Controllers/AINpcController.h"
#include "Data/NpcPersonaDataAsset.h"
#include "Events/NpcEventPayloadTypes.h"
#include "Events/NpcEventSubsystem.h"
#include "GameplayTagsManager.h"
#include "HAL/Event.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "LLM/OpenAIProvider.h"
#include "LLM/AnthropicProvider.h"
#include "LLM/LLMResponseParser.h"
#include "Misc/AutomationTest.h"
#include "Prompt/PromptBuilder.h"
#include "Settings/AINpcSettings.h"
#include "SmartObjectBridge/SmartObjectBridgeContext.h"
#include "StateTree/Tasks/StateTreeTask_ExecuteSmartObject.h"
#include "StateTree/Tasks/StateTreeTask_LLMQuery.h"
#include "StateTree/Tasks/StateTreeTask_SmartObjectTaskUtils.h"
#include "Animation/AnimMontage.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

#if WITH_EDITOR

namespace
{
class FScopedGlobalApiKeyOverride
{
public:
	explicit FScopedGlobalApiKeyOverride(const FString& InGlobalApiKey)
	{
		UAINpcSettings* Settings = GetMutableDefault<UAINpcSettings>();
		if (Settings)
		{
			OriginalGlobalApiKey = Settings->GlobalApiKey;
			Settings->GlobalApiKey = InGlobalApiKey;
			bHasSettings = true;
		}
	}

	~FScopedGlobalApiKeyOverride()
	{
		if (!bHasSettings)
		{
			return;
		}

		if (UAINpcSettings* Settings = GetMutableDefault<UAINpcSettings>())
		{
			Settings->GlobalApiKey = OriginalGlobalApiKey;
		}
	}

private:
	FString OriginalGlobalApiKey;
	bool bHasSettings = false;
};

class FScopedProviderRequestSettingsOverride
{
public:
	FScopedProviderRequestSettingsOverride(const FString& InGlobalBaseUrl, const FString& InGlobalModel, const float InRequestTimeoutSeconds)
	{
		UAINpcSettings* Settings = GetMutableDefault<UAINpcSettings>();
		if (Settings)
		{
			OriginalGlobalBaseUrl = Settings->GlobalBaseUrl;
			OriginalGlobalModel = Settings->GlobalModel;
			OriginalRequestTimeoutSeconds = Settings->RequestTimeoutSeconds;

			Settings->GlobalBaseUrl = InGlobalBaseUrl;
			Settings->GlobalModel = InGlobalModel;
			Settings->RequestTimeoutSeconds = InRequestTimeoutSeconds;
			bHasSettings = true;
		}
	}

	~FScopedProviderRequestSettingsOverride()
	{
		if (!bHasSettings)
		{
			return;
		}

		if (UAINpcSettings* Settings = GetMutableDefault<UAINpcSettings>())
		{
			Settings->GlobalBaseUrl = OriginalGlobalBaseUrl;
			Settings->GlobalModel = OriginalGlobalModel;
			Settings->RequestTimeoutSeconds = OriginalRequestTimeoutSeconds;
		}
	}

private:
	FString OriginalGlobalBaseUrl;
	FString OriginalGlobalModel;
	float OriginalRequestTimeoutSeconds = 0.0f;
	bool bHasSettings = false;
};

float ComputeRequiredReliabilityBudgetSeconds(const UAINpcSettings* Settings)
{
	if (!Settings)
	{
		return 0.0f;
	}

	const int32 MaxRetryAttempts = FMath::Max(0, Settings->MaxRequestRetries);
	const float RequestTimeoutSeconds = FMath::Max(0.0f, Settings->RequestTimeoutSeconds);
	const float RetryBackoffBaseSeconds = FMath::Max(0.0f, Settings->RetryBackoffBaseSeconds);

	float TotalRetryBackoffSeconds = 0.0f;
	for (int32 RetryIndex = 0; RetryIndex < MaxRetryAttempts; ++RetryIndex)
	{
		TotalRetryBackoffSeconds += RetryBackoffBaseSeconds * FMath::Pow(2.0f, static_cast<float>(RetryIndex));
	}

	return (RequestTimeoutSeconds * static_cast<float>(MaxRetryAttempts + 1)) + TotalRetryBackoffSeconds;
}

class FScopedProviderPreDispatchDelay
{
public:
	explicit FScopedProviderPreDispatchDelay(const float DelaySeconds)
	{
		FOpenAIProvider::SetPreDispatchDelaySecondsForTest(DelaySeconds);
	}

	~FScopedProviderPreDispatchDelay()
	{
		FOpenAIProvider::SetPreDispatchDelaySecondsForTest(0.0f);
	}
};

class FScopedProviderPreProcessDelay
{
public:
	explicit FScopedProviderPreProcessDelay(const float DelaySeconds)
	{
		FOpenAIProvider::SetPreProcessDelaySecondsForTest(DelaySeconds);
	}

	~FScopedProviderPreProcessDelay()
	{
		FOpenAIProvider::SetPreProcessDelaySecondsForTest(0.0f);
	}
};

class FScopedEnvironmentVariableOverride
{
public:
	FScopedEnvironmentVariableOverride(const TCHAR* InName, const TCHAR* InValue)
		: Name(InName)
	{
		PreviousValue = FPlatformMisc::GetEnvironmentVariable(Name);
		FPlatformMisc::SetEnvironmentVar(Name, InValue);
	}

	~FScopedEnvironmentVariableOverride()
	{
		FPlatformMisc::SetEnvironmentVar(Name, *PreviousValue);
	}

private:
	const TCHAR* Name = nullptr;
	FString PreviousValue;
};

class FScopedSmartObjectPromptTargetsOverride
{
public:
	explicit FScopedSmartObjectPromptTargetsOverride(const TArray<FString>& InTargets)
	{
		UAINpcComponent::SetSmartObjectTargetsForPromptForTest(InTargets);
	}

	~FScopedSmartObjectPromptTargetsOverride()
	{
		UAINpcComponent::ClearSmartObjectTargetsForPromptForTest();
	}
};

bool DeserializeJsonObjectForTest(const FString& JsonText, TSharedPtr<FJsonObject>& OutObject)
{
	const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonText);
	return FJsonSerializer::Deserialize(JsonReader, OutObject) && OutObject.IsValid();
}

bool JsonArrayContainsStringValue(const TArray<TSharedPtr<FJsonValue>>* Values, const FString& ExpectedValue)
{
	if (!Values)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		FString ActualValue;
		if (Value.IsValid() && Value->TryGetString(ActualValue) && ActualValue.Equals(ExpectedValue, ESearchCase::CaseSensitive))
		{
			return true;
		}
	}

	return false;
}

bool GetDistinctRoutingTestTags(FGameplayTag& OutMatchTag, FGameplayTag& OutNonMatchTag)
{
	OutMatchTag = FGameplayTag::RequestGameplayTag(FName(TEXT("AINpc.Tests.Route.Match")), false);
	OutNonMatchTag = FGameplayTag::RequestGameplayTag(FName(TEXT("AINpc.Tests.Route.Other")), false);
	if (OutMatchTag.IsValid() && OutNonMatchTag.IsValid() && OutMatchTag != OutNonMatchTag)
	{
		return true;
	}

	FGameplayTagContainer AllTags;
	UGameplayTagsManager::Get().RequestAllGameplayTags(AllTags, true);

	for (const FGameplayTag& Tag : AllTags.GetGameplayTagArray())
	{
		if (!Tag.IsValid())
		{
			continue;
		}

		if (!OutMatchTag.IsValid())
		{
			OutMatchTag = Tag;
			continue;
		}

		if (Tag != OutMatchTag)
		{
			OutNonMatchTag = Tag;
			return true;
		}
	}

	return false;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcStateTreeTimeoutBudgetTest,
	"AINpc.Core.Reliability.StateTreeTimeoutCoversReliabilityBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcStateTreeTimeoutBudgetTest::RunTest(const FString& Parameters)
{
	const UAINpcSettings* Settings = GetDefault<UAINpcSettings>();
	TestNotNull(TEXT("Default AI NPC settings should be available."), Settings);

	const float RequiredBudgetSeconds = ComputeRequiredReliabilityBudgetSeconds(Settings);
	const FStateTreeTask_LLMQueryInstanceData InstanceData;

	TestTrue(
		TEXT("StateTree LLM timeout default should not preempt configured request timeout + retry/backoff budget."),
		InstanceData.TimeoutSeconds >= RequiredBudgetSeconds);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcComponentApiKeyPrecedenceMatrixTest,
	"AINpc.Core.BlueprintIntegration.ComponentApiKeyPrecedenceMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcComponentApiKeyPrecedenceMatrixTest::RunTest(const FString& Parameters)
{
	FScopedEnvironmentVariableOverride ScopedEnvApiKey(TEXT("AINPC_OPENAI_API_KEY"), TEXT(""));

	UAINpcComponent* Component = NewObject<UAINpcComponent>();
	TestNotNull(TEXT("Component instance should be created for API-key precedence test."), Component);
	if (!Component)
	{
		return false;
	}

	UNpcPersonaDataAsset* Persona = NewObject<UNpcPersonaDataAsset>();
	TestNotNull(TEXT("Persona data asset should be created for API-key precedence test."), Persona);
	if (!Persona)
	{
		return false;
	}

	Component->PersonaDataAsset = Persona;

	{
		FScopedGlobalApiKeyOverride ScopedGlobalApiKey(TEXT("  global-key  "));
		Persona->ApiKey = TEXT("");
		Component->ApiKeyOverride = TEXT("");
		TestEqual(
			TEXT("Global key should be used when persona and override are absent."),
			Component->BuildRequestForTest().ApiKey,
			FString(TEXT("global-key")));
	}

	{
		FScopedGlobalApiKeyOverride ScopedGlobalApiKey(TEXT("global-key"));
		Persona->ApiKey = TEXT("  persona-key  ");
		Component->ApiKeyOverride = TEXT("");
		TestEqual(
			TEXT("Persona key should override global key."),
			Component->BuildRequestForTest().ApiKey,
			FString(TEXT("persona-key")));
	}

	{
		FScopedGlobalApiKeyOverride ScopedGlobalApiKey(TEXT("global-key"));
		Persona->ApiKey = TEXT("persona-key");
		Component->ApiKeyOverride = TEXT("  override-key  ");
		TestEqual(
			TEXT("Component override key should override persona and global keys."),
			Component->BuildRequestForTest().ApiKey,
			FString(TEXT("override-key")));
	}

	{
		FScopedGlobalApiKeyOverride ScopedGlobalApiKey(TEXT("global-key"));
		Persona->ApiKey = TEXT("   ");
		Component->ApiKeyOverride = TEXT("");
		TestEqual(
			TEXT("Whitespace persona key should not override global key."),
			Component->BuildRequestForTest().ApiKey,
			FString(TEXT("global-key")));
	}

	{
		FScopedGlobalApiKeyOverride ScopedGlobalApiKey(TEXT("global-key"));
		Persona->ApiKey = TEXT("persona-key");
		Component->ApiKeyOverride = TEXT("   ");
		TestEqual(
			TEXT("Whitespace override key should not override persona key."),
			Component->BuildRequestForTest().ApiKey,
			FString(TEXT("persona-key")));
	}

	{
		FScopedGlobalApiKeyOverride ScopedGlobalApiKey(TEXT("   "));
		Persona->ApiKey = TEXT("   ");
		Component->ApiKeyOverride = TEXT("   ");
		TestTrue(
			TEXT("All-whitespace keys should resolve to an empty request key."),
			Component->BuildRequestForTest().ApiKey.IsEmpty());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcComponentEnvironmentApiKeyFallbackTest,
	"AINpc.Core.Security.ComponentUsesEnvironmentApiKeyFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcComponentEnvironmentApiKeyFallbackTest::RunTest(const FString& Parameters)
{
	FScopedEnvironmentVariableOverride ScopedEnvApiKey(TEXT("AINPC_OPENAI_API_KEY"), TEXT("  env-key  "));
	FScopedGlobalApiKeyOverride ScopedGlobalApiKey(TEXT(""));

	UAINpcComponent* Component = NewObject<UAINpcComponent>();
	TestNotNull(TEXT("Component instance should be created for env-api-key fallback test."), Component);
	if (!Component)
	{
		return false;
	}

	UNpcPersonaDataAsset* Persona = NewObject<UNpcPersonaDataAsset>();
	TestNotNull(TEXT("Persona data asset should be created for env-api-key fallback test."), Persona);
	if (!Persona)
	{
		return false;
	}

	Component->PersonaDataAsset = Persona;
	Persona->ApiKey = TEXT("   ");
	Component->ApiKeyOverride = TEXT("");

	TestEqual(
		TEXT("Environment API key should be used when global/persona/component keys are empty."),
		Component->BuildRequestForTest().ApiKey,
		FString(TEXT("env-key")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcComponentProviderRequestConfigPrecedenceTest,
	"AINpc.Core.Provider.ComponentProviderRequestConfigPrecedence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcComponentProviderRequestConfigPrecedenceTest::RunTest(const FString& Parameters)
{
	FScopedEnvironmentVariableOverride ScopedEnvApiKey(TEXT("AINPC_OPENAI_API_KEY"), TEXT(""));
	FScopedGlobalApiKeyOverride ScopedGlobalApiKey(TEXT(""));
	FScopedProviderRequestSettingsOverride ScopedProviderSettings(
		TEXT("  https://global.example/v1  "),
		TEXT("  global-model  "),
		12.5f);

	UAINpcComponent* Component = NewObject<UAINpcComponent>();
	TestNotNull(TEXT("Component instance should be created for provider request precedence test."), Component);
	if (!Component)
	{
		return false;
	}

	UNpcPersonaDataAsset* Persona = NewObject<UNpcPersonaDataAsset>();
	TestNotNull(TEXT("Persona data asset should be created for provider request precedence test."), Persona);
	if (!Persona)
	{
		return false;
	}

	Component->PersonaDataAsset = Persona;
	Component->BaseUrlOverride = TEXT("");
	Component->ModelOverride = TEXT("");

	{
		Persona->BaseUrl = TEXT("");
		Persona->Model = TEXT("");

		const FLLMRequest Request = Component->BuildRequestForTest();
		TestEqual(TEXT("Global BaseUrl should be used when persona and component overrides are absent."), Request.BaseUrl, FString(TEXT("https://global.example/v1")));
		TestEqual(TEXT("Global model should be used when persona and component overrides are absent."), Request.Model, FString(TEXT("global-model")));
		TestTrue(TEXT("Global timeout should flow into the request."), FMath::IsNearlyEqual(Request.TimeoutSeconds, 12.5f));
	}

	{
		Persona->BaseUrl = TEXT("  https://persona.example/v1  ");
		Persona->Model = TEXT("  persona-model  ");

		const FLLMRequest Request = Component->BuildRequestForTest();
		TestEqual(TEXT("Persona BaseUrl should override global BaseUrl."), Request.BaseUrl, FString(TEXT("https://persona.example/v1")));
		TestEqual(TEXT("Persona model should override global model."), Request.Model, FString(TEXT("persona-model")));
	}

	{
		Persona->BaseUrl = TEXT("https://persona.example/v1");
		Persona->Model = TEXT("persona-model");
		Component->BaseUrlOverride = TEXT("  https://override.example/v1  ");
		Component->ModelOverride = TEXT("  override-model  ");

		const FLLMRequest Request = Component->BuildRequestForTest();
		TestEqual(TEXT("Component BaseUrl override should override persona and global BaseUrl."), Request.BaseUrl, FString(TEXT("https://override.example/v1")));
		TestEqual(TEXT("Component model override should override persona and global model."), Request.Model, FString(TEXT("override-model")));
	}

	{
		FScopedProviderRequestSettingsOverride ScopedNegativeTimeout(
			TEXT("https://global.example/v1"),
			TEXT("global-model"),
			-3.0f);

		Persona->BaseUrl = TEXT("");
		Persona->Model = TEXT("");
		Component->BaseUrlOverride = TEXT("");
		Component->ModelOverride = TEXT("");

		const FLLMRequest Request = Component->BuildRequestForTest();
		TestTrue(TEXT("Negative timeout settings should clamp to zero in the request."), FMath::IsNearlyZero(Request.TimeoutSeconds));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcComponentStartDialogueDelegateFlowTest,
	"AINpc.Core.BlueprintIntegration.ComponentStartDialogueDelegateFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcComponentStartDialogueDelegateFlowTest::RunTest(const FString& Parameters)
{
	UAINpcComponent::ResetConcurrencyStateForTest();
	UAINpcComponent::SetDialogueDispatchBypassForTest(true);

	UAINpcComponent* Component = NewObject<UAINpcComponent>();
	TestNotNull(TEXT("Component instance should be created for Blueprint delegate flow test."), Component);
	if (!Component)
	{
		UAINpcComponent::ResetConcurrencyStateForTest();
		return false;
	}

	Component->ResetDynamicDelegateCountersForTest();
	Component->OnDialogueResponse.AddDynamic(Component, &UAINpcComponent::HandleDialogueResponseDynamicForTest);
	Component->OnDialogueError.AddDynamic(Component, &UAINpcComponent::HandleDialogueErrorDynamicForTest);

	int32 ResponseCount = 0;
	int32 ErrorCount = 0;
	FString LastResponseText;
	Component->OnDialogueResponseNative().AddLambda(
		[&ResponseCount, &LastResponseText](const FString& ResponseText)
		{
			++ResponseCount;
			LastResponseText = ResponseText;
		});
	Component->OnDialogueErrorNative().AddLambda([&ErrorCount](const FString&) { ++ErrorCount; });

	const bool bStarted = Component->StartDialogue(TEXT("Hello from Blueprint flow test."));
	TestTrue(TEXT("StartDialogue should succeed for non-empty player input."), bStarted);
	TestTrue(TEXT("StartDialogue should place request in-flight."), Component->IsRequestInFlight());

	FLLMResponse SuccessResponse;
	SuccessResponse.RequestId = Component->GetActiveRequestIdForTest();
	SuccessResponse.bSuccess = true;
	SuccessResponse.Content = TEXT("Test response from provider.");

	Component->HandleRequestCompletedForTest(SuccessResponse);

	TestEqual(TEXT("Successful completion should emit exactly one dialogue response."), ResponseCount, 1);
	TestEqual(TEXT("Successful completion should not emit dialogue errors."), ErrorCount, 0);
	TestEqual(TEXT("Response text should match callback payload."), LastResponseText, SuccessResponse.Content);
	TestEqual(TEXT("Dynamic response delegate should fire once on success."), Component->GetDynamicDialogueResponseCountForTest(), 1);
	TestEqual(TEXT("Dynamic error delegate should not fire on success."), Component->GetDynamicDialogueErrorCountForTest(), 0);

	const bool bInvalidStart = Component->StartDialogue(TEXT("   "));
	TestFalse(TEXT("StartDialogue should fail for whitespace-only player input."), bInvalidStart);
	TestEqual(TEXT("Invalid StartDialogue input should emit one dialogue error."), ErrorCount, 1);
	TestEqual(TEXT("Dynamic error delegate should fire on invalid input."), Component->GetDynamicDialogueErrorCountForTest(), 1);

	Component->EndDialogue();
	UAINpcComponent::ResetConcurrencyStateForTest();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcComponentFallbackDegradeBroadcastTest,
	"AINpc.Core.Reliability.ComponentFallbackBroadcastsDegradedAndResponse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcComponentFallbackDegradeBroadcastTest::RunTest(const FString& Parameters)
{
	UAINpcComponent* Component = NewObject<UAINpcComponent>();
	TestNotNull(TEXT("Component instance should be created for fallback test."), Component);
	if (!Component)
	{
		return false;
	}

	int32 DegradedCount = 0;
	int32 ResponseCount = 0;
	int32 ErrorCount = 0;
	FString DegradedResponseText;
	FString DegradedFailureReason;

	Component->OnDialogueDegradedNative().AddLambda(
		[&DegradedCount, &DegradedResponseText, &DegradedFailureReason](const FString& FallbackResponse, const FString& FailureReason)
		{
			++DegradedCount;
			DegradedResponseText = FallbackResponse;
			DegradedFailureReason = FailureReason;
		});
	Component->OnDialogueResponseNative().AddLambda([&ResponseCount](const FString&) { ++ResponseCount; });
	Component->OnDialogueErrorNative().AddLambda([&ErrorCount](const FString&) { ++ErrorCount; });

	const FGuid ActiveRequestId = FGuid::NewGuid();
	Component->SetDialogueTestState(true, true, ActiveRequestId, 0, ENpcDialogueState::WaitingForLLM);

	FLLMResponse FailedResponse;
	FailedResponse.RequestId = ActiveRequestId;
	FailedResponse.bSuccess = false;
	FailedResponse.HttpStatusCode = 400;
	FailedResponse.ErrorMessage = TEXT("Synthetic test failure.");

	Component->HandleRequestCompletedForTest(FailedResponse);

	TestEqual(TEXT("Fallback flow should broadcast degraded once."), DegradedCount, 1);
	TestEqual(TEXT("Fallback flow should broadcast dialogue response once."), ResponseCount, 1);
	TestEqual(TEXT("Fallback flow should not emit hard error when fallback exists."), ErrorCount, 0);
	TestTrue(TEXT("Fallback response text should be non-empty."), !DegradedResponseText.IsEmpty());
	TestEqual(TEXT("Failure reason should propagate to degraded delegate."), DegradedFailureReason, FailedResponse.ErrorMessage);
	TestEqual(TEXT("Component should transition to speaking after fallback."), Component->GetDialogueState(), ENpcDialogueState::Speaking);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcComponentRetrySequencingTest,
	"AINpc.Core.Reliability.ComponentRetrySequencingAndBackoff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcComponentRetrySequencingTest::RunTest(const FString& Parameters)
{
	UAINpcComponent* Component = NewObject<UAINpcComponent>();
	TestNotNull(TEXT("Component instance should be created for retry test."), Component);
	if (!Component)
	{
		return false;
	}

	int32 DegradedCount = 0;
	int32 ResponseCount = 0;
	int32 ErrorCount = 0;
	Component->OnDialogueDegradedNative().AddLambda([&DegradedCount](const FString&, const FString&) { ++DegradedCount; });
	Component->OnDialogueResponseNative().AddLambda([&ResponseCount](const FString&) { ++ResponseCount; });
	Component->OnDialogueErrorNative().AddLambda([&ErrorCount](const FString&) { ++ErrorCount; });

	const FGuid ActiveRequestId = FGuid::NewGuid();
	Component->SetDialogueTestState(true, true, ActiveRequestId, 0, ENpcDialogueState::WaitingForLLM);

	FLLMResponse RetryableFailure;
	RetryableFailure.RequestId = ActiveRequestId;
	RetryableFailure.bSuccess = false;
	RetryableFailure.HttpStatusCode = 429;
	RetryableFailure.ErrorMessage = TEXT("Synthetic retryable failure.");

	Component->HandleRequestCompletedForTest(RetryableFailure);


	TestEqual(TEXT("Retry attempt counter should increment after retryable failure."), Component->GetRetryAttemptCountForTest(), 1);
	TestEqual(TEXT("Retry path should keep dialogue state in waiting mode."), Component->GetDialogueState(), ENpcDialogueState::WaitingForLLM);
	TestEqual(TEXT("Retry path should not degrade immediately."), DegradedCount, 0);
	TestEqual(TEXT("Retry path should not emit response immediately."), ResponseCount, 0);
	TestEqual(TEXT("Retry path should not emit hard error immediately."), ErrorCount, 0);

	const UAINpcSettings* Settings = GetDefault<UAINpcSettings>();
	const float BaseBackoff = FMath::Max(0.0f, Settings ? Settings->RetryBackoffBaseSeconds : 0.0f);
	TestTrue(TEXT("Retry delay index 0 should match base backoff."), FMath::IsNearlyEqual(Component->GetRetryDelaySecondsForTest(0), BaseBackoff));
	TestTrue(TEXT("Retry delay index 1 should double base backoff."), FMath::IsNearlyEqual(Component->GetRetryDelaySecondsForTest(1), BaseBackoff * 2.0f));
	TestTrue(TEXT("Retry delay index 2 should quadruple base backoff."), FMath::IsNearlyEqual(Component->GetRetryDelaySecondsForTest(2), BaseBackoff * 4.0f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcComponentLateCallbackIgnoredTest,
	"AINpc.Core.Reliability.ComponentIgnoresLateOrStaleCallbacks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcComponentLateCallbackIgnoredTest::RunTest(const FString& Parameters)
{
	UAINpcComponent* Component = NewObject<UAINpcComponent>();
	TestNotNull(TEXT("Component instance should be created for late-callback test."), Component);
	if (!Component)
	{
		return false;
	}

	int32 DegradedCount = 0;
	int32 ResponseCount = 0;
	int32 ErrorCount = 0;
	Component->OnDialogueDegradedNative().AddLambda([&DegradedCount](const FString&, const FString&) { ++DegradedCount; });
	Component->OnDialogueResponseNative().AddLambda([&ResponseCount](const FString&) { ++ResponseCount; });
	Component->OnDialogueErrorNative().AddLambda([&ErrorCount](const FString&) { ++ErrorCount; });

	Component->SetDialogueTestState(false, false, FGuid(), 0, ENpcDialogueState::Idle);

	FLLMResponse LateResponse;
	LateResponse.RequestId = FGuid::NewGuid();
	LateResponse.bSuccess = true;
	LateResponse.Content = TEXT("Should be ignored.");

	Component->HandleRequestCompletedForTest(LateResponse);

	TestEqual(TEXT("Late callback should not broadcast degraded."), DegradedCount, 0);
	TestEqual(TEXT("Late callback should not broadcast response."), ResponseCount, 0);
	TestEqual(TEXT("Late callback should not broadcast error."), ErrorCount, 0);
	TestEqual(TEXT("Late callback should not alter dialogue state."), Component->GetDialogueState(), ENpcDialogueState::Idle);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcConcurrencyQueueWhenSaturatedTest,
	"AINpc.Core.Concurrency.ComponentQueuesWhenDialoguePoolSaturated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcConcurrencyQueueWhenSaturatedTest::RunTest(const FString& Parameters)
{
	UAINpcComponent::ResetConcurrencyStateForTest();

	const UAINpcSettings* Settings = GetDefault<UAINpcSettings>();
	const int32 DialogueLimit = FMath::Max(1, Settings ? Settings->DialogueRequestConcurrencyLimit : 1);
	UAINpcComponent::SetActiveDialogueRequestSlotsForTest(DialogueLimit);

	UAINpcComponent* Component = NewObject<UAINpcComponent>();
	TestNotNull(TEXT("Component instance should be created for queue saturation test."), Component);
	if (!Component)
	{
		UAINpcComponent::ResetConcurrencyStateForTest();
		return false;
	}

	const bool bStartSucceeded = Component->StartDialogue(TEXT("Queue me because the pool is saturated."));
	TestTrue(TEXT("StartDialogue should succeed and enqueue when the dialogue pool is saturated."), bStartSucceeded);
	TestTrue(TEXT("Component should track that it has a queued request."), Component->HasQueuedDialogueRequestForTest());
	TestEqual(TEXT("Exactly one request should be queued."), UAINpcComponent::GetQueuedDialogueRequestCountForTest(), 1);
	TestFalse(TEXT("Queued request should not be in-flight yet."), Component->IsRequestInFlight());
	TestEqual(TEXT("Queued request should transition to waiting state."), Component->GetDialogueState(), ENpcDialogueState::WaitingForLLM);

	Component->EndDialogue();
	TestEqual(TEXT("EndDialogue should clear queued requests."), UAINpcComponent::GetQueuedDialogueRequestCountForTest(), 0);

	UAINpcComponent::ResetConcurrencyStateForTest();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcConcurrencyPumpQueuedDispatchTest,
	"AINpc.Core.Concurrency.ComponentPumpsQueuedRequestWhenSlotFrees",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcConcurrencyPumpQueuedDispatchTest::RunTest(const FString& Parameters)
{
	UAINpcComponent::ResetConcurrencyStateForTest();
	UAINpcComponent::SetDialogueDispatchBypassForTest(true);

	const UAINpcSettings* Settings = GetDefault<UAINpcSettings>();
	const int32 DialogueLimit = FMath::Max(1, Settings ? Settings->DialogueRequestConcurrencyLimit : 1);
	UAINpcComponent::SetActiveDialogueRequestSlotsForTest(DialogueLimit);

	UAINpcComponent* Component = NewObject<UAINpcComponent>();
	TestNotNull(TEXT("Component instance should be created for queue pump test."), Component);
	if (!Component)
	{
		UAINpcComponent::ResetConcurrencyStateForTest();
		return false;
	}

	const bool bQueueSucceeded = Component->StartDialogue(TEXT("Queue and dispatch when a slot is free."));
	TestTrue(TEXT("StartDialogue should enqueue when pool is saturated."), bQueueSucceeded);
	TestTrue(TEXT("Component should have a queued request before pumping."), Component->HasQueuedDialogueRequestForTest());

	UAINpcComponent::SetActiveDialogueRequestSlotsForTest(DialogueLimit - 1);
	UAINpcComponent::PumpQueuedDialogueRequestsForTest();

	TestFalse(TEXT("Queue token should clear after queued dispatch."), Component->HasQueuedDialogueRequestForTest());
	TestTrue(TEXT("Queued request should become in-flight after pump."), Component->IsRequestInFlight());
	TestEqual(TEXT("Queue should be empty after successful pump."), UAINpcComponent::GetQueuedDialogueRequestCountForTest(), 0);
	TestEqual(TEXT("Global active slot counter should return to the dialogue limit."), UAINpcComponent::GetActiveDialogueRequestSlotsForTest(), DialogueLimit);

	Component->EndDialogue();
	UAINpcComponent::ResetConcurrencyStateForTest();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcConcurrencyDuplicatePendingGuardTest,
	"AINpc.Core.Concurrency.ComponentRejectsDuplicatePendingRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcConcurrencyDuplicatePendingGuardTest::RunTest(const FString& Parameters)
{
	UAINpcComponent::ResetConcurrencyStateForTest();

	const UAINpcSettings* Settings = GetDefault<UAINpcSettings>();
	const int32 DialogueLimit = FMath::Max(1, Settings ? Settings->DialogueRequestConcurrencyLimit : 1);
	UAINpcComponent::SetActiveDialogueRequestSlotsForTest(DialogueLimit);

	UAINpcComponent* Component = NewObject<UAINpcComponent>();
	TestNotNull(TEXT("Component instance should be created for duplicate pending guard test."), Component);
	if (!Component)
	{
		UAINpcComponent::ResetConcurrencyStateForTest();
		return false;
	}

	const bool bFirstSucceeded = Component->StartDialogue(TEXT("First request should enqueue."));
	TestTrue(TEXT("First StartDialogue should succeed."), bFirstSucceeded);
	TestEqual(TEXT("First request should produce one queued entry."), UAINpcComponent::GetQueuedDialogueRequestCountForTest(), 1);

	const bool bSecondSucceeded = Component->StartDialogue(TEXT("Second request should be rejected while first is pending."));
	TestFalse(TEXT("Second StartDialogue should fail while a queued request is pending."), bSecondSucceeded);
	TestEqual(TEXT("Duplicate pending request should not create another queue entry."), UAINpcComponent::GetQueuedDialogueRequestCountForTest(), 1);

	Component->EndDialogue();
	UAINpcComponent::ResetConcurrencyStateForTest();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcMaintenanceConcurrencyQueueWhenSaturatedTest,
	"AINpc.Core.Concurrency.ComponentQueuesWhenMaintenancePoolSaturated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcMaintenanceConcurrencyQueueWhenSaturatedTest::RunTest(const FString& Parameters)
{
	UAINpcComponent::ResetConcurrencyStateForTest();

	const UAINpcSettings* Settings = GetDefault<UAINpcSettings>();
	const int32 MaintenanceLimit = FMath::Max(1, Settings ? Settings->MemoryMaintenanceConcurrencyLimit : 1);
	UAINpcComponent::SetActiveMemoryMaintenanceSlotsForTest(MaintenanceLimit);

	UAINpcComponent* Component = NewObject<UAINpcComponent>();
	TestNotNull(TEXT("Component instance should be created for maintenance queue saturation test."), Component);
	if (!Component)
	{
		UAINpcComponent::ResetConcurrencyStateForTest();
		return false;
	}

	const bool bStarted = Component->TryStartMemoryMaintenance();
	TestFalse(TEXT("Maintenance should queue when the maintenance pool is saturated."), bStarted);
	TestTrue(TEXT("Component should track queued maintenance request token."), Component->HasQueuedMemoryMaintenanceRequestForTest());
	TestEqual(TEXT("Exactly one maintenance request should be queued."), UAINpcComponent::GetQueuedMemoryMaintenanceRequestCountForTest(), 1);
	TestFalse(TEXT("Queued maintenance request should not own a slot yet."), Component->IsMemoryMaintenanceActiveForTest());

	Component->EndMemoryMaintenance();
	TestEqual(TEXT("EndMemoryMaintenance should clear queued maintenance entries."), UAINpcComponent::GetQueuedMemoryMaintenanceRequestCountForTest(), 0);

	UAINpcComponent::ResetConcurrencyStateForTest();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcMaintenanceConcurrencyPumpQueuedRequestTest,
	"AINpc.Core.Concurrency.ComponentPumpsQueuedMaintenanceWhenSlotFrees",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcMaintenanceConcurrencyPumpQueuedRequestTest::RunTest(const FString& Parameters)
{
	UAINpcComponent::ResetConcurrencyStateForTest();

	const UAINpcSettings* Settings = GetDefault<UAINpcSettings>();
	const int32 MaintenanceLimit = FMath::Max(1, Settings ? Settings->MemoryMaintenanceConcurrencyLimit : 1);
	UAINpcComponent::SetActiveMemoryMaintenanceSlotsForTest(MaintenanceLimit);

	UAINpcComponent* Component = NewObject<UAINpcComponent>();
	TestNotNull(TEXT("Component instance should be created for maintenance queue pump test."), Component);
	if (!Component)
	{
		UAINpcComponent::ResetConcurrencyStateForTest();
		return false;
	}

	const bool bStarted = Component->TryStartMemoryMaintenance();
	TestFalse(TEXT("Maintenance should queue when the maintenance pool is saturated."), bStarted);
	TestTrue(TEXT("Component should have queued maintenance before pumping."), Component->HasQueuedMemoryMaintenanceRequestForTest());

	UAINpcComponent::SetActiveMemoryMaintenanceSlotsForTest(MaintenanceLimit - 1);
	UAINpcComponent::PumpQueuedMemoryMaintenanceRequestsForTest();

	TestFalse(TEXT("Queue token should clear after maintenance queue pump."), Component->HasQueuedMemoryMaintenanceRequestForTest());
	TestTrue(TEXT("Queued maintenance request should own a slot after pump."), Component->IsMemoryMaintenanceActiveForTest());
	TestEqual(TEXT("Maintenance queue should be empty after successful pump."), UAINpcComponent::GetQueuedMemoryMaintenanceRequestCountForTest(), 0);
	TestEqual(TEXT("Global maintenance slot counter should return to maintenance limit."), UAINpcComponent::GetActiveMemoryMaintenanceSlotsForTest(), MaintenanceLimit);

	Component->EndMemoryMaintenance();
	TestEqual(TEXT("Releasing maintenance slot should decrement active count."), UAINpcComponent::GetActiveMemoryMaintenanceSlotsForTest(), MaintenanceLimit - 1);

	UAINpcComponent::ResetConcurrencyStateForTest();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOpenAIProviderPreDispatchCancelTest,
	"AINpc.Core.Reliability.OpenAIProviderCancelsBeforeDispatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOpenAIProviderPreDispatchCancelTest::RunTest(const FString& Parameters)
{
	FScopedProviderPreDispatchDelay ScopedDispatchDelay(0.15f);

	TSharedRef<FOpenAIProvider, ESPMode::ThreadSafe> Provider =
		MakeShared<FOpenAIProvider, ESPMode::ThreadSafe>(TEXT("test-key"), TEXT("gpt-4o-mini"), TEXT("https://127.0.0.1:9"));

	FLLMRequest Request;
	FLLMMessage Message;
	Message.Role = TEXT("user");
	Message.Content = TEXT("cancel-me");
	Request.Messages.Add(MoveTemp(Message));

	TAtomic<bool> bCallbackCalled(false);
	TAtomic<bool> bCallbackReportedCancelled(false);
	TAtomic<bool> bReentrantCancelAttempted(false);
	TAtomic<bool> bReentrantCancelReturnedFalse(false);
	FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool(false);

	const FGuid RequestId = Provider->SendRequest(
		Request,
		[Provider, &bCallbackCalled, &bCallbackReportedCancelled, &bReentrantCancelAttempted, &bReentrantCancelReturnedFalse, CompletionEvent](const FLLMResponse& Response)
		{
			bCallbackCalled = true;
			if (Response.ErrorMessage.Contains(TEXT("cancel"), ESearchCase::IgnoreCase))
			{
				bCallbackReportedCancelled = true;
			}

			bReentrantCancelAttempted = true;
			bReentrantCancelReturnedFalse = !Provider->CancelRequest(Response.RequestId);

			CompletionEvent->Trigger();
		});

	const bool bCancelled = Provider->CancelRequest(RequestId);
	TestTrue(TEXT("CancelRequest should succeed for request cancelled before dispatch registration."), bCancelled);

	const bool bCompletedInTime = CompletionEvent->Wait(2000);
	TestTrue(TEXT("Completion callback should still execute for cancelled pre-dispatch request."), bCompletedInTime);
	TestTrue(TEXT("Completion callback should indicate cancellation."), bCallbackReportedCancelled.Load());
	TestTrue(TEXT("Completion callback should be invoked exactly once for cancel path."), bCallbackCalled.Load());
	TestTrue(TEXT("Cancellation callback should tolerate re-entrant CancelRequest calls without deadlock."), bReentrantCancelAttempted.Load());
	TestTrue(TEXT("Re-entrant cancellation after callback dispatch should report no pending request to cancel."), bReentrantCancelReturnedFalse.Load());

	FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOpenAIProviderMidDispatchCancelRaceTest,
	"AINpc.Core.Reliability.OpenAIProviderCancelsAfterInitialGateBeforeSend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOpenAIProviderMidDispatchCancelRaceTest::RunTest(const FString& Parameters)
{
	FScopedProviderPreProcessDelay ScopedPreProcessDelay(0.2f);

	TSharedRef<FOpenAIProvider, ESPMode::ThreadSafe> Provider =
		MakeShared<FOpenAIProvider, ESPMode::ThreadSafe>(TEXT("test-key"), TEXT("gpt-4o-mini"), TEXT("https://127.0.0.1:9"));

	FLLMRequest Request;
	FLLMMessage Message;
	Message.Role = TEXT("user");
	Message.Content = TEXT("cancel-me-mid-dispatch");
	Request.Messages.Add(MoveTemp(Message));

	TAtomic<bool> bCallbackCalled(false);
	TAtomic<bool> bCallbackReportedCancelled(false);
	FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool(false);

	const FGuid RequestId = Provider->SendRequest(
		Request,
		[&bCallbackCalled, &bCallbackReportedCancelled, CompletionEvent](const FLLMResponse& Response)
		{
			bCallbackCalled = true;
			if (Response.ErrorMessage.Contains(TEXT("cancel"), ESearchCase::IgnoreCase))
			{
				bCallbackReportedCancelled = true;
			}
			CompletionEvent->Trigger();
		});

	bool bReachedPostInitialCancelGate = false;
	for (int32 Attempt = 0; Attempt < 200; ++Attempt)
	{
		if (FOpenAIProvider::HasReachedPostInitialCancelGateForTest())
		{
			bReachedPostInitialCancelGate = true;
			break;
		}

		FPlatformProcess::SleepNoStats(0.005f);
	}

	TestTrue(TEXT("Dispatch should reach the post-initial cancel gate before cancellation in this race test."), bReachedPostInitialCancelGate);
	const bool bCancelled = Provider->CancelRequest(RequestId);
	TestTrue(TEXT("CancelRequest should succeed in the mid-dispatch race window."), bCancelled);

	const bool bCompletedInTime = CompletionEvent->Wait(2000);
	TestTrue(TEXT("Completion callback should execute for mid-dispatch cancellation."), bCompletedInTime);
	TestTrue(TEXT("Completion callback should indicate cancellation in the race window."), bCallbackReportedCancelled.Load());
	TestTrue(TEXT("Completion callback should be invoked exactly once for race cancel path."), bCallbackCalled.Load());

	FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcOpenAIProviderStructuredOutputContractTest,
	"AINpc.Core.Reliability.OpenAIProviderRequestsStructuredOutputContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcOpenAIProviderStructuredOutputContractTest::RunTest(const FString& Parameters)
{
	TSharedRef<FOpenAIProvider, ESPMode::ThreadSafe> Provider =
		MakeShared<FOpenAIProvider, ESPMode::ThreadSafe>(TEXT("test-key"), TEXT("gpt-4o-mini"), TEXT("https://example.test"));

	const FLLMProviderCapabilities Capabilities = Provider->GetCapabilities();
	TestTrue(TEXT("OpenAI provider should report function-calling capability for structured output contract."), Capabilities.bSupportsFunctionCalling);
	TestTrue(TEXT("OpenAI provider should report JSON-mode capability for structured output contract."), Capabilities.bSupportsJsonMode);

	FLLMRequest Request;
	Request.Temperature = 0.2f;
	Request.MaxTokens = 128;
	FLLMMessage UserMessage;
	UserMessage.Role = TEXT("user");
	UserMessage.Content = TEXT("Respond with structured NPC output.");
	Request.Messages.Add(MoveTemp(UserMessage));

	const FString RequestBody = Provider->BuildRequestBodyForTest(Request);
	TSharedPtr<FJsonObject> RequestJson;
	const bool bParsedRequestJson = DeserializeJsonObjectForTest(RequestBody, RequestJson);
	TestTrue(TEXT("Provider request payload should deserialize as JSON."), bParsedRequestJson);
	if (!bParsedRequestJson || !RequestJson.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* ToolsArray = nullptr;
	TestTrue(TEXT("Provider request should include tools array for function-calling tier preference."), RequestJson->TryGetArrayField(TEXT("tools"), ToolsArray) && ToolsArray);
	TestTrue(TEXT("Provider request should include at least one tool definition."), ToolsArray && ToolsArray->Num() > 0);

	const TSharedPtr<FJsonObject> FirstToolObject =
		(ToolsArray && ToolsArray->Num() > 0 && (*ToolsArray)[0].IsValid())
			? (*ToolsArray)[0]->AsObject()
			: nullptr;
	TestTrue(TEXT("First tool definition should be a valid object."), FirstToolObject.IsValid());
	if (!FirstToolObject.IsValid())
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* FunctionObject = nullptr;
	TestTrue(TEXT("Tool definition should include a function object."), FirstToolObject->TryGetObjectField(TEXT("function"), FunctionObject) && FunctionObject && FunctionObject->IsValid());
	if (!FunctionObject || !FunctionObject->IsValid())
	{
		return false;
	}

	FString FunctionName;
	(*FunctionObject)->TryGetStringField(TEXT("name"), FunctionName);
	TestEqual(TEXT("Structured-output function name should match provider contract."), FunctionName, FString(TEXT("emit_npc_response")));

	const TSharedPtr<FJsonObject>* ParametersObject = nullptr;
	TestTrue(TEXT("Tool function should include JSON parameters schema."), (*FunctionObject)->TryGetObjectField(TEXT("parameters"), ParametersObject) && ParametersObject && ParametersObject->IsValid());
	if (!ParametersObject || !ParametersObject->IsValid())
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	TestTrue(TEXT("Tool parameters schema should include properties object."), (*ParametersObject)->TryGetObjectField(TEXT("properties"), PropertiesObject) && PropertiesObject && PropertiesObject->IsValid());
	if (!PropertiesObject || !PropertiesObject->IsValid())
	{
		return false;
	}

	bool bParametersAdditionalProperties = true;
	const bool bHasParametersAdditionalProperties = (*ParametersObject)->TryGetBoolField(TEXT("additionalProperties"), bParametersAdditionalProperties);
	TestTrue(TEXT("Structured-output schema should explicitly declare additionalProperties on root parameters object."), bHasParametersAdditionalProperties);
	TestFalse(TEXT("Structured-output root parameters object should disallow extra top-level keys."), bParametersAdditionalProperties);

	const TArray<TSharedPtr<FJsonValue>>* RequiredFields = nullptr;
	const bool bHasRequiredFields = (*ParametersObject)->TryGetArrayField(TEXT("required"), RequiredFields);
	TestTrue(TEXT("Structured-output schema should include required top-level keys list."), bHasRequiredFields && RequiredFields);
	TestTrue(TEXT("Structured-output schema should require dialogue key."), JsonArrayContainsStringValue(RequiredFields, TEXT("dialogue")));
	TestTrue(TEXT("Structured-output schema should require actions key."), JsonArrayContainsStringValue(RequiredFields, TEXT("actions")));
	TestTrue(TEXT("Structured-output schema should require emotion_delta key."), JsonArrayContainsStringValue(RequiredFields, TEXT("emotion_delta")));
	TestTrue(TEXT("Structured-output schema should require relationship_delta key."), JsonArrayContainsStringValue(RequiredFields, TEXT("relationship_delta")));

	TestTrue(TEXT("Structured-output schema should declare dialogue field."), (*PropertiesObject)->HasField(TEXT("dialogue")));
	TestTrue(TEXT("Structured-output schema should declare actions field."), (*PropertiesObject)->HasField(TEXT("actions")));
	TestTrue(TEXT("Structured-output schema should declare emotion_delta field."), (*PropertiesObject)->HasField(TEXT("emotion_delta")));
	TestTrue(TEXT("Structured-output schema should declare relationship_delta field."), (*PropertiesObject)->HasField(TEXT("relationship_delta")));

	const TSharedPtr<FJsonObject>* ActionsSchemaObject = nullptr;
	TestTrue(TEXT("Structured-output schema should include actions array schema."), (*PropertiesObject)->TryGetObjectField(TEXT("actions"), ActionsSchemaObject) && ActionsSchemaObject && ActionsSchemaObject->IsValid());
	const TSharedPtr<FJsonObject>* ActionItemSchemaObject = nullptr;
	TestTrue(TEXT("Actions schema should include items object schema."), ActionsSchemaObject && (*ActionsSchemaObject)->TryGetObjectField(TEXT("items"), ActionItemSchemaObject) && ActionItemSchemaObject && ActionItemSchemaObject->IsValid());
	bool bActionItemAdditionalProperties = true;
	const bool bHasActionItemAdditionalProperties =
		ActionItemSchemaObject && (*ActionItemSchemaObject)->TryGetBoolField(TEXT("additionalProperties"), bActionItemAdditionalProperties);
	TestTrue(TEXT("Action item schema should explicitly declare additionalProperties."), bHasActionItemAdditionalProperties);
	TestFalse(TEXT("Action item schema should disallow extra keys."), bActionItemAdditionalProperties);

	const TSharedPtr<FJsonObject>* EmotionDeltaSchemaObject = nullptr;
	TestTrue(TEXT("Structured-output schema should include emotion_delta object schema."), (*PropertiesObject)->TryGetObjectField(TEXT("emotion_delta"), EmotionDeltaSchemaObject) && EmotionDeltaSchemaObject && EmotionDeltaSchemaObject->IsValid());
	bool bEmotionAdditionalProperties = true;
	const bool bHasEmotionAdditionalProperties =
		EmotionDeltaSchemaObject && (*EmotionDeltaSchemaObject)->TryGetBoolField(TEXT("additionalProperties"), bEmotionAdditionalProperties);
	TestTrue(TEXT("emotion_delta schema should explicitly declare additionalProperties."), bHasEmotionAdditionalProperties);
	TestFalse(TEXT("emotion_delta schema should disallow extra keys."), bEmotionAdditionalProperties);

	const TSharedPtr<FJsonObject>* RelationshipDeltaSchemaObject = nullptr;
	TestTrue(TEXT("Structured-output schema should include relationship_delta object schema."), (*PropertiesObject)->TryGetObjectField(TEXT("relationship_delta"), RelationshipDeltaSchemaObject) && RelationshipDeltaSchemaObject && RelationshipDeltaSchemaObject->IsValid());
	bool bRelationshipAdditionalProperties = true;
	const bool bHasRelationshipAdditionalProperties =
		RelationshipDeltaSchemaObject && (*RelationshipDeltaSchemaObject)->TryGetBoolField(TEXT("additionalProperties"), bRelationshipAdditionalProperties);
	TestTrue(TEXT("relationship_delta schema should explicitly declare additionalProperties."), bHasRelationshipAdditionalProperties);
	TestFalse(TEXT("relationship_delta schema should disallow extra keys."), bRelationshipAdditionalProperties);

	const TSharedPtr<FJsonObject>* ToolChoiceObject = nullptr;
	TestTrue(TEXT("Provider request should include explicit tool_choice object."), RequestJson->TryGetObjectField(TEXT("tool_choice"), ToolChoiceObject) && ToolChoiceObject && ToolChoiceObject->IsValid());
	if (!ToolChoiceObject || !ToolChoiceObject->IsValid())
	{
		return false;
	}

	FString ToolChoiceType;
	(*ToolChoiceObject)->TryGetStringField(TEXT("type"), ToolChoiceType);
	TestEqual(TEXT("tool_choice should force function selection."), ToolChoiceType, FString(TEXT("function")));

	const TSharedPtr<FJsonObject>* ToolChoiceFunctionObject = nullptr;
	TestTrue(TEXT("tool_choice should include nested function selector."), (*ToolChoiceObject)->TryGetObjectField(TEXT("function"), ToolChoiceFunctionObject) && ToolChoiceFunctionObject && ToolChoiceFunctionObject->IsValid());
	if (!ToolChoiceFunctionObject || !ToolChoiceFunctionObject->IsValid())
	{
		return false;
	}

	FString ToolChoiceFunctionName;
	(*ToolChoiceFunctionObject)->TryGetStringField(TEXT("name"), ToolChoiceFunctionName);
	TestEqual(TEXT("tool_choice function name should match structured-output function."), ToolChoiceFunctionName, FString(TEXT("emit_npc_response")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcPromptBuilderStructuredOutputConstraintsTest,
	"AINpc.Core.Prompt.StructuredOutputConstraints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcPromptBuilderStructuredOutputConstraintsTest::RunTest(const FString& Parameters)
{
	UNpcPersonaDataAsset* Persona = NewObject<UNpcPersonaDataAsset>();
	TestNotNull(TEXT("Persona data asset should be created for prompt constraints test."), Persona);
	if (!Persona)
	{
		return false;
	}

	Persona->SpeakingLength = ENpcSpeakingLength::Short;
	const FString Prompt = FPromptBuilder::BuildSystemPrompt(Persona);

	TestTrue(TEXT("Prompt should include sentence-length guidance from speaking-length policy."), Prompt.Contains(TEXT("Use 1 to 2 sentences.")));
	TestTrue(TEXT("Prompt should require JSON-only fallback output."), Prompt.Contains(TEXT("return ONLY one valid JSON object"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Prompt should require dialogue key in JSON output contract."), Prompt.Contains(TEXT("\"dialogue\"")));
	TestTrue(TEXT("Prompt should require actions key in JSON output contract."), Prompt.Contains(TEXT("\"actions\"")));
	TestTrue(TEXT("Prompt should require emotion_delta key in JSON output contract."), Prompt.Contains(TEXT("\"emotion_delta\"")));
	TestTrue(TEXT("Prompt should require relationship_delta key in JSON output contract."), Prompt.Contains(TEXT("\"relationship_delta\"")));
	TestTrue(TEXT("Prompt should prohibit extra keys in fallback JSON mode."), Prompt.Contains(TEXT("or extra keys")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcPromptBuilderSmartObjectContextInjectionTest,
	"AINpc.Core.Prompt.SmartObjectContextInjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcPromptBuilderSmartObjectContextInjectionTest::RunTest(const FString& Parameters)
{
	UNpcPersonaDataAsset* Persona = NewObject<UNpcPersonaDataAsset>();
	TestNotNull(TEXT("Persona data asset should be created for SmartObject prompt context test."), Persona);
	if (!Persona)
	{
		return false;
	}

	FPromptBuilderConfig PromptConfig;
	PromptConfig.AvailableSmartObjectTargets = {TEXT("SO_SLOT_Chair"), TEXT("SO_SLOT_Cup"), TEXT("SO_SLOT_Sword")};

	const FString Prompt = FPromptBuilder::BuildSystemPrompt(Persona, PromptConfig);

	TestTrue(TEXT("Prompt should include nearby SmartObject slot-id whitelist for legal SmartObject targets."), Prompt.Contains(TEXT("Available SmartObject slot IDs near NPC (legal SmartObject targets): [SO_SLOT_Chair, SO_SLOT_Cup, SO_SLOT_Sword]")));
	TestTrue(TEXT("Prompt should only require target binding for SmartObject interaction actions."), Prompt.Contains(TEXT("For actions that require SmartObject interaction, set \"target\" to one slot ID from this list.")));
	TestTrue(TEXT("Prompt should allow targetless actions when SmartObject interaction is not required."), Prompt.Contains(TEXT("For actions that do not require SmartObject interaction, \"target\" may be omitted or empty.")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcComponentPromptSmartObjectTargetsOverrideTest,
	"AINpc.Core.SmartObject.ComponentPromptTargetsUsesRuntimeOverrideForTest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcComponentPromptSmartObjectTargetsOverrideTest::RunTest(const FString& Parameters)
{
	UAINpcComponent* Component = NewObject<UAINpcComponent>();
	TestNotNull(TEXT("Component instance should be created for SmartObject target override test."), Component);
	if (!Component)
	{
		return false;
	}

	UAINpcComponent::ClearSmartObjectTargetsForPromptForTest();
	const TArray<FString> TargetsWithoutOverride = Component->GetAvailableSmartObjectTargetsForPromptForTest();
	TestTrue(TEXT("Without SmartObject world context and without override, prompt targets should be empty."), TargetsWithoutOverride.IsEmpty());

	const TArray<FString> OverrideTargets = {TEXT("SO_SLOT_03"), TEXT("SO_SLOT_01"), TEXT("SO_SLOT_02")};
	{
		FScopedSmartObjectPromptTargetsOverride ScopedTargetsOverride(OverrideTargets);
		const TArray<FString> TargetsWithOverride = Component->GetAvailableSmartObjectTargetsForPromptForTest();
		const TArray<FString> ExpectedSortedTargets = {TEXT("SO_SLOT_01"), TEXT("SO_SLOT_02"), TEXT("SO_SLOT_03")};
		TestEqual(TEXT("SmartObject target override should be surfaced via component prompt target query in sorted order."), TargetsWithOverride, ExpectedSortedTargets);
	}

	const TArray<FString> TargetsAfterOverrideReset = Component->GetAvailableSmartObjectTargetsForPromptForTest();
	TestTrue(TEXT("After clearing override, prompt targets should return to runtime query behavior."), TargetsAfterOverrideReset.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcResponseParserFunctionCallingTierTest,
	"AINpc.Core.Parser.FunctionCallingTier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcResponseParserFunctionCallingTierTest::RunTest(const FString& Parameters)
{
	const FString ResponseBody = TEXT(R"({"choices":[{"message":{"content":"I will do that now.","tool_calls":[{"id":"call_1","type":"function","function":{"name":"Action.Inspect","arguments":"{\"dialogue\":\"Let me inspect this carefully.\",\"target\":\"artifact_01\"}"}}]}}]})");
	FParsedLLMResponse ParsedResponse;
	FString ErrorMessage;

	const bool bParsed = FLLMResponseParser::ParseOpenAIChatCompletion(ResponseBody, ParsedResponse, ErrorMessage);
	TestTrue(TEXT("Function-calling tier should parse successfully."), bParsed);
	TestTrue(TEXT("Function-calling tier should not report parser errors."), ErrorMessage.IsEmpty());
	TestEqual(TEXT("Function-calling tier should be selected first."), ParsedResponse.ParseTier, ELLMResponseParseTier::FunctionCalling);
	TestEqual(TEXT("Function-calling parser should extract dialogue text."), ParsedResponse.Dialogue, FString(TEXT("Let me inspect this carefully.")));
	TestTrue(TEXT("Function-calling parser should extract action intent."), ParsedResponse.Actions.Num() > 0);
	if (ParsedResponse.Actions.Num() > 0)
	{
		TestEqual(TEXT("Function-calling parser should map function name to action type."), ParsedResponse.Actions[0].ActionType, FString(TEXT("Action.Inspect")));
		TestEqual(TEXT("Function-calling parser should preserve action target from arguments."), ParsedResponse.Actions[0].Target, FString(TEXT("artifact_01")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcResponseParserFunctionCallingStructuredOutputNoActionTest,
	"AINpc.Core.Parser.FunctionCallingTierStructuredOutputAllowsEmptyActions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcResponseParserFunctionCallingStructuredOutputNoActionTest::RunTest(const FString& Parameters)
{
	const FString ResponseBody = TEXT(R"({"choices":[{"message":{"content":"I can reply without a gameplay action.","tool_calls":[{"id":"call_1","type":"function","function":{"name":"emit_npc_response","arguments":"{\"dialogue\":\"I can reply without a gameplay action.\",\"actions\":[],\"emotion_delta\":{\"valence\":0.0,\"arousal\":0.0,\"dominance\":0.0},\"relationship_delta\":{\"affinity\":0.0,\"trust\":0.0,\"familiarity\":0.0}}"}}]}}]})");
	FParsedLLMResponse ParsedResponse;
	FString ErrorMessage;

	const bool bParsed = FLLMResponseParser::ParseOpenAIChatCompletion(ResponseBody, ParsedResponse, ErrorMessage);
	TestTrue(TEXT("Function-calling tier should parse structured-output function payloads with empty actions."), bParsed);
	TestTrue(TEXT("Structured-output function payload with empty actions should not report parser errors."), ErrorMessage.IsEmpty());
	TestEqual(TEXT("Function-calling tier should remain selected for structured-output function payloads."), ParsedResponse.ParseTier, ELLMResponseParseTier::FunctionCalling);
	TestEqual(TEXT("Structured-output function payload should preserve dialogue text."), ParsedResponse.Dialogue, FString(TEXT("I can reply without a gameplay action.")));
	TestEqual(TEXT("Structured-output function payload should keep actions empty when no action is needed."), ParsedResponse.Actions.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcResponseParserFunctionCallingMalformedArgumentsTest,
	"AINpc.Core.Parser.FunctionCallingTierMalformedArgumentsFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcResponseParserFunctionCallingMalformedArgumentsTest::RunTest(const FString& Parameters)
{
	const FString ResponseBody = TEXT(R"({"choices":[{"message":{"content":"I can handle this.","tool_calls":[{"id":"call_1","type":"function","function":{"name":"Action.Warn","arguments":"{\"dialogue\":\"broken-json\""}}]}}]})");
	FParsedLLMResponse ParsedResponse;
	FString ErrorMessage;

	const bool bParsed = FLLMResponseParser::ParseOpenAIChatCompletion(ResponseBody, ParsedResponse, ErrorMessage);
	TestTrue(TEXT("Function-calling tier should still parse when tool arguments are malformed."), bParsed);
	TestTrue(TEXT("Malformed function arguments should not emit parser errors when function metadata is usable."), ErrorMessage.IsEmpty());
	TestEqual(TEXT("Function-calling tier should remain selected for malformed arguments fallback path."), ParsedResponse.ParseTier, ELLMResponseParseTier::FunctionCalling);
	TestEqual(TEXT("Malformed function arguments should fall back to message content for dialogue."), ParsedResponse.Dialogue, FString(TEXT("I can handle this.")));
	TestTrue(TEXT("Function name should still be promoted to action intent for malformed arguments."), ParsedResponse.Actions.Num() > 0);
	if (ParsedResponse.Actions.Num() > 0)
	{
		TestEqual(TEXT("Malformed function arguments fallback should preserve function name as action type."), ParsedResponse.Actions[0].ActionType, FString(TEXT("Action.Warn")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcResponseParserFunctionCallingStructuredOutputRejectsMissingRequiredKeysTest,
	"AINpc.Core.Parser.FunctionCallingTierStructuredOutputRejectsMissingRequiredKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcResponseParserFunctionCallingStructuredOutputRejectsMissingRequiredKeysTest::RunTest(const FString& Parameters)
{
	const FString ResponseBody = TEXT(R"({"choices":[{"message":{"content":"I can reply without full structured payload.","tool_calls":[{"id":"call_1","type":"function","function":{"name":"emit_npc_response","arguments":"{\"dialogue\":\"I can reply without full structured payload.\",\"actions\":[],\"emotion_delta\":{\"valence\":0.0,\"arousal\":0.0,\"dominance\":0.0}}"}}]}}]})");
	FParsedLLMResponse ParsedResponse;
	FString ErrorMessage;

	const bool bParsed = FLLMResponseParser::ParseOpenAIChatCompletion(ResponseBody, ParsedResponse, ErrorMessage);
	TestTrue(TEXT("Structured-output function payload missing required keys should downgrade instead of passing FunctionCalling tier."), bParsed);
	TestTrue(TEXT("Structured-output function payload downgrade should not emit parser errors."), ErrorMessage.IsEmpty());
	TestEqual(TEXT("Missing required structured-output keys in function arguments should bypass FunctionCalling tier and fall back to plain text."), ParsedResponse.ParseTier, ELLMResponseParseTier::PlainText);
	TestEqual(TEXT("Downgraded structured-output function payload should use message content as dialogue."), ParsedResponse.Dialogue, FString(TEXT("I can reply without full structured payload.")));
	TestTrue(TEXT("Downgraded structured-output function payload should still provide fallback action intent."), ParsedResponse.Actions.Num() > 0);
	if (ParsedResponse.Actions.Num() > 0)
	{
		TestEqual(TEXT("Downgraded structured-output function payload should use default plain-text action."), ParsedResponse.Actions[0].ActionType, FString(TEXT("Action.DefaultTalk")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcResponseParserStrictJsonTierTest,
	"AINpc.Core.Parser.StrictJsonSchemaTier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcResponseParserStrictJsonTierTest::RunTest(const FString& Parameters)
{
	const FString ResponseBody = TEXT(R"({"choices":[{"message":{"content":"{\"dialogue\":\"Welcome, traveler.\",\"actions\":[{\"type\":\"Action.Wave\",\"target\":\"player\"}],\"emotion_delta\":{\"valence\":0.2,\"arousal\":0.1,\"dominance\":0.0},\"relationship_delta\":{\"affinity\":1.0,\"trust\":0.5,\"familiarity\":0.25}}"}}]})");
	FParsedLLMResponse ParsedResponse;
	FString ErrorMessage;

	const bool bParsed = FLLMResponseParser::ParseOpenAIChatCompletion(ResponseBody, ParsedResponse, ErrorMessage);
	TestTrue(TEXT("Strict JSON-schema tier should parse successfully."), bParsed);
	TestTrue(TEXT("Strict JSON-schema tier should not report parser errors."), ErrorMessage.IsEmpty());
	TestEqual(TEXT("Strict JSON-schema tier should be selected when schema matches."), ParsedResponse.ParseTier, ELLMResponseParseTier::StrictJsonSchema);
	TestEqual(TEXT("Strict JSON-schema tier should extract dialogue text."), ParsedResponse.Dialogue, FString(TEXT("Welcome, traveler.")));
	TestEqual(TEXT("Strict JSON-schema tier should extract one action intent."), ParsedResponse.Actions.Num(), 1);
	if (ParsedResponse.Actions.Num() == 1)
	{
		TestEqual(TEXT("Strict JSON-schema tier action type should match payload."), ParsedResponse.Actions[0].ActionType, FString(TEXT("Action.Wave")));
		TestEqual(TEXT("Strict JSON-schema tier action target should match payload."), ParsedResponse.Actions[0].Target, FString(TEXT("player")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcResponseParserStrictJsonTierRejectsMissingEmotionDeltaTest,
	"AINpc.Core.Parser.StrictJsonSchemaTierRejectsMissingEmotionDelta",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcResponseParserStrictJsonTierRejectsMissingEmotionDeltaTest::RunTest(const FString& Parameters)
{
	const FString ResponseBody = TEXT(R"({"choices":[{"message":{"content":"{\"dialogue\":\"Fallback to loose tier when emotion delta is missing.\",\"actions\":[{\"type\":\"Action.Wave\",\"target\":\"player\"}],\"relationship_delta\":{\"affinity\":1.0,\"trust\":0.5,\"familiarity\":0.25}}"}}]})");
	FParsedLLMResponse ParsedResponse;
	FString ErrorMessage;

	const bool bParsed = FLLMResponseParser::ParseOpenAIChatCompletion(ResponseBody, ParsedResponse, ErrorMessage);
	TestTrue(TEXT("Parser should still succeed when strict payload misses emotion_delta by downgrading to loose tier."), bParsed);
	TestTrue(TEXT("Strict-to-loose downgrade should not emit parser errors."), ErrorMessage.IsEmpty());
	TestEqual(TEXT("Missing emotion_delta should prevent StrictJsonSchema tier and downgrade to LooseExtraction."), ParsedResponse.ParseTier, ELLMResponseParseTier::LooseExtraction);
	TestEqual(TEXT("Loose downgrade should preserve dialogue text."), ParsedResponse.Dialogue, FString(TEXT("Fallback to loose tier when emotion delta is missing.")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcResponseParserStrictJsonTierRejectsExtraTopLevelKeysTest,
	"AINpc.Core.Parser.StrictJsonSchemaTierRejectsExtraTopLevelKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcResponseParserStrictJsonTierRejectsExtraTopLevelKeysTest::RunTest(const FString& Parameters)
{
	const FString ResponseBody = TEXT(R"({"choices":[{"message":{"content":"{\"dialogue\":\"Fallback to loose tier when extra keys exist.\",\"actions\":[{\"type\":\"Action.Wave\",\"target\":\"player\"}],\"emotion_delta\":{\"valence\":0.2,\"arousal\":0.1,\"dominance\":0.0},\"relationship_delta\":{\"affinity\":1.0,\"trust\":0.5,\"familiarity\":0.25},\"extra_field\":\"unexpected\"}"}}]})");
	FParsedLLMResponse ParsedResponse;
	FString ErrorMessage;

	const bool bParsed = FLLMResponseParser::ParseOpenAIChatCompletion(ResponseBody, ParsedResponse, ErrorMessage);
	TestTrue(TEXT("Parser should still succeed when strict payload has extra keys by downgrading to loose tier."), bParsed);
	TestTrue(TEXT("Strict extra-key downgrade should not emit parser errors."), ErrorMessage.IsEmpty());
	TestEqual(TEXT("Extra top-level keys should prevent StrictJsonSchema tier and downgrade to LooseExtraction."), ParsedResponse.ParseTier, ELLMResponseParseTier::LooseExtraction);
	TestEqual(TEXT("Loose downgrade for extra keys should preserve dialogue text."), ParsedResponse.Dialogue, FString(TEXT("Fallback to loose tier when extra keys exist.")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcResponseParserStrictJsonTierAllowsEmptyActionsTest,
	"AINpc.Core.Parser.StrictJsonSchemaTierAllowsEmptyActions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcResponseParserStrictJsonTierAllowsEmptyActionsTest::RunTest(const FString& Parameters)
{
	const FString ResponseBody = TEXT(R"({"choices":[{"message":{"content":"{\"dialogue\":\"No gameplay action is required.\",\"actions\":[],\"emotion_delta\":{\"valence\":0.0,\"arousal\":0.0,\"dominance\":0.0},\"relationship_delta\":{\"affinity\":0.0,\"trust\":0.0,\"familiarity\":0.0}}"}}]})");
	FParsedLLMResponse ParsedResponse;
	FString ErrorMessage;

	const bool bParsed = FLLMResponseParser::ParseOpenAIChatCompletion(ResponseBody, ParsedResponse, ErrorMessage);
	TestTrue(TEXT("Strict JSON-schema tier should parse successfully when actions array is empty."), bParsed);
	TestTrue(TEXT("Strict JSON-schema empty-actions payload should not report parser errors."), ErrorMessage.IsEmpty());
	TestEqual(TEXT("Strict JSON-schema tier should remain selected when required keys are present and actions is empty."), ParsedResponse.ParseTier, ELLMResponseParseTier::StrictJsonSchema);
	TestEqual(TEXT("Strict JSON-schema empty-actions payload should preserve dialogue text."), ParsedResponse.Dialogue, FString(TEXT("No gameplay action is required.")));
	TestEqual(TEXT("Strict JSON-schema empty-actions payload should preserve empty actions array."), ParsedResponse.Actions.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcResponseParserLooseTierTest,
	"AINpc.Core.Parser.LooseExtractionTier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcResponseParserLooseTierTest::RunTest(const FString& Parameters)
{
	const FString ResponseBody = TEXT(R"({"choices":[{"message":{"content":"Sure, here is the payload:\n```json\n{\"dialogue\":\"Take this gift.\",\"actions\":[{\"type\":\"Action.Give\",\"target\":\"gift_01\"}]}\n```"}}]})");
	FParsedLLMResponse ParsedResponse;
	FString ErrorMessage;

	const bool bParsed = FLLMResponseParser::ParseOpenAIChatCompletion(ResponseBody, ParsedResponse, ErrorMessage);
	TestTrue(TEXT("Loose extraction tier should parse mixed text payloads successfully."), bParsed);
	TestTrue(TEXT("Loose extraction tier should not report parser errors."), ErrorMessage.IsEmpty());
	TestEqual(TEXT("Loose extraction tier should be selected when strict schema fails."), ParsedResponse.ParseTier, ELLMResponseParseTier::LooseExtraction);
	TestEqual(TEXT("Loose extraction tier should extract dialogue text."), ParsedResponse.Dialogue, FString(TEXT("Take this gift.")));
	TestEqual(TEXT("Loose extraction tier should extract one action intent."), ParsedResponse.Actions.Num(), 1);
	if (ParsedResponse.Actions.Num() == 1)
	{
		TestEqual(TEXT("Loose extraction tier action type should match payload."), ParsedResponse.Actions[0].ActionType, FString(TEXT("Action.Give")));
		TestEqual(TEXT("Loose extraction tier action target should match payload."), ParsedResponse.Actions[0].Target, FString(TEXT("gift_01")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcResponseParserLooseTierActionIntentsFallbackTest,
	"AINpc.Core.Parser.LooseExtractionTierFallsBackToActionIntents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcResponseParserLooseTierActionIntentsFallbackTest::RunTest(const FString& Parameters)
{
	const FString ResponseBody = TEXT(R"({"choices":[{"message":{"content":"Sure, here is the payload:\n```json\n{\"dialogue\":\"I salute your courage.\",\"actions\":[],\"action_intents\":[{\"type\":\"Action.Salute\",\"target\":\"player\"}]}\n```"}}]})");
	FParsedLLMResponse ParsedResponse;
	FString ErrorMessage;

	const bool bParsed = FLLMResponseParser::ParseOpenAIChatCompletion(ResponseBody, ParsedResponse, ErrorMessage);
	TestTrue(TEXT("Loose extraction tier should parse when actions array is empty but action_intents is present."), bParsed);
	TestTrue(TEXT("Loose extraction fallback to action_intents should not report parser errors."), ErrorMessage.IsEmpty());
	TestEqual(TEXT("Loose extraction tier should be selected for action_intents fallback case."), ParsedResponse.ParseTier, ELLMResponseParseTier::LooseExtraction);
	TestEqual(TEXT("Loose extraction fallback should preserve dialogue text."), ParsedResponse.Dialogue, FString(TEXT("I salute your courage.")));
	TestEqual(TEXT("Loose extraction fallback should extract one action intent from action_intents."), ParsedResponse.Actions.Num(), 1);
	if (ParsedResponse.Actions.Num() == 1)
	{
		TestEqual(TEXT("Fallback action intent type should come from action_intents field."), ParsedResponse.Actions[0].ActionType, FString(TEXT("Action.Salute")));
		TestEqual(TEXT("Fallback action intent target should come from action_intents field."), ParsedResponse.Actions[0].Target, FString(TEXT("player")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcResponseParserPlainTextTierTest,
	"AINpc.Core.Parser.PlainTextTier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcResponseParserPlainTextTierTest::RunTest(const FString& Parameters)
{
	const FString ResponseBody = TEXT(R"({"choices":[{"message":{"content":"I need a second to think about that."}}]})");
	FParsedLLMResponse ParsedResponse;
	FString ErrorMessage;

	const bool bParsed = FLLMResponseParser::ParseOpenAIChatCompletion(ResponseBody, ParsedResponse, ErrorMessage);
	TestTrue(TEXT("Plain-text downgrade tier should parse successfully."), bParsed);
	TestTrue(TEXT("Plain-text downgrade tier should not report parser errors."), ErrorMessage.IsEmpty());
	TestEqual(TEXT("Plain-text downgrade tier should be selected as last fallback."), ParsedResponse.ParseTier, ELLMResponseParseTier::PlainText);
	TestEqual(TEXT("Plain-text downgrade tier should preserve dialogue text."), ParsedResponse.Dialogue, FString(TEXT("I need a second to think about that.")));
	TestTrue(TEXT("Plain-text downgrade tier should provide default action intent."), ParsedResponse.Actions.Num() > 0);
	if (ParsedResponse.Actions.Num() > 0)
	{
		TestEqual(TEXT("Plain-text downgrade tier should use the default action template."), ParsedResponse.Actions[0].ActionType, FString(TEXT("Action.DefaultTalk")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcStateTreeExitCancelsQueuedDialogueRequestTest,
	"AINpc.Core.Reliability.StateTreeExitCancelsQueuedDialogueRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcStateTreeExitCancelsQueuedDialogueRequestTest::RunTest(const FString& Parameters)
{
	UAINpcComponent::ResetConcurrencyStateForTest();
	UAINpcComponent::SetDialogueDispatchBypassForTest(true);

	const UAINpcSettings* Settings = GetDefault<UAINpcSettings>();
	const int32 DialogueLimit = FMath::Max(1, Settings ? Settings->DialogueRequestConcurrencyLimit : 1);
	UAINpcComponent::SetActiveDialogueRequestSlotsForTest(DialogueLimit);

	UAINpcComponent* Component = NewObject<UAINpcComponent>();
	TestNotNull(TEXT("Component instance should be created for StateTree queued-abort test."), Component);
	if (!Component)
	{
		UAINpcComponent::ResetConcurrencyStateForTest();
		return false;
	}

	const bool bQueueSucceeded = Component->StartDialogue(TEXT("Queue and cancel on StateTree exit."));
	TestTrue(TEXT("StartDialogue should enqueue when pool is saturated."), bQueueSucceeded);
	TestTrue(TEXT("Component should have a queued request before exit cleanup."), Component->HasQueuedDialogueRequestForTest());
	TestEqual(TEXT("Queue should contain one request before exit cleanup."), UAINpcComponent::GetQueuedDialogueRequestCountForTest(), 1);
	TestEqual(TEXT("Queued request should set waiting state before exit cleanup."), Component->GetDialogueState(), ENpcDialogueState::WaitingForLLM);

	FStateTreeTask_LLMQuery::ApplyExitStateCleanupForTest(Component);

	TestFalse(TEXT("Exit cleanup should clear component queue token."), Component->HasQueuedDialogueRequestForTest());
	TestEqual(TEXT("Exit cleanup should remove queued request."), UAINpcComponent::GetQueuedDialogueRequestCountForTest(), 0);
	TestFalse(TEXT("Exit cleanup should end dialogue session."), Component->IsDialogueActive());
	TestFalse(TEXT("Exit cleanup should not leave request in-flight."), Component->IsRequestInFlight());
	TestEqual(TEXT("Exit cleanup should return dialogue state to idle."), Component->GetDialogueState(), ENpcDialogueState::Idle);

	UAINpcComponent::SetActiveDialogueRequestSlotsForTest(DialogueLimit - 1);
	UAINpcComponent::PumpQueuedDialogueRequestsForTest();

	TestFalse(TEXT("No queued request should dispatch after exit cleanup."), Component->IsRequestInFlight());
	TestEqual(TEXT("Queue should remain empty after slot release."), UAINpcComponent::GetQueuedDialogueRequestCountForTest(), 0);
	TestEqual(TEXT("Active slot count should remain unchanged by queue pump when queue is empty."), UAINpcComponent::GetActiveDialogueRequestSlotsForTest(), DialogueLimit - 1);

	UAINpcComponent::ResetConcurrencyStateForTest();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcStateTreeQueuedTimeoutSuppressionAndResumptionTest,
	"AINpc.Core.Reliability.StateTreeQueuedTimeoutSuppressionAndResumption",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcStateTreeQueuedTimeoutSuppressionAndResumptionTest::RunTest(const FString& Parameters)
{
	FStateTreeTask_LLMQueryInstanceData InstanceData;
	InstanceData.TimeoutSeconds = 0.5f;
	InstanceData.TimeoutGraceSeconds = 0.2f;
	InstanceData.ElapsedSeconds = 0.4f;
	InstanceData.bTimeoutExceeded = true;
	InstanceData.TimeoutExceededAtSeconds = 0.2f;

	bool bShouldForceFailureCleanup = false;
	const EStateTreeRunStatus QueuedStatus = FStateTreeTask_LLMQuery::EvaluateWaitingStateForTest(
		InstanceData,
		0.6f,
		false,
		true,
		bShouldForceFailureCleanup);

	TestEqual(TEXT("Queued waiting state should keep running without timeout failure."), QueuedStatus, EStateTreeRunStatus::Running);
	TestTrue(TEXT("Queued waiting state should reset elapsed timeout tracking."), FMath::IsNearlyZero(InstanceData.ElapsedSeconds));
	TestFalse(TEXT("Queued waiting state should clear soft-timeout flag."), InstanceData.bTimeoutExceeded);
	TestTrue(TEXT("Queued waiting state should clear timeout-exceeded timestamp."), FMath::IsNearlyZero(InstanceData.TimeoutExceededAtSeconds));
	TestFalse(TEXT("Queued waiting state should not request failure cleanup."), bShouldForceFailureCleanup);

	const EStateTreeRunStatus SoftTimeoutStatus = FStateTreeTask_LLMQuery::EvaluateWaitingStateForTest(
		InstanceData,
		0.5f,
		true,
		false,
		bShouldForceFailureCleanup);

	TestEqual(TEXT("In-flight transition should continue running on first timeout boundary (soft-timeout)."), SoftTimeoutStatus, EStateTreeRunStatus::Running);
	TestTrue(TEXT("In-flight transition should mark timeout exceeded at first timeout boundary."), InstanceData.bTimeoutExceeded);
	TestTrue(TEXT("In-flight transition should start accumulating elapsed timeout after dispatch begins."), InstanceData.ElapsedSeconds >= InstanceData.TimeoutSeconds);
	TestFalse(TEXT("Soft-timeout should not request failure cleanup yet."), bShouldForceFailureCleanup);

	const EStateTreeRunStatus GraceStatus = FStateTreeTask_LLMQuery::EvaluateWaitingStateForTest(
		InstanceData,
		0.1f,
		true,
		false,
		bShouldForceFailureCleanup);

	TestEqual(TEXT("Grace-window tick should remain running while still inside timeout grace."), GraceStatus, EStateTreeRunStatus::Running);
	TestFalse(TEXT("Grace-window tick should not request cleanup before grace expires."), bShouldForceFailureCleanup);

	const EStateTreeRunStatus HardTimeoutStatus = FStateTreeTask_LLMQuery::EvaluateWaitingStateForTest(
		InstanceData,
		0.11f,
		true,
		false,
		bShouldForceFailureCleanup);

	TestEqual(TEXT("Post-dispatch waiting should fail once timeout grace is exceeded."), HardTimeoutStatus, EStateTreeRunStatus::Failed);
	TestTrue(TEXT("Hard-timeout should request failure cleanup after grace expiration."), bShouldForceFailureCleanup);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcStateTreeTimeoutRoutesThroughFallbackPathTest,
	"AINpc.Core.Reliability.StateTreeTimeoutRoutesThroughFallbackPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcStateTreeTimeoutRoutesThroughFallbackPathTest::RunTest(const FString& Parameters)
{
	UAINpcComponent* Component = NewObject<UAINpcComponent>();
	TestNotNull(TEXT("Component instance should be created for StateTree timeout fallback test."), Component);
	if (!Component)
	{
		return false;
	}

	int32 DegradedCount = 0;
	int32 ResponseCount = 0;
	int32 ErrorCount = 0;
	FString LastDegradedFailureReason;
	Component->OnDialogueDegradedNative().AddLambda(
		[&DegradedCount, &LastDegradedFailureReason](const FString&, const FString& FailureReason)
		{
			++DegradedCount;
			LastDegradedFailureReason = FailureReason;
		});
	Component->OnDialogueResponseNative().AddLambda([&ResponseCount](const FString&) { ++ResponseCount; });
	Component->OnDialogueErrorNative().AddLambda([&ErrorCount](const FString&) { ++ErrorCount; });

	Component->SetDialogueTestState(
		true,
		true,
		FGuid::NewGuid(),
		2,
		ENpcDialogueState::WaitingForLLM);

	Component->HandleStateTreeTimeoutFailureForTest();

	TestFalse(TEXT("Timeout cleanup should clear request-in-flight state."), Component->IsRequestInFlight());
	TestEqual(TEXT("Timeout cleanup should reset retry attempt count."), Component->GetRetryAttemptCountForTest(), 0);
	TestEqual(TEXT("Timeout cleanup should route to fallback speaking state."), Component->GetDialogueState(), ENpcDialogueState::Speaking);
	TestEqual(TEXT("Timeout cleanup should emit one degraded callback."), DegradedCount, 1);
	TestEqual(TEXT("Timeout cleanup should emit one fallback dialogue response."), ResponseCount, 1);
	TestEqual(TEXT("Timeout cleanup should not emit hard error when fallback is available."), ErrorCount, 0);
	TestTrue(TEXT("Timeout degraded failure reason should mention timeout."), LastDegradedFailureReason.Contains(TEXT("timed out"), ESearchCase::IgnoreCase));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDialogueStateTransitionClearsDelayMaskingTest,
	"AINpc.Core.Dialogue.StateTransitionClearsDelayMasking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcDialogueStateTransitionClearsDelayMaskingTest::RunTest(const FString& Parameters)
{
	UAINpcComponent* Component = NewObject<UAINpcComponent>();
	TestNotNull(TEXT("Component instance should be created for dialogue-state housekeeping test."), Component);
	if (!Component)
	{
		return false;
	}

	UNpcPersonaDataAsset* Persona = NewObject<UNpcPersonaDataAsset>();
	TestNotNull(TEXT("Persona data asset should be created for dialogue-state housekeeping test."), Persona);
	if (!Persona)
	{
		return false;
	}

	Persona->DelayFillerTexts.Add(FText::FromString(TEXT("Thinking...")));
	Component->PersonaDataAsset = Persona;
	Component->SetDialogueTestState(true, true, FGuid::NewGuid(), 0, ENpcDialogueState::WaitingForLLM);

	int32 DelayMaskingEndCount = 0;
	Component->OnDelayMaskingEndNative().AddLambda([&DelayMaskingEndCount]() { ++DelayMaskingEndCount; });

	Component->HandleDelayMaskingThresholdReachedForTest();
	TestTrue(TEXT("Threshold path should activate delay masking while still waiting for LLM."), Component->IsDelayMaskingActive());

	Component->SetDialogueStateFromStateTree(ENpcDialogueState::Cooldown);

	TestEqual(TEXT("Leaving WaitingForLLM should transition the component into the requested dialogue state."), Component->GetDialogueState(), ENpcDialogueState::Cooldown);
	TestFalse(TEXT("Leaving WaitingForLLM should clear active delay masking."), Component->IsDelayMaskingActive());
	TestEqual(TEXT("Leaving WaitingForLLM should emit one delay-masking end signal."), DelayMaskingEndCount, 1);
	TestFalse(TEXT("Fresh Cooldown transition should not already satisfy a long state-duration threshold."), Component->HasBeenInDialogueStateLongerThan(1000.0f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDelayMaskingOwnerAttackStartsEventDrivenTest,
	"AINpc.Core.Events.DelayMaskingOwnerAttackStartsEventDriven",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcDelayMaskingOwnerAttackStartsEventDrivenTest::RunTest(const FString& Parameters)
{
	AActor* OwnerActor = NewObject<AActor>();
	AActor* InstigatorActor = NewObject<AActor>();
	UAINpcComponent* Component = NewObject<UAINpcComponent>(OwnerActor);
	TestNotNull(TEXT("Component instance should be created for owner-attack delay-masking test."), Component);
	if (!Component)
	{
		return false;
	}

	UNpcPersonaDataAsset* Persona = NewObject<UNpcPersonaDataAsset>();
	TestNotNull(TEXT("Persona data asset should be created for owner-attack delay-masking test."), Persona);
	if (!Persona)
	{
		return false;
	}

	UAnimMontage* HitReactionMontage = NewObject<UAnimMontage>();
	Persona->HitReactionDelayMaskingMontages.Add(HitReactionMontage);
	Component->PersonaDataAsset = Persona;
	Component->SetDialogueTestState(true, true, FGuid::NewGuid(), 0, ENpcDialogueState::WaitingForLLM);

	int32 DelayMaskingStartCount = 0;
	UAnimMontage* ObservedMontage = nullptr;
	FText ObservedFillerText;
	Component->OnDelayMaskingStartNative().AddLambda(
		[&DelayMaskingStartCount, &ObservedMontage, &ObservedFillerText](UAnimMontage* Montage, const FText& FillerText)
		{
			++DelayMaskingStartCount;
			ObservedMontage = Montage;
			ObservedFillerText = FillerText;
		});

	FNpcAttackEventPayload AttackPayload;
	AttackPayload.InstigatorActor = InstigatorActor;
	AttackPayload.TargetActor = OwnerActor;

	FNpcEventMessage EventMessage;
	EventMessage.Payload.InitializeAs<FNpcAttackEventPayload>(AttackPayload);

	Component->HandleNpcEventStageDispatchedForTest(EventMessage, ENpcEventDispatchStage::DelayMasking);

	TestEqual(TEXT("Owner-targeted attack should trigger exactly one immediate delay-masking start."), DelayMaskingStartCount, 1);
	TestEqual(TEXT("Owner-targeted attack should use hit-reaction montage options."), ObservedMontage, HitReactionMontage);
	TestTrue(TEXT("Event-driven delay masking should suppress filler text before threshold."), ObservedFillerText.IsEmptyOrWhitespace());
	TestTrue(TEXT("Event-driven start should mark delay masking active."), Component->IsDelayMaskingActive());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDelayMaskingNonOwnerAttackDoesNotStartTest,
	"AINpc.Core.Events.DelayMaskingNonOwnerAttackDoesNotStart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcDelayMaskingNonOwnerAttackDoesNotStartTest::RunTest(const FString& Parameters)
{
	AActor* OwnerActor = NewObject<AActor>();
	AActor* OtherActor = NewObject<AActor>();
	UAINpcComponent* Component = NewObject<UAINpcComponent>(OwnerActor);
	TestNotNull(TEXT("Component instance should be created for non-owner attack delay-masking test."), Component);
	if (!Component)
	{
		return false;
	}

	UNpcPersonaDataAsset* Persona = NewObject<UNpcPersonaDataAsset>();
	TestNotNull(TEXT("Persona data asset should be created for non-owner attack delay-masking test."), Persona);
	if (!Persona)
	{
		return false;
	}

	UAnimMontage* HitReactionMontage = NewObject<UAnimMontage>();
	Persona->HitReactionDelayMaskingMontages.Add(HitReactionMontage);
	Component->PersonaDataAsset = Persona;
	Component->SetDialogueTestState(true, true, FGuid::NewGuid(), 0, ENpcDialogueState::WaitingForLLM);

	int32 DelayMaskingStartCount = 0;
	Component->OnDelayMaskingStartNative().AddLambda(
		[&DelayMaskingStartCount](UAnimMontage* Montage, const FText& FillerText)
		{
			(void)Montage;
			(void)FillerText;
			++DelayMaskingStartCount;
		});

	FNpcAttackEventPayload AttackPayload;
	AttackPayload.TargetActor = OtherActor;

	FNpcEventMessage EventMessage;
	EventMessage.Payload.InitializeAs<FNpcAttackEventPayload>(AttackPayload);

	Component->HandleNpcEventStageDispatchedForTest(EventMessage, ENpcEventDispatchStage::DelayMasking);

	TestEqual(TEXT("Non-owner attack should not trigger immediate delay-masking start."), DelayMaskingStartCount, 0);
	TestFalse(TEXT("Non-owner attack should keep delay masking inactive until threshold path."), Component->IsDelayMaskingActive());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDelayMaskingOwnerAttackDoesNotStartWhenNotWaitingStateTest,
	"AINpc.Core.Events.DelayMaskingOwnerAttackDoesNotStartWhenNotWaitingState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcDelayMaskingOwnerAttackDoesNotStartWhenNotWaitingStateTest::RunTest(const FString& Parameters)
{
	AActor* OwnerActor = NewObject<AActor>();
	AActor* InstigatorActor = NewObject<AActor>();
	UAINpcComponent* Component = NewObject<UAINpcComponent>(OwnerActor);
	TestNotNull(TEXT("Component instance should be created for non-waiting-state delay-masking test."), Component);
	if (!Component)
	{
		return false;
	}

	UNpcPersonaDataAsset* Persona = NewObject<UNpcPersonaDataAsset>();
	TestNotNull(TEXT("Persona data asset should be created for non-waiting-state delay-masking test."), Persona);
	if (!Persona)
	{
		return false;
	}

	UAnimMontage* HitReactionMontage = NewObject<UAnimMontage>();
	Persona->HitReactionDelayMaskingMontages.Add(HitReactionMontage);
	Component->PersonaDataAsset = Persona;
	Component->SetDialogueTestState(true, true, FGuid::NewGuid(), 0, ENpcDialogueState::Speaking);

	int32 DelayMaskingStartCount = 0;
	Component->OnDelayMaskingStartNative().AddLambda(
		[&DelayMaskingStartCount](UAnimMontage* Montage, const FText& FillerText)
		{
			(void)Montage;
			(void)FillerText;
			++DelayMaskingStartCount;
		});

	FNpcAttackEventPayload AttackPayload;
	AttackPayload.InstigatorActor = InstigatorActor;
	AttackPayload.TargetActor = OwnerActor;

	FNpcEventMessage EventMessage;
	EventMessage.Payload.InitializeAs<FNpcAttackEventPayload>(AttackPayload);

	Component->HandleNpcEventStageDispatchedForTest(EventMessage, ENpcEventDispatchStage::DelayMasking);

	TestEqual(TEXT("Owner-targeted attack should not trigger event-driven delay masking outside waiting state."), DelayMaskingStartCount, 0);
	TestFalse(TEXT("Event-driven delay masking should remain inactive outside waiting state."), Component->IsDelayMaskingActive());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcEventTagFallbackRoutingAndMixedRoutingTest,
	"AINpc.Core.Events.EventTagFallbackRoutingAndMixedRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcEventTagFallbackRoutingAndMixedRoutingTest::RunTest(const FString& Parameters)
{
	AActor* OwnerActor = NewObject<AActor>();
	UAINpcComponent* Component = NewObject<UAINpcComponent>(OwnerActor);
	TestNotNull(TEXT("Component instance should be created for EventTag routing fallback test."), Component);
	if (!Component)
	{
		return false;
	}

	FGameplayTag MatchingTag;
	FGameplayTag NonMatchingTag;
	const bool bHasDistinctTags = GetDistinctRoutingTestTags(MatchingTag, NonMatchingTag);
	TestTrue(TEXT("Gameplay tag registry should provide at least two distinct tags for routing tests."), bHasDistinctTags);
	if (!bHasDistinctTags)
	{
		return false;
	}

	UNpcPersonaDataAsset* Persona = NewObject<UNpcPersonaDataAsset>();
	TestNotNull(TEXT("Persona data asset should be created for EventTag routing fallback test."), Persona);
	if (!Persona)
	{
		return false;
	}

	UAnimMontage* HitReactionMontage = NewObject<UAnimMontage>();
	Persona->HitReactionDelayMaskingMontages.Add(HitReactionMontage);
	Component->PersonaDataAsset = Persona;
	Component->EventSubscriptionTags.AddTag(MatchingTag);
	Component->SetDialogueTestState(true, true, FGuid::NewGuid(), 0, ENpcDialogueState::WaitingForLLM);

	int32 DelayMaskingStartCount = 0;
	Component->OnDelayMaskingStartNative().AddLambda(
		[&DelayMaskingStartCount](UAnimMontage* Montage, const FText& FillerText)
		{
			(void)Montage;
			(void)FillerText;
			++DelayMaskingStartCount;
		});

	const auto MakeOwnerAttackEventMessage = [OwnerActor](const FGameplayTag EventTag, const FGameplayTagContainer& RoutingTags)
	{
		FNpcAttackEventPayload Payload;
		Payload.TargetActor = OwnerActor;

		FNpcEventMessage EventMessage;
		EventMessage.EventTag = EventTag;
		EventMessage.RoutingTags = RoutingTags;
		EventMessage.Payload.InitializeAs<FNpcAttackEventPayload>(Payload);
		return EventMessage;
	};

	const FGameplayTagContainer EmptyRoutingTags;
	Component->HandleNpcEventStageDispatchedForTest(
		MakeOwnerAttackEventMessage(NonMatchingTag, EmptyRoutingTags),
		ENpcEventDispatchStage::DelayMasking);

	TestEqual(
		TEXT("EventTag-only event with a non-matching EventTag should not be processed when subscriptions are set."),
		DelayMaskingStartCount,
		0);

	Component->HandleNpcEventStageDispatchedForTest(
		MakeOwnerAttackEventMessage(MatchingTag, EmptyRoutingTags),
		ENpcEventDispatchStage::DelayMasking);

	TestEqual(
		TEXT("EventTag-only event with a matching EventTag should be processed through EventTag fallback routing."),
		DelayMaskingStartCount,
		1);

	FGameplayTagContainer MatchingRoutingTags;
	MatchingRoutingTags.AddTag(MatchingTag);
	Component->HandleNpcEventStageDispatchedForTest(
		MakeOwnerAttackEventMessage(NonMatchingTag, MatchingRoutingTags),
		ENpcEventDispatchStage::DelayMasking);

	TestEqual(
		TEXT("Mixed routing should still process when RoutingTags contain a subscribed tag even if EventTag differs."),
		DelayMaskingStartCount,
		2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDelayMaskingOwnerGiftAndTradeStartEventDrivenTest,
	"AINpc.Core.Events.DelayMaskingOwnerGiftAndTradeStartEventDriven",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcDelayMaskingOwnerGiftAndTradeStartEventDrivenTest::RunTest(const FString& Parameters)
{
	AActor* OwnerActor = NewObject<AActor>();
	AActor* OtherActor = NewObject<AActor>();
	UAINpcComponent* Component = NewObject<UAINpcComponent>(OwnerActor);
	TestNotNull(TEXT("Component instance should be created for owner gift/trade delay-masking test."), Component);
	if (!Component)
	{
		return false;
	}

	UNpcPersonaDataAsset* Persona = NewObject<UNpcPersonaDataAsset>();
	TestNotNull(TEXT("Persona data asset should be created for owner gift/trade delay-masking test."), Persona);
	if (!Persona)
	{
		return false;
	}

	UAnimMontage* InspectMontage = NewObject<UAnimMontage>();
	Persona->InspectDelayMaskingMontages.Add(InspectMontage);
	Component->PersonaDataAsset = Persona;
	Component->SetDialogueTestState(true, true, FGuid::NewGuid(), 0, ENpcDialogueState::WaitingForLLM);

	int32 DelayMaskingStartCount = 0;
	TArray<UAnimMontage*> ObservedMontages;
	TArray<FText> ObservedFillerTexts;
	Component->OnDelayMaskingStartNative().AddLambda(
		[&DelayMaskingStartCount, &ObservedMontages, &ObservedFillerTexts](UAnimMontage* Montage, const FText& FillerText)
		{
			++DelayMaskingStartCount;
			ObservedMontages.Add(Montage);
			ObservedFillerTexts.Add(FillerText);
		});

	FNpcGiftEventPayload GiftPayload;
	GiftPayload.ReceiverActor = OwnerActor;
	FNpcEventMessage GiftEventMessage;
	GiftEventMessage.Payload.InitializeAs<FNpcGiftEventPayload>(GiftPayload);
	Component->HandleNpcEventStageDispatchedForTest(GiftEventMessage, ENpcEventDispatchStage::DelayMasking);

	FNpcTradeEventPayload TradePayload;
	TradePayload.InitiatorActor = OwnerActor;
	TradePayload.CounterpartyActor = OtherActor;
	FNpcEventMessage TradeEventMessage;
	TradeEventMessage.Payload.InitializeAs<FNpcTradeEventPayload>(TradePayload);
	Component->HandleNpcEventStageDispatchedForTest(TradeEventMessage, ENpcEventDispatchStage::DelayMasking);

	TestEqual(TEXT("Owner-involved gift/trade payloads should each trigger immediate delay masking."), DelayMaskingStartCount, 2);
	if (ObservedMontages.Num() == 2)
	{
		TestEqual(TEXT("Owner gift should use inspect montage options."), ObservedMontages[0], InspectMontage);
		TestEqual(TEXT("Owner trade should use inspect montage options."), ObservedMontages[1], InspectMontage);
	}
	if (ObservedFillerTexts.Num() == 2)
	{
		TestTrue(TEXT("Owner gift event-driven start should suppress filler text."), ObservedFillerTexts[0].IsEmptyOrWhitespace());
		TestTrue(TEXT("Owner trade event-driven start should suppress filler text."), ObservedFillerTexts[1].IsEmptyOrWhitespace());
	}
	TestTrue(TEXT("Owner gift/trade start should mark delay masking active."), Component->IsDelayMaskingActive());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDelayMaskingNonOwnerGiftAndTradeDoNotStartTest,
	"AINpc.Core.Events.DelayMaskingNonOwnerGiftAndTradeDoNotStart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcDelayMaskingNonOwnerGiftAndTradeDoNotStartTest::RunTest(const FString& Parameters)
{
	AActor* OwnerActor = NewObject<AActor>();
	AActor* OtherActorA = NewObject<AActor>();
	AActor* OtherActorB = NewObject<AActor>();
	UAINpcComponent* Component = NewObject<UAINpcComponent>(OwnerActor);
	TestNotNull(TEXT("Component instance should be created for non-owner gift/trade delay-masking test."), Component);
	if (!Component)
	{
		return false;
	}

	UNpcPersonaDataAsset* Persona = NewObject<UNpcPersonaDataAsset>();
	TestNotNull(TEXT("Persona data asset should be created for non-owner gift/trade delay-masking test."), Persona);
	if (!Persona)
	{
		return false;
	}

	UAnimMontage* InspectMontage = NewObject<UAnimMontage>();
	Persona->InspectDelayMaskingMontages.Add(InspectMontage);
	Component->PersonaDataAsset = Persona;
	Component->SetDialogueTestState(true, true, FGuid::NewGuid(), 0, ENpcDialogueState::WaitingForLLM);

	int32 DelayMaskingStartCount = 0;
	Component->OnDelayMaskingStartNative().AddLambda(
		[&DelayMaskingStartCount](UAnimMontage* Montage, const FText& FillerText)
		{
			(void)Montage;
			(void)FillerText;
			++DelayMaskingStartCount;
		});

	FNpcGiftEventPayload GiftPayload;
	GiftPayload.ReceiverActor = OtherActorA;
	FNpcEventMessage GiftEventMessage;
	GiftEventMessage.Payload.InitializeAs<FNpcGiftEventPayload>(GiftPayload);
	Component->HandleNpcEventStageDispatchedForTest(GiftEventMessage, ENpcEventDispatchStage::DelayMasking);

	FNpcTradeEventPayload TradePayload;
	TradePayload.InitiatorActor = OtherActorA;
	TradePayload.CounterpartyActor = OtherActorB;
	FNpcEventMessage TradeEventMessage;
	TradeEventMessage.Payload.InitializeAs<FNpcTradeEventPayload>(TradePayload);
	Component->HandleNpcEventStageDispatchedForTest(TradeEventMessage, ENpcEventDispatchStage::DelayMasking);

	TestEqual(TEXT("Non-owner gift/trade payloads should not trigger immediate delay masking."), DelayMaskingStartCount, 0);
	TestFalse(TEXT("Non-owner gift/trade payloads should keep delay masking inactive until threshold path."), Component->IsDelayMaskingActive());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcDelayMaskingThresholdFillerAfterEventDrivenStartTest,
	"AINpc.Core.Events.DelayMaskingThresholdFillerAfterEventDrivenStart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcDelayMaskingThresholdFillerAfterEventDrivenStartTest::RunTest(const FString& Parameters)
{
	AActor* OwnerActor = NewObject<AActor>();
	AActor* InstigatorActor = NewObject<AActor>();
	UAINpcComponent* Component = NewObject<UAINpcComponent>(OwnerActor);
	TestNotNull(TEXT("Component instance should be created for threshold filler-text test."), Component);
	if (!Component)
	{
		return false;
	}

	UNpcPersonaDataAsset* Persona = NewObject<UNpcPersonaDataAsset>();
	TestNotNull(TEXT("Persona data asset should be created for threshold filler-text test."), Persona);
	if (!Persona)
	{
		return false;
	}

	const FText ExpectedThresholdFillerText = FText::FromString(TEXT("Still thinking..."));
	UAnimMontage* HitReactionMontage = NewObject<UAnimMontage>();
	Persona->HitReactionDelayMaskingMontages.Add(HitReactionMontage);
	Persona->DelayFillerTexts.Add(ExpectedThresholdFillerText);
	Component->PersonaDataAsset = Persona;
	Component->SetDialogueTestState(true, true, FGuid::NewGuid(), 0, ENpcDialogueState::WaitingForLLM);

	int32 DelayMaskingStartCount = 0;
	TArray<UAnimMontage*> ObservedMontages;
	TArray<FText> ObservedFillerTexts;
	Component->OnDelayMaskingStartNative().AddLambda(
		[&DelayMaskingStartCount, &ObservedMontages, &ObservedFillerTexts](UAnimMontage* Montage, const FText& FillerText)
		{
			++DelayMaskingStartCount;
			ObservedMontages.Add(Montage);
			ObservedFillerTexts.Add(FillerText);
		});

	FNpcAttackEventPayload AttackPayload;
	AttackPayload.InstigatorActor = InstigatorActor;
	AttackPayload.TargetActor = OwnerActor;

	FNpcEventMessage EventMessage;
	EventMessage.Payload.InitializeAs<FNpcAttackEventPayload>(AttackPayload);

	Component->HandleNpcEventStageDispatchedForTest(EventMessage, ENpcEventDispatchStage::DelayMasking);
	Component->HandleDelayMaskingThresholdReachedForTest();

	TestEqual(TEXT("Event-driven start plus threshold should emit two delay-masking start signals."), DelayMaskingStartCount, 2);
	if (ObservedMontages.Num() == 2)
	{
		TestEqual(TEXT("Event-driven start should emit hit-reaction montage."), ObservedMontages[0], HitReactionMontage);
		TestEqual(TEXT("Threshold filler-text update should avoid restarting montage."), ObservedMontages[1], static_cast<UAnimMontage*>(nullptr));
	}
	if (ObservedFillerTexts.Num() == 2)
	{
		TestTrue(TEXT("Event-driven start should emit empty filler text."), ObservedFillerTexts[0].IsEmptyOrWhitespace());
		TestEqual(TEXT("Threshold path should emit configured filler text even when masking is already active."), ObservedFillerTexts[1].ToString(), ExpectedThresholdFillerText.ToString());
	}
	TestTrue(TEXT("Threshold update should preserve active delay-masking state."), Component->IsDelayMaskingActive());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcEventSubsystemDispatchOrderTest,
	"AINpc.Core.Events.SubsystemDispatchOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcEventSubsystemDispatchOrderTest::RunTest(const FString& Parameters)
{
	const TArray<ENpcEventDispatchStage> Order = UNpcEventSubsystem::GetDefaultDispatchOrder();
	TestEqual(TEXT("Default dispatch order must include exactly 4 stages."), Order.Num(), 4);
	if (Order.Num() != 4)
	{
		return false;
	}

	TestEqual(TEXT("Stage 0 should be DelayMasking."), Order[0], ENpcEventDispatchStage::DelayMasking);
	TestEqual(TEXT("Stage 1 should be EmotionAppraisal."), Order[1], ENpcEventDispatchStage::EmotionAppraisal);
	TestEqual(TEXT("Stage 2 should be MemoryWrite."), Order[2], ENpcEventDispatchStage::MemoryWrite);
	TestEqual(TEXT("Stage 3 should be PromptUpdate."), Order[3], ENpcEventDispatchStage::PromptUpdate);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcEventSubsystemBroadcastStageSequenceTest,
	"AINpc.Core.Events.BroadcastDispatchesAllStagesInOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcEventSubsystemBroadcastStageSequenceTest::RunTest(const FString& Parameters)
{
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	TestNotNull(TEXT("Transient game instance should be created for event subsystem test."), GameInstance);
	if (!GameInstance)
	{
		return false;
	}

	UNpcEventSubsystem* Subsystem = NewObject<UNpcEventSubsystem>(GameInstance);
	TestNotNull(TEXT("Event subsystem instance should be created for broadcast test."), Subsystem);
	if (!Subsystem)
	{
		return false;
	}

	TArray<ENpcEventDispatchStage> ObservedStages;
	Subsystem->OnEventStageDispatchedNative().AddLambda(
		[&ObservedStages](const FNpcEventMessage&, ENpcEventDispatchStage DispatchStage)
		{
			ObservedStages.Add(DispatchStage);
		});

	FNpcEventMessage EventMessage;
	Subsystem->BroadcastEvent(EventMessage);

	const TArray<ENpcEventDispatchStage> ExpectedStages = UNpcEventSubsystem::GetDefaultDispatchOrder();
	TestEqual(TEXT("Broadcast should dispatch all default stages exactly once."), ObservedStages.Num(), ExpectedStages.Num());
	if (ObservedStages.Num() != ExpectedStages.Num())
	{
		return false;
	}

	for (int32 Index = 0; Index < ExpectedStages.Num(); ++Index)
	{
		TestEqual(FString::Printf(TEXT("Broadcast dispatch stage index %d should match default order."), Index), ObservedStages[Index], ExpectedStages[Index]);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcExecuteSmartObjectWhitelistContractTest,
	"AINpc.Core.SmartObject.ExecuteTaskWhitelist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcExecuteSmartObjectWhitelistContractTest::RunTest(const FString& Parameters)
{
	{
		FNpcAction ActionIntent;
		ActionIntent.ActionType = TEXT("Action.Inspect");
		ActionIntent.Target = TEXT("SO_SLOT_Chair");

		FString FailureReason;
		const bool bIsLegal = FStateTreeTask_ExecuteSmartObject::ValidateSmartObjectActionForTest(
			ActionIntent,
			{TEXT("SO_SLOT_Chair"), TEXT("SO_SLOT_Table")},
			FailureReason);

		TestTrue(TEXT("Whitelisted SmartObject target should pass ExecuteSmartObject action validation."), bIsLegal);
		TestTrue(TEXT("Legal SmartObject action validation should not emit failure reason."), FailureReason.IsEmpty());
	}

	{
		FNpcAction ActionIntent;
		ActionIntent.ActionType = TEXT("Action.Inspect");
		ActionIntent.Target = TEXT("SO_SLOT_Illegal");

		FString FailureReason;
		const bool bIsLegal = FStateTreeTask_ExecuteSmartObject::ValidateSmartObjectActionForTest(
			ActionIntent,
			{TEXT("SO_SLOT_Chair"), TEXT("SO_SLOT_Table")},
			FailureReason);

		TestFalse(TEXT("Non-whitelisted SmartObject target should be rejected by ExecuteSmartObject action validation."), bIsLegal);
		TestTrue(TEXT("Rejected SmartObject action should emit a failure reason."), !FailureReason.IsEmpty());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcExecuteSmartObjectWhitelistAllowsLegalTargetTest,
	"AINpc.Core.SmartObject.ExecuteTaskWhitelistCases.AllowsLegalTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcExecuteSmartObjectWhitelistAllowsLegalTargetTest::RunTest(const FString& Parameters)
{
	FNpcAction ActionIntent;
	ActionIntent.ActionType = TEXT("Action.Inspect");
	ActionIntent.Target = TEXT("SO_SLOT_Chair");

	FString FailureReason;
	const bool bIsLegal = FStateTreeTask_ExecuteSmartObject::ValidateSmartObjectActionForTest(
		ActionIntent,
		{TEXT("SO_SLOT_Chair"), TEXT("SO_SLOT_Table")},
		FailureReason);

	TestTrue(TEXT("Whitelisted SmartObject target should pass ExecuteSmartObject action validation."), bIsLegal);
	TestTrue(TEXT("Legal SmartObject action validation should not emit failure reason."), FailureReason.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcExecuteSmartObjectWhitelistRejectsIllegalTargetTest,
	"AINpc.Core.SmartObject.ExecuteTaskWhitelistCases.RejectsIllegalTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcExecuteSmartObjectWhitelistRejectsIllegalTargetTest::RunTest(const FString& Parameters)
{
	FNpcAction ActionIntent;
	ActionIntent.ActionType = TEXT("Action.Inspect");
	ActionIntent.Target = TEXT("SO_SLOT_Illegal");

	FString FailureReason;
	const bool bIsLegal = FStateTreeTask_ExecuteSmartObject::ValidateSmartObjectActionForTest(
		ActionIntent,
		{TEXT("SO_SLOT_Chair"), TEXT("SO_SLOT_Table")},
		FailureReason);

	TestFalse(TEXT("Non-whitelisted SmartObject target should be rejected by ExecuteSmartObject action validation."), bIsLegal);
	TestTrue(TEXT("Rejected SmartObject action should emit a failure reason."), !FailureReason.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcSmartObjectUserActorResolutionPrefersControllerPawnTest,
	"AINpc.Core.SmartObject.UserActorResolutionPrefersControllerPawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcSmartObjectUserActorResolutionPrefersControllerPawnTest::RunTest(const FString& Parameters)
{
	AAINpcController* ControllerOwner = NewObject<AAINpcController>();
	TestNotNull(TEXT("Controller owner should be created for SmartObject task user resolution test."), ControllerOwner);
	if (!ControllerOwner)
	{
		return false;
	}

	APawn* ControlledPawn = NewObject<APawn>();
	TestNotNull(TEXT("Controlled pawn should be created for SmartObject task user resolution test."), ControlledPawn);
	if (!ControlledPawn)
	{
		return false;
	}

	ControllerOwner->SetPawn(ControlledPawn);

	TObjectPtr<AActor> CachedUserActor = nullptr;
	AActor* ResolvedActor = AINpc::SmartObjectTaskUtils::ResolveUserActorFromOwnerObject(ControllerOwner, CachedUserActor);
	TestEqual(
		TEXT("Controller-owned SmartObject task resolution should prefer the possessed pawn."),
		ResolvedActor,
		static_cast<AActor*>(ControlledPawn));
	TestEqual(
		TEXT("Resolved pawn should be cached for subsequent calls."),
		static_cast<AActor*>(CachedUserActor.Get()),
		static_cast<AActor*>(ControlledPawn));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcSmartObjectCleanupScopesClaimsByWorldTest,
	"AINpc.Core.SmartObject.CleanupScopesClaimsByWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcSmartObjectCleanupScopesClaimsByWorldTest::RunTest(const FString& Parameters)
{
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	TestNotNull(TEXT("Game instance should be created for world-scoped cleanup test."), GameInstance);
	if (!GameInstance)
	{
		return false;
	}

	USmartObjectBridgeContext* BridgeContext = NewObject<USmartObjectBridgeContext>(GameInstance);
	TestNotNull(TEXT("Bridge context should be created for world-scoped cleanup test."), BridgeContext);
	if (!BridgeContext)
	{
		return false;
	}

	UWorld* FirstWorld = NewObject<UWorld>();
	UWorld* SecondWorld = NewObject<UWorld>();
	TestNotNull(TEXT("First transient world should be created for world-scoped cleanup test."), FirstWorld);
	TestNotNull(TEXT("Second transient world should be created for world-scoped cleanup test."), SecondWorld);
	if (!FirstWorld || !SecondWorld)
	{
		return false;
	}

	BridgeContext->AddTrackedClaimForTest(FirstWorld);
	BridgeContext->AddTrackedClaimForTest(SecondWorld);

	TestEqual(TEXT("Test setup should track two active claims."), BridgeContext->GetTrackedClaimCountForTest(), 2);
	TestEqual(TEXT("Test setup should track one claim in first world."), BridgeContext->GetTrackedClaimCountForWorldForTest(FirstWorld), 1);
	TestEqual(TEXT("Test setup should track one claim in second world."), BridgeContext->GetTrackedClaimCountForWorldForTest(SecondWorld), 1);

	BridgeContext->TriggerWorldCleanupForTest(FirstWorld);

	TestEqual(TEXT("Cleaning the first world should only remove claims from the first world."), BridgeContext->GetTrackedClaimCountForTest(), 1);
	TestEqual(TEXT("Cleaning the first world should clear first-world claims."), BridgeContext->GetTrackedClaimCountForWorldForTest(FirstWorld), 0);
	TestEqual(TEXT("Cleaning the first world should preserve second-world claims."), BridgeContext->GetTrackedClaimCountForWorldForTest(SecondWorld), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcAnthropicProviderInstantiationTest,
	"AINpc.LLM.AnthropicProvider",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcAnthropicProviderInstantiationTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FAnthropicProvider> Provider = MakeShared<FAnthropicProvider>(
		TEXT("test-api-key"),
		TEXT("claude-3-5-sonnet-20241022"),
		TEXT("https://api.anthropic.com/v1"));

	TestTrue(TEXT("AnthropicProvider should be instantiated successfully."), Provider.IsValid());

	if (Provider.IsValid())
	{
		FLLMProviderCapabilities Capabilities = Provider->GetCapabilities();
		TestTrue(TEXT("AnthropicProvider should support tool calling."), Capabilities.bSupportsToolCalling);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAINpcRetryFallbackIntegrationTest,
	"AINpc.Reliability.RetryFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcRetryFallbackIntegrationTest::RunTest(const FString& Parameters)
{
	UAINpcComponent* Component = NewObject<UAINpcComponent>();
	TestNotNull(TEXT("Component instance should be created for retry-fallback integration test."), Component);
	if (!Component)
	{
		return false;
	}

	int32 DegradedCount = 0;
	Component->OnDialogueDegradedNative().AddLambda([&DegradedCount](const FString&, const FString&) { ++DegradedCount; });

	const FGuid ActiveRequestId = FGuid::NewGuid();
	Component->SetDialogueTestState(true, true, ActiveRequestId, 0, ENpcDialogueState::WaitingForLLM);

	FLLMResponse RetryableFailure;
	RetryableFailure.RequestId = ActiveRequestId;
	RetryableFailure.bSuccess = false;
	RetryableFailure.HttpStatusCode = 429;
	RetryableFailure.ErrorMessage = TEXT("Rate limit exceeded");

	Component->HandleRequestCompletedForTest(RetryableFailure);

	UE_LOG(LogAINpc, Verbose, TEXT("Retry attempt 1 triggered after rate limit failure"));
	TestEqual(TEXT("First retry should increment attempt counter."), Component->GetRetryAttemptCountForTest(), 1);

	const UAINpcSettings* Settings = GetDefault<UAINpcSettings>();
	const int32 MaxRetries = Settings ? Settings->MaxRequestRetries : 3;

	for (int32 i = 1; i < MaxRetries; ++i)
	{
		Component->HandleRequestCompletedForTest(RetryableFailure);
		UE_LOG(LogAINpc, Verbose, TEXT("Retry attempt %d triggered"), i + 1);
	}

	FLLMResponse FinalFailure;
	FinalFailure.RequestId = ActiveRequestId;
	FinalFailure.bSuccess = false;
	FinalFailure.HttpStatusCode = 500;
	FinalFailure.ErrorMessage = TEXT("Service unavailable");

	Component->HandleRequestCompletedForTest(FinalFailure);

	UE_LOG(LogAINpc, Verbose, TEXT("Fallback to template response triggered after retry exhaustion"));
	UE_LOG(LogAINpc, Verbose, TEXT("Timeout degradation notification sent to blueprint"));
	TestEqual(TEXT("Exhausted retries should trigger degradation."), DegradedCount, 1);
	TestEqual(TEXT("Component should transition to speaking after fallback."), Component->GetDialogueState(), ENpcDialogueState::Speaking);

	return true;
}

#endif
