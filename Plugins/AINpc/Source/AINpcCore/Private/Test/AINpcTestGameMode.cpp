#include "Test/AINpcTestGameMode.h"
#include "Test/AINpcTestCharacter.h"
#include "Test/AINpcTestSmartObjectActor.h"
#include "Components/AINpcComponent.h"
#include "Components/AINpcProviderConfigResolver.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SmartObjectBridge/AINpcSmartObjectRuntimeExecutor.h"
#include "SmartObjectBridge/SmartObjectBridgeContext.h"
#include "TimerManager.h"

namespace
{
	const float InitialDialogueDelaySeconds = 3.0f;
	const float VerificationTimeoutSeconds = 45.0f;
	const float SuccessExitDelaySeconds = 5.0f;
	const float FailureExitDelaySeconds = 8.0f;
	const float VisualVerificationPollSeconds = 0.1f;
	const float SmartObjectSearchRadius = 1200.0f;
	const int32 SmartObjectClaimPriority = 2;
	const TCHAR* InitialPromptTemplateFileName = TEXT("AINpcVisualHarnessInitialPrompt.txt");
	const TCHAR* SmartObjectTargetIdPlaceholder = TEXT("{SmartObjectTargetId}");

	bool LoadInitialPromptTemplate(FString& OutPromptTemplate, FString& OutFailureReason)
	{
		const FString TemplatePath = FPaths::Combine(FPaths::ProjectConfigDir(), InitialPromptTemplateFileName);
		FString LoadedTemplate;
		if (!FFileHelper::LoadFileToString(LoadedTemplate, *TemplatePath))
		{
			OutFailureReason = FString::Printf(
				TEXT("Failed to load visual harness initial prompt template from %s."),
				*TemplatePath);
			return false;
		}

		LoadedTemplate.TrimStartAndEndInline();
		if (LoadedTemplate.IsEmpty())
		{
			OutFailureReason = FString::Printf(
				TEXT("Visual harness initial prompt template was empty: %s."),
				*TemplatePath);
			return false;
		}

		if (!LoadedTemplate.Contains(SmartObjectTargetIdPlaceholder))
		{
			OutFailureReason = FString::Printf(
				TEXT("Visual harness initial prompt template %s is missing required placeholder %s."),
				*TemplatePath,
				SmartObjectTargetIdPlaceholder);
			return false;
		}

		OutPromptTemplate = MoveTemp(LoadedTemplate);
		return true;
	}
}

AAINpcTestGameMode::AAINpcTestGameMode()
{
	DefaultPawnClass = AAINpcTestCharacter::StaticClass();
}

void AAINpcTestGameMode::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogTemp, Warning, TEXT("=== AAINpcTestGameMode::BeginPlay CALLED ==="));
	SpawnTestNpc();

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 30.0f, FColor::Cyan,
			TEXT("AAINpcTestGameMode::BeginPlay - NPC will spawn and auto-dialogue"));
	}
	UE_LOG(LogTemp, Warning, TEXT("=== AAINpcTestGameMode::BeginPlay FINISHED ==="));
}

AActor* AAINpcTestGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	return nullptr; // spawn at origin
}

void AAINpcTestGameMode::SpawnTestNpc()
{
	UE_LOG(LogTemp, Warning, TEXT("=== SpawnTestNpc START ==="));
	UWorld* World = GetWorld();
	if (!World)
	{
		RecordFailure(TEXT("SpawnTestNpc failed because World is null."));
		return;
	}

	const FVector SpawnLocation(500.0f, 0.0f, 100.0f);
	const FVector SmartObjectLocation(900.0f, 0.0f, 40.0f);

	FActorSpawnParameters Params;
	Params.Name = TEXT("AutoTestNpc");
	Params.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	FActorSpawnParameters SmartObjectParams;
	SmartObjectParams.Name = TEXT("AutoTestSmartObject");
	SmartObjectParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	SpawnedSmartObject = World->SpawnActor<AAINpcTestSmartObjectActor>(
		AAINpcTestSmartObjectActor::StaticClass(),
		SmartObjectLocation,
		FRotator::ZeroRotator,
		SmartObjectParams);
	if (!SpawnedSmartObject)
	{
		RecordFailure(TEXT("Failed to spawn runtime SmartObject actor for the visual harness."));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("=== Spawning NPC at %s ==="), *SpawnLocation.ToString());
	SpawnedNpc = World->SpawnActor<AAINpcTestCharacter>(
		AAINpcTestCharacter::StaticClass(), SpawnLocation, FRotator::ZeroRotator, Params);

	if (!SpawnedNpc || !SpawnedNpc->NpcComponent)
	{
		RecordFailure(TEXT("Failed to spawn NPC test character or its UAINpcComponent is null."));
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("=== NPC spawned successfully! ==="));

	// Set camera to look at NPC
	APlayerController* PC = World->GetFirstPlayerController();
	if (PC)
	{
		FVector CameraLoc = SpawnLocation + FVector(-300.0f, 0.0f, 50.0f);
		FRotator CameraRot = (SpawnLocation - CameraLoc).Rotation();

		// Set view target to NPC
		PC->SetViewTarget(SpawnedNpc);

		// Or teleport player pawn to camera position
		if (APawn* PlayerPawn = PC->GetPawn())
		{
			PlayerPawn->SetActorLocation(CameraLoc);
			PC->SetControlRotation(CameraRot);
		}
		UE_LOG(LogTemp, Warning, TEXT("=== Camera set to look at NPC ==="));
	}

	UAINpcComponent* const NpcComponent = SpawnedNpc->NpcComponent;
	NpcComponent->OnDialogueSessionStarted.AddDynamic(this, &AAINpcTestGameMode::OnNpcSessionStarted);
	NpcComponent->OnDialogueResponse.AddDynamic(this, &AAINpcTestGameMode::OnNpcResponse);
	NpcComponent->OnPartialResponse.AddDynamic(this, &AAINpcTestGameMode::OnNpcPartialResponse);
	NpcComponent->OnDialogueError.AddDynamic(this, &AAINpcTestGameMode::OnNpcError);
	NpcComponent->OnDialogueSessionEnded.AddDynamic(this, &AAINpcTestGameMode::OnNpcSessionEnded);
	NpcComponent->OnDialogueDegraded.AddDynamic(this, &AAINpcTestGameMode::OnNpcDegraded);

	FString ProviderFailureReason;
	if (!ValidateProviderConfiguration(*NpcComponent, ProviderFailureReason))
	{
		RecordFailure(ProviderFailureReason);
		return;
	}

	ShowStatus(TEXT("NPC and runtime SmartObject spawned. Starting real provider action verification in 3 seconds..."), FColor::Green, 15.0f);
	World->GetTimerManager().SetTimer(
		VerificationTimeoutTimerHandle,
		this,
		&AAINpcTestGameMode::HandleVerificationTimeout,
		VerificationTimeoutSeconds,
		false);
	World->GetTimerManager().SetTimer(
		InitialDialogueTimerHandle,
		this,
		&AAINpcTestGameMode::StartInitialDialogue,
		InitialDialogueDelaySeconds,
		false);
}

void AAINpcTestGameMode::ShowStatus(const FString& Message, const FColor& Color, const float DurationSeconds) const
{
	UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, DurationSeconds, Color, Message);
	}
}

void AAINpcTestGameMode::StartInitialDialogue()
{
	if (bTerminalOutcomeRecorded || !SpawnedNpc || !SpawnedNpc->NpcComponent)
	{
		return;
	}

	const TArray<FString> AvailableTargets = SpawnedNpc->NpcComponent->GetAvailableSmartObjectTargetsForExecution();
	if (AvailableTargets.IsEmpty())
	{
		RecordFailure(TEXT("Runtime SmartObject target list was empty, so the harness could not ask for a legal action."));
		return;
	}

	const FString RequiredTargetId = AvailableTargets[0];
	FString InitialPromptTemplate;
	FString TemplateFailureReason;
	if (!LoadInitialPromptTemplate(InitialPromptTemplate, TemplateFailureReason))
	{
		RecordFailure(TemplateFailureReason);
		return;
	}

	const FString InitialPrompt = InitialPromptTemplate.Replace(
		SmartObjectTargetIdPlaceholder,
		*RequiredTargetId,
		ESearchCase::CaseSensitive);

	ShowStatus(FString::Printf(TEXT(">>> Player: %s"), *InitialPrompt), FColor::Cyan, 10.0f);
	if (!SpawnedNpc->NpcComponent->StartDialogue(InitialPrompt))
	{
		RecordFailure(FString::Printf(
			TEXT("Initial StartDialogue call was rejected before any real provider response or action verification. %s"),
			*DescribeDialogueState(*SpawnedNpc->NpcComponent)));
	}
}

void AAINpcTestGameMode::HandleVerificationTimeout()
{
	if (bTerminalOutcomeRecorded || !SpawnedNpc || !SpawnedNpc->NpcComponent)
	{
		return;
	}

	RecordFailure(FString::Printf(
		TEXT("Timed out after %.1fs waiting for a real SmartObject action response and visible NPC movement. ActionObserved=%s DistanceToTarget=%.1f. %s"),
		VerificationTimeoutSeconds,
		bActionExecutionObserved ? TEXT("true") : TEXT("false"),
		SpawnedNpc->GetVisualActionTargetDistance(),
		*DescribeDialogueState(*SpawnedNpc->NpcComponent)));
}

void AAINpcTestGameMode::PollVisualVerification()
{
	if (bTerminalOutcomeRecorded || !SpawnedNpc)
	{
		return;
	}

	if (SpawnedNpc->HasReachedVisualActionTarget())
	{
		RecordSuccess();
	}
}

void AAINpcTestGameMode::RecordFailure(const FString& Reason)
{
	if (bTerminalOutcomeRecorded)
	{
		return;
	}

	bTerminalOutcomeRecorded = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(InitialDialogueTimerHandle);
		World->GetTimerManager().ClearTimer(VerificationTimeoutTimerHandle);
		World->GetTimerManager().ClearTimer(VisualVerificationTimerHandle);
	}

	ShowStatus(FString::Printf(TEXT("VISUAL QA FAILED: %s"), *Reason), FColor::Red, FailureExitDelaySeconds + 4.0f);
	UE_LOG(LogTemp, Error, TEXT("=== NPC Dialogue Visual QA FAILED ==="));
	UE_LOG(LogTemp, Error, TEXT("%s"), *Reason);

	if (SpawnedNpc && SpawnedNpc->NpcComponent && SpawnedNpc->NpcComponent->IsDialogueActive())
	{
		SpawnedNpc->NpcComponent->EndDialogue();
	}

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (USmartObjectBridgeContext* BridgeContext = GameInstance->GetSubsystem<USmartObjectBridgeContext>())
		{
			BridgeContext->ReleaseSlotForUser(SpawnedNpc);
		}
	}

	if (SpawnedSmartObject)
	{
		SpawnedSmartObject->SetInteractionState(false);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			ExitTimerHandle,
			this,
			&AAINpcTestGameMode::RequestHarnessExit,
			FailureExitDelaySeconds,
			false);
	}
}

void AAINpcTestGameMode::RecordSuccess()
{
	if (bTerminalOutcomeRecorded)
	{
		return;
	}

	bTerminalOutcomeRecorded = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(InitialDialogueTimerHandle);
		World->GetTimerManager().ClearTimer(VerificationTimeoutTimerHandle);
		World->GetTimerManager().ClearTimer(VisualVerificationTimerHandle);
	}

	UE_LOG(LogTemp, Warning, TEXT("=== NPC Action Visual QA VERIFIED WITH REAL PROVIDER ==="));
	ShowStatus(
		FString::Printf(
			TEXT("VISUAL QA VERIFIED: action target %s claimed/used and NPC reached the claimed slot."),
			*SpawnedNpc->GetVisualActionTargetId()),
		FColor::Green,
		SuccessExitDelaySeconds + 6.0f);

	if (SpawnedNpc && SpawnedNpc->NpcComponent && SpawnedNpc->NpcComponent->IsDialogueActive())
	{
		SpawnedNpc->NpcComponent->EndDialogue();
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			ExitTimerHandle,
			this,
			&AAINpcTestGameMode::RequestHarnessExit,
			SuccessExitDelaySeconds,
			false);
	}
}

void AAINpcTestGameMode::RequestHarnessExit()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (USmartObjectBridgeContext* BridgeContext = GameInstance->GetSubsystem<USmartObjectBridgeContext>())
		{
			BridgeContext->ReleaseSlotForUser(SpawnedNpc);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("=== AAINpcTestGameMode exiting after visual QA result ==="));
	FPlatformMisc::RequestExit(false);
}

bool AAINpcTestGameMode::ValidateProviderConfiguration(const UAINpcComponent& NpcComponent, FString& OutFailureReason) const
{
	FLLMRequest RequestConfig;
	FAINpcProviderConfigResolver::ApplyRequestConfig(NpcComponent, RequestConfig);
	RequestConfig.BaseUrl = RequestConfig.BaseUrl.TrimStartAndEnd();
	RequestConfig.ApiKey = RequestConfig.ApiKey.TrimStartAndEnd();
	RequestConfig.Model = RequestConfig.Model.TrimStartAndEnd();

	if (!RequestConfig.BaseUrl.IsEmpty())
	{
		return true;
	}

	OutFailureReason = FString::Printf(
		TEXT("No real provider endpoint resolved for the visual harness. Resolved BaseUrl is empty after applying the existing provider/baseUrl override chain. ModelLength=%d ApiKeyLength=%d."),
		RequestConfig.Model.Len(),
		RequestConfig.ApiKey.Len());
	return false;
}

FString AAINpcTestGameMode::DescribeDialogueState(const UAINpcComponent& NpcComponent) const
{
	const UEnum* const DialogueStateEnum = StaticEnum<ENpcDialogueState>();
	const FString DialogueState = DialogueStateEnum
		? DialogueStateEnum->GetValueAsString(NpcComponent.GetDialogueState())
		: TEXT("Unknown");

	return FString::Printf(
		TEXT("DialogueState=%s RequestInFlight=%s SessionActive=%s Queued=%s"),
		*DialogueState,
		NpcComponent.IsRequestInFlight() ? TEXT("true") : TEXT("false"),
		NpcComponent.IsDialogueActive() ? TEXT("true") : TEXT("false"),
		NpcComponent.IsDialogueRequestQueued() ? TEXT("true") : TEXT("false"));
}

void AAINpcTestGameMode::OnNpcSessionStarted()
{
	ShowStatus(TEXT("Dialogue session started through the real provider chain."), FColor::Green, 6.0f);
}

void AAINpcTestGameMode::OnNpcResponse(const FString& Text)
{
	ShowStatus(FString::Printf(TEXT("<<< NPC: %s"), *Text.Left(160)), FColor::Yellow, 8.0f);

	if (!SpawnedNpc || !SpawnedNpc->NpcComponent)
	{
		RecordFailure(TEXT("Harness lost its NPC reference before action verification could start."));
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	USmartObjectBridgeContext* BridgeContext = GameInstance ? GameInstance->GetSubsystem<USmartObjectBridgeContext>() : nullptr;
	if (!BridgeContext)
	{
		RecordFailure(TEXT("SmartObject bridge subsystem was unavailable during visual action verification."));
		return;
	}

	FAINpcSmartObjectRuntimeExecutionResult ExecutionResult;
	if (!FAINpcSmartObjectRuntimeExecutor::TryExecuteLatestActionIntent(
		*SpawnedNpc->NpcComponent,
		*SpawnedNpc,
		*BridgeContext,
		SmartObjectSearchRadius,
		SmartObjectClaimPriority,
		ExecutionResult))
	{
		RecordFailure(FString::Printf(
			TEXT("Real provider response did not lead to an executable SmartObject action. %s"),
			*ExecutionResult.FailureReason));
		return;
	}

	bActionExecutionObserved = true;
	if (SpawnedSmartObject)
	{
		SpawnedSmartObject->SetInteractionState(true);
	}

	SpawnedNpc->BeginVisualActionMove(ExecutionResult.ClaimedSlotTransform, ExecutionResult.RequestedTarget);
	ShowStatus(
		FString::Printf(
			TEXT("SmartObject action verified: %s -> %s. NPC is moving to the claimed slot."),
			*ExecutionResult.ActionType,
			*ExecutionResult.RequestedTarget),
		FColor::Green,
		8.0f);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			VisualVerificationTimerHandle,
			this,
			&AAINpcTestGameMode::PollVisualVerification,
			VisualVerificationPollSeconds,
			true);
	}
}

void AAINpcTestGameMode::OnNpcPartialResponse(const FString& Text)
{
	ShowStatus(FString::Printf(TEXT("[stream] %s"), *Text.Left(80)), FColor::Orange, 3.0f);
}

void AAINpcTestGameMode::OnNpcError(const FString& ErrorMessage)
{
	RecordFailure(FString::Printf(TEXT("Real provider chain reported an error: %s"), *ErrorMessage));
}

void AAINpcTestGameMode::OnNpcSessionEnded()
{
	ShowStatus(TEXT("Dialogue session ended."), FColor::Silver, 4.0f);
}

void AAINpcTestGameMode::OnNpcDegraded(const FString& FallbackResponse, const FString& FailureReason)
{
	RecordFailure(FString::Printf(
		TEXT("Dialogue fell back instead of producing a real provider response. FailureReason=%s Fallback=%s"),
		*FailureReason,
		*FallbackResponse.Left(160)));
}
