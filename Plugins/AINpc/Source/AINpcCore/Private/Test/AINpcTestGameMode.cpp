#include "Test/AINpcTestGameMode.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/AINpcComponent.h"
#include "Components/AINpcProviderConfigResolver.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "SmartObjectBridge/SmartObjectBridgeContext.h"
#include "Test/AINpcTestCharacter.h"
#include "Test/AINpcTestSmartObjectActor.h"
#include "Test/AINpcVisualTestRegistry.h"
#include "TimerManager.h"

namespace
{
	const float PollSeconds = 0.1f;
	const float SuccessExitDelaySeconds = 5.0f;
	const float FailureExitDelaySeconds = 8.0f;
	const float MaxVisibleHoldSeconds = 300.0f;
	const TCHAR* DefaultVisualTestId = TEXT("us1.dialogue-action");


	FString RedactSensitiveText(const FString& Input)
	{
		FString Redacted = Input;
		const TArray<FString> Keys = {
			TEXT("Authorization"),
			TEXT("x-api-key"),
			TEXT("apiKey"),
			TEXT("token"),
			TEXT("password"),
			TEXT("secret"),
			TEXT("bearer")
		};

		for (const FString& Key : Keys)
		{
			int32 SearchFrom = 0;
			while (SearchFrom < Redacted.Len())
			{
				const int32 KeyIndex = Redacted.Find(Key, ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchFrom);
				if (KeyIndex == INDEX_NONE)
				{
					break;
				}

				int32 SeparatorIndex = INDEX_NONE;
				const int32 MaxSeparatorScan = FMath::Min(Redacted.Len(), KeyIndex + Key.Len() + 16);
				for (int32 Index = KeyIndex + Key.Len(); Index < MaxSeparatorScan; ++Index)
				{
					const TCHAR Char = Redacted[Index];
					if (Char == TEXT(':') || Char == TEXT('='))
					{
						SeparatorIndex = Index;
						break;
					}
					if (!FChar::IsWhitespace(Char) && Char != TEXT('"') && Char != TEXT('\''))
					{
						break;
					}
				}

				int32 ValueStart = INDEX_NONE;
				if (SeparatorIndex == INDEX_NONE)
				{
					ValueStart = KeyIndex + Key.Len();
					while (ValueStart < Redacted.Len() && (FChar::IsWhitespace(Redacted[ValueStart]) || Redacted[ValueStart] == TEXT("-")[0] || Redacted[ValueStart] == TEXT("/")[0] || Redacted[ValueStart] == TCHAR(34) || Redacted[ValueStart] == TCHAR(39)))
					{
						++ValueStart;
					}
				}
				else
				{
					ValueStart = SeparatorIndex + 1;
				}
				while (ValueStart < Redacted.Len() && (FChar::IsWhitespace(Redacted[ValueStart]) || Redacted[ValueStart] == TEXT('"') || Redacted[ValueStart] == TEXT('\'')))
				{
					++ValueStart;
				}

				if (Redacted.Mid(ValueStart, 6).Equals(TEXT("Bearer"), ESearchCase::IgnoreCase))
				{
					ValueStart += 6;
					while (ValueStart < Redacted.Len() && FChar::IsWhitespace(Redacted[ValueStart]))
					{
						++ValueStart;
					}
				}

				int32 ValueEnd = ValueStart;
				while (ValueEnd < Redacted.Len())
				{
					const TCHAR Char = Redacted[ValueEnd];
					if (FChar::IsWhitespace(Char) || Char == TEXT(',') || Char == TEXT(';') || Char == TEXT('}') || Char == TEXT(']') || Char == TEXT('"') || Char == TEXT('\''))
					{
						break;
					}
					++ValueEnd;
				}

				if (ValueEnd > ValueStart)
				{
					Redacted.RemoveAt(ValueStart, ValueEnd - ValueStart, EAllowShrinking::No);
					Redacted.InsertAt(ValueStart, TEXT("<redacted>"));
					SearchFrom = ValueStart + 10;
				}
				else
				{
					SearchFrom = ValueStart + 1;
				}
			}
		}

		int32 BearerIndex = 0;
		while ((BearerIndex = Redacted.Find(TEXT("Bearer "), ESearchCase::IgnoreCase, ESearchDir::FromStart, BearerIndex)) != INDEX_NONE)
		{
			const int32 ValueStart = BearerIndex + 7;
			int32 ValueEnd = ValueStart;
			while (ValueEnd < Redacted.Len())
			{
				const TCHAR Char = Redacted[ValueEnd];
				if (FChar::IsWhitespace(Char) || Char == TEXT(',') || Char == TEXT(';') || Char == TEXT('}') || Char == TEXT(']') || Char == TEXT('"') || Char == TEXT('\''))
				{
					break;
				}
				++ValueEnd;
			}
			if (ValueEnd <= ValueStart)
			{
				BearerIndex = ValueStart + 1;
				continue;
			}
			Redacted.RemoveAt(ValueStart, ValueEnd - ValueStart, EAllowShrinking::No);
			Redacted.InsertAt(ValueStart, TEXT("<redacted>"));
			BearerIndex = ValueStart + 10;
		}

		return Redacted;
	}


	FString ToIsoUtc(const FDateTime& Time)
	{
		return Time.ToIso8601();
	}

	TArray<TSharedPtr<FJsonValue>> StringsToJsonArray(const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> JsonValues;
		for (const FString& Value : Values)
		{
			JsonValues.Add(MakeShared<FJsonValueString>(Value));
		}
		return JsonValues;
	}

	TSharedPtr<FJsonObject> BuildObservationJson(const FAINpcVisualTestObservations& Observations)
	{
		TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		for (const TPair<FString, bool>& Field : Observations.BooleanFields)
		{
			Json->SetBoolField(Field.Key, Field.Value);
		}
		for (const TPair<FString, int32>& Field : Observations.IntegerFields)
		{
			Json->SetNumberField(Field.Key, Field.Value);
		}
		for (const TPair<FString, double>& Field : Observations.NumberFields)
		{
			Json->SetNumberField(Field.Key, Field.Value);
		}
		for (const TPair<FString, FString>& Field : Observations.StringFields)
		{
			Json->SetStringField(Field.Key, RedactSensitiveText(Field.Value));
		}
		return Json;
	}

	float GetVisibleHoldSeconds(const float DefaultDelaySeconds)
	{
		float RequestedDelaySeconds = DefaultDelaySeconds;
		if (FParse::Value(FCommandLine::Get(), TEXT("AINpcVisualHoldSeconds="), RequestedDelaySeconds))
		{
			return FMath::Clamp(RequestedDelaySeconds, 0.0f, MaxVisibleHoldSeconds);
		}

		return DefaultDelaySeconds;
	}

	FString ResolveVisualLogPath()
	{
		FString RequestedPath;
		if (FParse::Value(FCommandLine::Get(), TEXT("AbsLog="), RequestedPath) && !RequestedPath.IsEmpty())
		{
			return FPaths::ConvertRelativePathToFull(RequestedPath);
		}

		return FPaths::Combine(FPaths::ProjectLogDir(), TEXT("VerifierHost.log"));
	}
}

AAINpcTestGameMode::AAINpcTestGameMode()
{
	DefaultPawnClass = ADefaultPawn::StaticClass();
}

void AAINpcTestGameMode::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogTemp, Warning, TEXT("=== AAINpcTestGameMode::BeginPlay CALLED ==="));
	VisualTestId = DefaultVisualTestId;
	VisualStartTimeUtc = FDateTime::UtcNow();
	VisualRunId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	FParse::Value(FCommandLine::Get(), TEXT("AINpcVisualTestId="), VisualTestId);
	VisualResultPath = ResolveVisualResultPath();
	UE_LOG(LogTemp, Warning, TEXT("=== AINpc Visual Test START: %s ResultPath=%s ==="), *VisualTestId, *VisualResultPath);

	StartHarness();
}

AActor* AAINpcTestGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	return nullptr;
}

void AAINpcTestGameMode::StartHarness()
{
	StartSelectedTest();
}

bool AAINpcTestGameMode::SpawnFixture(const EAINpcVisualTestFixtureKind FixtureKind, FString& OutFailureReason)
{
	SpawnedSmartObject = nullptr;

	if (!SpawnNpc(OutFailureReason))
	{
		return false;
	}

	switch (FixtureKind)
	{
	case EAINpcVisualTestFixtureKind::NpcOnly:
		break;

	case EAINpcVisualTestFixtureKind::NpcWithSmartObject:
		if (!SpawnSmartObject(OutFailureReason))
		{
			return false;
		}
		break;
	}

	if (!PositionObserverCamera(OutFailureReason))
	{
		return false;
	}

	UE_LOG(LogTemp, Warning, TEXT("=== Visual fixture spawned. TestId=%s Npc=%s SmartObject=%s ==="), *VisualTestId, *SpawnedNpc->GetName(), SpawnedSmartObject ? *SpawnedSmartObject->GetName() : TEXT("not requested"));
	return true;
}

bool AAINpcTestGameMode::SpawnNpc(FString& OutFailureReason)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		OutFailureReason = TEXT("Visual harness failed because World is null.");
		return false;
	}

	const FVector SpawnLocation(500.0f, 0.0f, 100.0f);
	FActorSpawnParameters NpcParams;
	NpcParams.Name = TEXT("AutoTestNpc");
	NpcParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	SpawnedNpc = World->SpawnActor<AAINpcTestCharacter>(AAINpcTestCharacter::StaticClass(), SpawnLocation, FRotator::ZeroRotator, NpcParams);
	if (!SpawnedNpc || !SpawnedNpc->NpcComponent)
	{
		OutFailureReason = TEXT("Failed to spawn NPC test character or its UAINpcComponent is null.");
		return false;
	}
	if (!SpawnedNpc->HasValidVisualMeshAndAnimation())
	{
		OutFailureReason = TEXT("Visual NPC is invalid because mannequin mesh or animation blueprint is missing.");
		return false;
	}

	return true;
}

bool AAINpcTestGameMode::SpawnSmartObject(FString& OutFailureReason)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		OutFailureReason = TEXT("Visual harness failed because World is null.");
		return false;
	}

	const FVector SmartObjectLocation(900.0f, 0.0f, 40.0f);
	FActorSpawnParameters SmartObjectParams;
	SmartObjectParams.Name = TEXT("AutoTestSmartObject");
	SmartObjectParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	SpawnedSmartObject = World->SpawnActor<AAINpcTestSmartObjectActor>(AAINpcTestSmartObjectActor::StaticClass(), SmartObjectLocation, FRotator::ZeroRotator, SmartObjectParams);
	if (!SpawnedSmartObject)
	{
		OutFailureReason = TEXT("Failed to spawn runtime SmartObject actor for the visual harness.");
		return false;
	}

	return true;
}

bool AAINpcTestGameMode::PositionObserverCamera(FString& OutFailureReason)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		OutFailureReason = TEXT("Visual harness could not find a PlayerController for the observer camera.");
		return false;
	}

	APawn* ObserverPawn = PC->GetPawn();
	if (!ObserverPawn)
	{
		OutFailureReason = TEXT("Visual harness PlayerController has no observer pawn.");
		return false;
	}

	if (ObserverPawn == SpawnedNpc)
	{
		OutFailureReason = TEXT("Observer pawn incorrectly resolved to the spawned NPC.");
		return false;
	}

	const FVector NpcLocation = SpawnedNpc->GetActorLocation();
	const FVector FocusBase = SpawnedSmartObject ? (NpcLocation + SpawnedSmartObject->GetActorLocation()) * 0.5f : NpcLocation;
	const FVector FocusPoint = FocusBase + FVector(0.0f, 0.0f, 90.0f);
	const FVector CameraLoc = NpcLocation + FVector(-520.0f, -360.0f, 180.0f);
	const FRotator CameraRot = (FocusPoint - CameraLoc).Rotation();
	ObserverPawn->SetActorLocation(CameraLoc);
	ObserverPawn->SetActorRotation(CameraRot);
	PC->SetControlRotation(CameraRot);
	PC->SetViewTarget(ObserverPawn);

	UE_LOG(LogTemp, Warning, TEXT("=== Observer camera active. ObserverPawn=%s Npc=%s SmartObject=%s Camera=%s Focus=%s ==="), *ObserverPawn->GetName(), *SpawnedNpc->GetName(), SpawnedSmartObject ? *SpawnedSmartObject->GetName() : TEXT("None"), *CameraLoc.ToString(), *FocusPoint.ToString());
	return true;
}

void AAINpcTestGameMode::StartSelectedTest()
{
	ActiveDescriptor = FAINpcVisualTestRegistry::Find(VisualTestId);
	const FAINpcVisualTestDescriptor* Descriptor = ActiveDescriptor;
	if (!Descriptor || !Descriptor->CreateTest)
	{
		RecordFailure(FString::Printf(TEXT("Unknown AINpc visual TestId '%s'. Registered TestIds: %s."), *VisualTestId, *FAINpcVisualTestRegistry::GetRegisteredTestIds()));
		return;
	}

	FString FixtureFailureReason;
	if (!SpawnFixture(Descriptor->FixtureKind, FixtureFailureReason))
	{
		RecordFailure(FixtureFailureReason);
		return;
	}

	FAINpcVisualTestFixture Fixture{SpawnedNpc, SpawnedSmartObject};
	FAINpcVisualTestContext Context{Fixture, VisualTestId};
	ActiveTest = Descriptor->CreateTest(Context);
	if (!ActiveTest)
	{
		RecordFailure(FString::Printf(TEXT("AINpc visual TestId '%s' factory returned null."), *VisualTestId));
		return;
	}
	FString FailureReason;
	if (!ActiveTest->Start(FailureReason))
	{
		RecordFailure(FailureReason);
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(PollTimerHandle, this, &AAINpcTestGameMode::PollActiveTest, PollSeconds, true);
	}
}

void AAINpcTestGameMode::PollActiveTest()
{
	if (bTerminalOutcomeRecorded || !ActiveTest)
	{
		return;
	}

	ActiveTest->Poll();
	if (ActiveTest->HasFailed())
	{
		RecordFailure(ActiveTest->GetFailureReason());
		return;
	}
	if (ActiveTest->IsComplete())
	{
		RecordSuccess(ActiveTest->BuildSummary());
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
		World->GetTimerManager().ClearTimer(PollTimerHandle);
	}

	ShowStatus(FString::Printf(TEXT("VISUAL QA FAILED: %s"), *Reason), FColor::Red, FailureExitDelaySeconds + 4.0f);
	UE_LOG(LogTemp, Error, TEXT("=== AINpc Visual Test FAIL: %s ==="), *VisualTestId);
	const FString Summary = ActiveTest ? ActiveTest->BuildSummary() : FString();
	UE_LOG(LogTemp, Error, TEXT("AINpc visual test summary: %s Result=FAIL Reason=%s %s"), *VisualTestId, *Reason, *Summary);
	WriteVisualResult(TEXT("FAIL"), TEXT("failure"), Reason, Summary);

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

	ActiveTest.Reset();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(ExitTimerHandle, this, &AAINpcTestGameMode::RequestHarnessExit, FailureExitDelaySeconds, false);
	}
}

void AAINpcTestGameMode::RecordSuccess(const FString& Summary)
{
	if (bTerminalOutcomeRecorded)
	{
		return;
	}

	bTerminalOutcomeRecorded = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PollTimerHandle);
	}

	UE_LOG(LogTemp, Warning, TEXT("=== AINpc Visual Test PASS: %s ==="), *VisualTestId);
	UE_LOG(LogTemp, Warning, TEXT("AINpc visual test summary: %s Result=PASS %s"), *VisualTestId, *Summary);
	WriteVisualResult(TEXT("PASS"), TEXT("success"), FString(), Summary);
	ShowStatus(FString::Printf(TEXT("AINpc visual test PASS: %s"), *VisualTestId), FColor::Green, GetVisibleHoldSeconds(SuccessExitDelaySeconds) + 6.0f);

	if (SpawnedNpc && SpawnedNpc->NpcComponent && SpawnedNpc->NpcComponent->IsDialogueActive())
	{
		SpawnedNpc->NpcComponent->EndDialogue();
	}

	ActiveTest.Reset();
	if (UWorld* World = GetWorld())
	{
		const float VisibleHoldSeconds = GetVisibleHoldSeconds(SuccessExitDelaySeconds);
		UE_LOG(LogTemp, Warning, TEXT("=== AINpc visual harness will hold success window for %.1f seconds before exit ==="), VisibleHoldSeconds);
		World->GetTimerManager().SetTimer(ExitTimerHandle, this, &AAINpcTestGameMode::RequestHarnessExit, VisibleHoldSeconds, false);
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

void AAINpcTestGameMode::ShowStatus(const FString& Message, const FColor& Color, const float DurationSeconds) const
{
	UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, DurationSeconds, Color, Message);
	}
}

FString AAINpcTestGameMode::ResolveVisualResultPath() const
{
	FString RequestedPath;
	if (FParse::Value(FCommandLine::Get(), TEXT("AINpcVisualResultPath="), RequestedPath) && !RequestedPath.IsEmpty())
	{
		return FPaths::ConvertRelativePathToFull(RequestedPath);
	}

	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TestLogs"), TEXT("visual-game"), VisualRunId, TEXT("result.json"));
}

void AAINpcTestGameMode::WriteVisualResult(const FString& Status, const FString& ExitReason, const FString& FailureReason, const FString& DiagnosticSummary)
{
	const FDateTime EndTimeUtc = FDateTime::UtcNow();
	const FTimespan Duration = EndTimeUtc - VisualStartTimeUtc;

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schemaVersion"), 1);
	Root->SetStringField(TEXT("runId"), VisualRunId);
	Root->SetStringField(TEXT("layer"), TEXT("visual-game"));
	Root->SetStringField(TEXT("testId"), VisualTestId);
	Root->SetArrayField(TEXT("storyIds"), ActiveDescriptor ? StringsToJsonArray(ActiveDescriptor->StoryIds) : TArray<TSharedPtr<FJsonValue>>());
	Root->SetArrayField(TEXT("phaseIds"), ActiveDescriptor ? StringsToJsonArray(ActiveDescriptor->PhaseIds) : TArray<TSharedPtr<FJsonValue>>());
	Root->SetStringField(TEXT("status"), Status);
	Root->SetStringField(TEXT("startTimeUtc"), ToIsoUtc(VisualStartTimeUtc));
	Root->SetStringField(TEXT("endTimeUtc"), ToIsoUtc(EndTimeUtc));
	Root->SetNumberField(TEXT("durationMs"), Duration.GetTotalMilliseconds());

	TSharedPtr<FJsonObject> Command = MakeShared<FJsonObject>();
	Command->SetStringField(TEXT("executable"), FPlatformProcess::ExecutablePath());
	Command->SetNumberField(TEXT("processId"), static_cast<double>(FPlatformProcess::GetCurrentProcessId()));
	Command->SetStringField(TEXT("workingDirectory"), FPaths::LaunchDir());
	Command->SetStringField(TEXT("map"), GetWorld() ? GetWorld()->GetMapName() : FString());
	Command->SetStringField(TEXT("testId"), VisualTestId);
	Command->SetStringField(TEXT("resultPath"), VisualResultPath);
	Command->SetStringField(TEXT("arguments"), RedactSensitiveText(FCommandLine::Get()));
	Command->SetStringField(TEXT("display"), RedactSensitiveText(FString::Printf(TEXT("%s %s"), FPlatformProcess::ExecutablePath(), FCommandLine::Get())));
	Root->SetObjectField(TEXT("command"), Command);

	TSharedPtr<FJsonObject> Artifacts = MakeShared<FJsonObject>();
	Artifacts->SetStringField(TEXT("resultPath"), VisualResultPath);
	Artifacts->SetStringField(TEXT("logPath"), ResolveVisualLogPath());
	Root->SetObjectField(TEXT("artifacts"), Artifacts);

	const FAINpcVisualTestObservations Observations = ActiveTest ? ActiveTest->BuildObservations() : FAINpcVisualTestObservations();
	TSharedPtr<FJsonObject> ProviderEvidence = MakeShared<FJsonObject>();
	if (SpawnedNpc && SpawnedNpc->NpcComponent)
	{
		const FAINpcProviderBootstrapConfig ProviderConfig = FAINpcProviderConfigResolver::ResolveBootstrapConfig(*SpawnedNpc->NpcComponent);
		ProviderEvidence->SetStringField(TEXT("source"), TEXT("Config/AINpcLocalProvider.json via FAINpcProviderConfigResolver"));
		ProviderEvidence->SetStringField(TEXT("provider"), ProviderConfig.ProviderType);
		ProviderEvidence->SetStringField(TEXT("baseUrl"), ProviderConfig.BaseUrl.IsEmpty() ? TEXT("missing") : TEXT("present"));
		ProviderEvidence->SetStringField(TEXT("model"), ProviderConfig.Model);
		ProviderEvidence->SetStringField(TEXT("effortLevel"), ProviderConfig.EffortLevel);
		ProviderEvidence->SetBoolField(TEXT("apiKeyPresent"), !ProviderConfig.ApiKey.IsEmpty());
	}
	TSharedPtr<FJsonObject> RuntimeObservationSummary = MakeShared<FJsonObject>();
	RuntimeObservationSummary->SetBoolField(TEXT("sessionStartedObserved"), Observations.BooleanFields.Contains(TEXT("sessionStarted")) && Observations.BooleanFields[TEXT("sessionStarted")]);
	RuntimeObservationSummary->SetBoolField(TEXT("responseObserved"), Observations.BooleanFields.Contains(TEXT("dialogueResponseObserved")) && Observations.BooleanFields[TEXT("dialogueResponseObserved")]);
	RuntimeObservationSummary->SetStringField(TEXT("statusSummary"), Status);
	Root->SetObjectField(TEXT("providerIdentity"), ProviderEvidence);
	Root->SetObjectField(TEXT("runtimeObservationSummary"), RuntimeObservationSummary);
	TSharedPtr<FJsonObject> VisibleGuardrail = MakeShared<FJsonObject>();
	VisibleGuardrail->SetStringField(TEXT("executable"), FPaths::GetCleanFilename(FPlatformProcess::ExecutablePath()));
	VisibleGuardrail->SetBoolField(TEXT("visibleGameLaunchRequired"), true);
	VisibleGuardrail->SetBoolField(TEXT("runtimeResultIsFinalAcceptanceCandidate"), Status == TEXT("PASS"));
	Root->SetObjectField(TEXT("visibleLaunchGuardrail"), VisibleGuardrail);

	TSharedPtr<FJsonObject> Diagnostics = MakeShared<FJsonObject>();
	Diagnostics->SetStringField(TEXT("exitReason"), ExitReason);
	Diagnostics->SetStringField(TEXT("summary"), RedactSensitiveText(DiagnosticSummary));
	Diagnostics->SetStringField(TEXT("map"), GetWorld() ? GetWorld()->GetMapName() : FString());
	Diagnostics->SetStringField(TEXT("resultPath"), VisualResultPath);
	Root->SetObjectField(TEXT("diagnostics"), Diagnostics);

	Root->SetObjectField(TEXT("observations"), BuildObservationJson(Observations));

	TArray<TSharedPtr<FJsonValue>> Failures;
	if (!FailureReason.IsEmpty())
	{
		TSharedPtr<FJsonObject> Failure = MakeShared<FJsonObject>();
		Failure->SetStringField(TEXT("id"), VisualTestId);
		Failure->SetStringField(TEXT("message"), RedactSensitiveText(FailureReason));
		Failure->SetStringField(TEXT("artifact"), VisualResultPath);
		Failures.Add(MakeShared<FJsonValueObject>(Failure));
	}
	Root->SetArrayField(TEXT("failures"), Failures);

	FString JsonText;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to serialize AINpc visual result JSON for %s."), *VisualTestId);
		return;
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(VisualResultPath), true);
	if (!FFileHelper::SaveStringToFile(JsonText, *VisualResultPath))
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to write AINpc visual result JSON to %s."), *VisualResultPath);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("AINpc visual result JSON written: %s"), *VisualResultPath);
}
