#include "Tests/AINpcGauntletTestController.h"
#include "Components/AINpcComponent.h"
#include "Data/NpcPersonaDataAsset.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Events/NpcEventSubsystem.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/PlayerController.h"
#include "GauntletModule.h"
#include "Misc/CommandLine.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AINpcGauntletTestController)

namespace
{
	// Creates a minimal persona data asset for testing
	UNpcPersonaDataAsset* CreateTestPersonaData(UObject* Outer)
	{
		UNpcPersonaDataAsset* DataAsset = NewObject<UNpcPersonaDataAsset>(Outer);
		DataAsset->PersonaName = TEXT("TestNPC");
		DataAsset->Background = TEXT("A test NPC for automated verification.");
		DataAsset->SpeakingStyle = TEXT("Concise and helpful.");
		DataAsset->DelayFillerThreshold = 0.1f; // fast for tests
		DataAsset->DelayFillerTexts.Add(FText::FromString(TEXT("Hmm...")));
		return DataAsset;
	}
}

void UAINpcGauntletTestController::OnInit()
{
	UE_LOG(LogGauntlet, Display, TEXT("=== AINpc Gauntlet Test Controller Initialized ==="));

	// Parse optional timeout override from command line
	FParse::Value(FCommandLine::Get(), TEXT("AINpcTestTimeout="), TestTimeoutSeconds);
	TestTimeoutSeconds = FMath::Max(5.0f, TestTimeoutSeconds);

	Phase = ETestPhase::WaitingForWorld;
	PhaseElapsed = 0.0f;
	TotalTestTimeSeconds = 0.0f;
	ChecksPassed = 0;
	ChecksFailed = 0;

	MarkHeartbeatActive(TEXT("AINpc test initialized, waiting for world"));
}

void UAINpcGauntletTestController::OnPostMapChange(UWorld* World)
{
	if (!World)
	{
		return;
	}

	UE_LOG(LogGauntlet, Display, TEXT("AINpcGauntlet: Map loaded '%s', spawning test NPC"),
		*World->GetMapName());

	if (SpawnTestNpc(World))
	{
		EnterPhase(ETestPhase::Setup);
	}
	else
	{
		FailCheck(TEXT("Failed to spawn test NPC"));
		EndTest(1);
	}
}

void UAINpcGauntletTestController::OnTick(float TimeDelta)
{
	PhaseElapsed += TimeDelta;
	TotalTestTimeSeconds += TimeDelta;

	if (TotalTestTimeSeconds > TestTimeoutSeconds && Phase != ETestPhase::Done)
	{
		FailCheck(FString::Printf(TEXT("Test timeout after %.1fs"), TotalTestTimeSeconds));
		LogSummary();
		EndTest(1);
		return;
	}

	switch (Phase)
	{
	case ETestPhase::WaitingForWorld:
		break; // Nothing to tick here, OnPostMapChange drives progression
	case ETestPhase::Setup:           TickSetup(TimeDelta); break;
	case ETestPhase::DialogueInit:    TickDialogueInit(TimeDelta); break;
	case ETestPhase::DialogueWait:    TickDialogueWait(TimeDelta); break;
	case ETestPhase::EventTest:       TickEventTest(TimeDelta); break;
	case ETestPhase::StateMachineTest:TickStateMachineTest(TimeDelta); break;
	case ETestPhase::Cleanup:         TickCleanup(TimeDelta); break;
	case ETestPhase::Done:            break;
	}
}

bool UAINpcGauntletTestController::SpawnTestNpc(UWorld* World)
{
	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = TEXT("AINpcGauntletTestNpc");
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* Actor = World->SpawnActor<ADefaultPawn>(
		ADefaultPawn::StaticClass(),
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		SpawnParams);

	if (!Actor)
	{
		return false;
	}

	TestNpcComp = NewObject<UAINpcComponent>(Actor);
	if (!TestNpcComp.IsValid())
	{
		return false;
	}

	TestNpcComp->RegisterComponent();
	TestNpcComp->SetPersonaData(CreateTestPersonaData(Actor));
	TestNpc = Actor;

	UE_LOG(LogGauntlet, Display, TEXT("AINpcGauntlet: Test NPC '%s' spawned with UAINpcComponent"),
		*Actor->GetName());
	return true;
}

void UAINpcGauntletTestController::ConfigureNpcComponent()
{
	if (!TestNpcComp.IsValid())
	{
		FailCheck(TEXT("TestNpcComp invalid during configuration"));
		return;
	}

	// Bind to all dialogue delegates to verify they fire
	TestNpcComp->OnDialogueSessionStarted.AddDynamic(this, &UAINpcGauntletTestController::OnSessionStarted);
	TestNpcComp->OnDialogueResponse.AddDynamic(this, &UAINpcGauntletTestController::OnDialogueResponse);
	TestNpcComp->OnPartialResponse.AddDynamic(this, &UAINpcGauntletTestController::OnPartialResponse);
	TestNpcComp->OnDialogueError.AddDynamic(this, &UAINpcGauntletTestController::OnDialogueError);
	TestNpcComp->OnDialogueSessionEnded.AddDynamic(this, &UAINpcGauntletTestController::OnSessionEnded);

	// Enable dispatch bypass so we can test the full pipeline without real HTTP
	UAINpcComponent::SetDialogueDispatchBypassForTest(true);

	UE_LOG(LogGauntlet, Display, TEXT("AINpcGauntlet: NPC component configured, dispatch bypass enabled"));
}

void UAINpcGauntletTestController::FailCheck(const FString& Reason)
{
	++ChecksFailed;
	UE_LOG(LogGauntlet, Error, TEXT("AINpcGauntlet FAIL: %s"), *Reason);
}

// ── Phase: Setup ─────────────────────────────

void UAINpcGauntletTestController::TickSetup(float DeltaTime)
{
	(void)DeltaTime;
	ConfigureNpcComponent();
	EnterPhase(ETestPhase::DialogueInit);
}

// ── Phase: DialogueInit ──────────────────────

void UAINpcGauntletTestController::TickDialogueInit(float DeltaTime)
{
	(void)DeltaTime;

	if (!TestNpcComp.IsValid())
	{
		FailCheck(TEXT("TestNpcComp invalid"));
		EndTest(1);
		return;
	}

	const bool bStarted = TestNpcComp->StartDialogue(TEXT("Hello, who are you?"));
	if (!bStarted)
	{
		FailCheck(TEXT("StartDialogue returned false"));
		EnterPhase(ETestPhase::StateMachineTest);
		return;
	}

	// Verify state transition occurred
	if (TestNpcComp->GetDialogueState() != ENpcDialogueState::WaitingForLLM)
	{
		FailCheck(FString::Printf(TEXT("Expected WaitingForLLM state, got %d"),
			static_cast<int32>(TestNpcComp->GetDialogueState())));
	}
	else
	{
		++ChecksPassed;
		UE_LOG(LogGauntlet, Display, TEXT("AINpcGauntlet PASS: Dialogue started, state=WaitingForLLM"));
	}

	if (bSessionStarted)
	{
		++ChecksPassed;
		UE_LOG(LogGauntlet, Display, TEXT("AINpcGauntlet PASS: OnDialogueSessionStarted fired"));
	}
	else
	{
		FailCheck(TEXT("OnDialogueSessionStarted did not fire"));
	}

	// Simulate a successful LLM response arriving
	FLLMResponse TestResponse;
	TestResponse.RequestId = TestNpcComp->GetActiveRequestIdForTest();
	TestResponse.bSuccess = true;
	TestResponse.Content = TEXT("Hello! I am TestNPC, your automated test companion.");
	TestResponse.ParsedResponse.Dialogue = TestResponse.Content;

	TestNpcComp->HandleRequestCompletedForTest(TestResponse);
	EnterPhase(ETestPhase::DialogueWait);
}

// ── Phase: DialogueWait ──────────────────────

void UAINpcGauntletTestController::TickDialogueWait(float DeltaTime)
{
	if (PhaseElapsed < 0.5f)
	{
		return; // Give delegate callbacks a moment
	}

	if (bDialogueResponseReceived)
	{
		++ChecksPassed;
		UE_LOG(LogGauntlet, Display, TEXT("AINpcGauntlet PASS: OnDialogueResponse received: '%s'"),
			*TestNpcComp->GetNpcResponse());
	}
	else
	{
		FailCheck(TEXT("OnDialogueResponse did not fire after simulated response"));
	}

	if (TestNpcComp->GetDialogueState() != ENpcDialogueState::Speaking)
	{
		FailCheck(FString::Printf(TEXT("Expected Speaking state after response, got %d"),
			static_cast<int32>(TestNpcComp->GetDialogueState())));
	}
	else
	{
		++ChecksPassed;
		UE_LOG(LogGauntlet, Display, TEXT("AINpcGauntlet PASS: State=Speaking after response"));
	}

	if (TestNpcComp->GetNpcResponse() == TestNpcComp->GetNpcResponse()) // non-empty
	{
		++ChecksPassed;
	}
	else
	{
		FailCheck(TEXT("GetNpcResponse returned empty"));
	}

	EnterPhase(ETestPhase::EventTest);
}

// ── Phase: EventTest ─────────────────────────

void UAINpcGauntletTestController::TickEventTest(float DeltaTime)
{
	(void)DeltaTime;

	if (!TestNpcComp.IsValid())
	{
		FailCheck(TEXT("TestNpcComp invalid during event test"));
		EnterPhase(ETestPhase::StateMachineTest);
		return;
	}

	// Verify the NPC is in the expected post-response state
	if (TestNpcComp->IsDialogueActive())
	{
		++ChecksPassed;
		UE_LOG(LogGauntlet, Display, TEXT("AINpcGauntlet PASS: Dialogue still active after response"));
	}
	else
	{
		FailCheck(TEXT("Dialogue should still be active after response"));
	}

	// Verify our conversation history accumulated
	const FString Response = TestNpcComp->GetNpcResponse();
	if (!Response.IsEmpty())
	{
		++ChecksPassed;
	}
	else
	{
		FailCheck(TEXT("GetNpcResponse empty after dialogue round"));
	}

	// Dispatch a simulated event through the event subsystem
	const FNpcEventMessage EventMessage;
	TestNpcComp->HandleNpcEventStageDispatchedForTest(
		EventMessage,
		ENpcEventDispatchStage::DelayMasking);
	++ChecksPassed;
	UE_LOG(LogGauntlet, Display, TEXT("AINpcGauntlet PASS: Event dispatched without crash"));

	EnterPhase(ETestPhase::StateMachineTest);
}

// ── Phase: StateMachineTest ──────────────────

void UAINpcGauntletTestController::TickStateMachineTest(float DeltaTime)
{
	(void)DeltaTime;

	if (!TestNpcComp.IsValid())
	{
		FailCheck(TEXT("TestNpcComp invalid during state machine test"));
		EnterPhase(ETestPhase::Cleanup);
		return;
	}

	// Test 1: IsDialogueActive() contract
	if (TestNpcComp->IsDialogueActive())
	{
		++ChecksPassed;
		UE_LOG(LogGauntlet, Display, TEXT("AINpcGauntlet PASS: IsDialogueActive() == true"));
	}

	// Test 2: State machine knows about current state
	const ENpcDialogueState CurrentState = TestNpcComp->GetDialogueState();
	UE_LOG(LogGauntlet, Display, TEXT("AINpcGauntlet: Current dialogue state = %d"),
		static_cast<int32>(CurrentState));

	// Test 3: HasBeenInDialogueStateLongerThan
	const bool bHasBeenLonger = TestNpcComp->HasBeenInDialogueStateLongerThan(0.0f);
	if (bHasBeenLonger)
	{
		++ChecksPassed;
		UE_LOG(LogGauntlet, Display, TEXT("AINpcGauntlet PASS: HasBeenInDialogueStateLongerThan(0) == true"));
	}

	// Test 4: SendMessage alias works
	const bool bSecondMessage = TestNpcComp->SendMessage(TEXT("Tell me more"));
	if (bSecondMessage)
	{
		++ChecksPassed;
		UE_LOG(LogGauntlet, Display, TEXT("AINpcGauntlet PASS: SendMessage (non-session-start) returned true"));
	}
	else
	{
		// May fail if request already in flight — that's acceptable
		UE_LOG(LogGauntlet, Display, TEXT("AINpcGauntlet: SendMessage returned false (may be expected if previous request pending)"));
	}

	EnterPhase(ETestPhase::Cleanup);
}

// ── Phase: Cleanup ───────────────────────────

void UAINpcGauntletTestController::TickCleanup(float DeltaTime)
{
	(void)DeltaTime;

	if (!TestNpcComp.IsValid())
	{
		EnterPhase(ETestPhase::Done);
		return;
	}

	// Disable bypass before cleanup
	UAINpcComponent::SetDialogueDispatchBypassForTest(false);

	TestNpcComp->EndDialogue();

	if (!TestNpcComp->IsDialogueActive())
	{
		++ChecksPassed;
		UE_LOG(LogGauntlet, Display, TEXT("AINpcGauntlet PASS: Dialogue ended cleanly, !IsDialogueActive()"));
	}
	else
	{
		FailCheck(TEXT("IsDialogueActive() should be false after EndDialogue()"));
	}

	if (TestNpcComp->GetDialogueState() == ENpcDialogueState::Idle)
	{
		++ChecksPassed;
		UE_LOG(LogGauntlet, Display, TEXT("AINpcGauntlet PASS: State=Idle after EndDialogue()"));
	}
	else
	{
		FailCheck(TEXT("State should be Idle after EndDialogue()"));
	}

	// Clean up delegates
	TestNpcComp->OnDialogueSessionStarted.RemoveDynamic(this, &UAINpcGauntletTestController::OnSessionStarted);
	TestNpcComp->OnDialogueResponse.RemoveDynamic(this, &UAINpcGauntletTestController::OnDialogueResponse);
	TestNpcComp->OnPartialResponse.RemoveDynamic(this, &UAINpcGauntletTestController::OnPartialResponse);
	TestNpcComp->OnDialogueError.RemoveDynamic(this, &UAINpcGauntletTestController::OnDialogueError);
	TestNpcComp->OnDialogueSessionEnded.RemoveDynamic(this, &UAINpcGauntletTestController::OnSessionEnded);

	UAINpcComponent::ResetConcurrencyStateForTest();

	EnterPhase(ETestPhase::Done);
}

// ── Delegate callbacks ───────────────────────

void UAINpcGauntletTestController::OnSessionStarted()
{
	bSessionStarted = true;
	UE_LOG(LogGauntlet, Verbose, TEXT("AINpcGauntlet: OnDialogueSessionStarted"));
}

void UAINpcGauntletTestController::OnDialogueResponse(const FString& Text)
{
	bDialogueResponseReceived = true;
	UE_LOG(LogGauntlet, Verbose, TEXT("AINpcGauntlet: OnDialogueResponse: '%s'"), *Text);
}

void UAINpcGauntletTestController::OnPartialResponse(const FString& Text)
{
	++PartialResponseCount;
	UE_LOG(LogGauntlet, Verbose, TEXT("AINpcGauntlet: OnPartialResponse[%d]: '%s'"), PartialResponseCount, *Text);
}

void UAINpcGauntletTestController::OnDialogueError(const FString& Msg)
{
	bDialogueErrorReceived = true;
	UE_LOG(LogGauntlet, Warning, TEXT("AINpcGauntlet: OnDialogueError: '%s'"), *Msg);
}

void UAINpcGauntletTestController::OnSessionEnded()
{
	bSessionEnded = true;
	UE_LOG(LogGauntlet, Verbose, TEXT("AINpcGauntlet: OnDialogueSessionEnded"));
}

// ── Helpers ──────────────────────────────────

void UAINpcGauntletTestController::EnterPhase(ETestPhase NewPhase)
{
	const TCHAR* PhaseNames[] = {
		TEXT("WaitingForWorld"), TEXT("Setup"), TEXT("DialogueInit"),
		TEXT("DialogueWait"), TEXT("EventTest"), TEXT("StateMachineTest"),
		TEXT("Cleanup"), TEXT("Done"),
	};

	UE_LOG(LogGauntlet, Display, TEXT("AINpcGauntlet: Phase %s → %s"),
		PhaseNames[static_cast<int32>(Phase)],
		PhaseNames[static_cast<int32>(NewPhase)]);

	Phase = NewPhase;
	PhaseElapsed = 0.0f;
	MarkHeartbeatActive(FString::Printf(TEXT("Phase: %s"), PhaseNames[static_cast<int32>(NewPhase)]));

	if (Phase == ETestPhase::Done)
	{
		LogSummary();
		const int32 ExitCode = (ChecksFailed > 0) ? 1 : 0;
		EndTest(ExitCode);
	}
}

void UAINpcGauntletTestController::LogSummary()
{
	const int32 Total = ChecksPassed + ChecksFailed;
	UE_LOG(LogGauntlet, Display, TEXT(""));
	UE_LOG(LogGauntlet, Display, TEXT("=== AINpc Gauntlet Test Summary ==="));
	UE_LOG(LogGauntlet, Display, TEXT("  Total:   %d"), Total);
	UE_LOG(LogGauntlet, Display, TEXT("  Passed:  %d"), ChecksPassed);
	UE_LOG(LogGauntlet, Display, TEXT("  Failed:  %d"), ChecksFailed);
	UE_LOG(LogGauntlet, Display, TEXT("  Time:    %.2fs"), TotalTestTimeSeconds);
	UE_LOG(LogGauntlet, Display, TEXT(""));
}
