// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/NpcDialogueBubbleWidget.h"
#include "AINpcUILog.h"
#include "Components/AINpcComponent.h"
#include "CoreGlobals.h"
#include "TimerManager.h"

namespace
{
	bool ShouldApplyImmediateFallbackWhenWorldMissing()
	{
#if WITH_EDITOR
		return !GIsAutomationTesting;
#else
		return true;
#endif
	}

	bool IsSentenceBoundaryCharacter(const TCHAR Character)
	{
		return Character == TEXT('.')
			|| Character == TEXT('?')
			|| Character == TEXT('!')
			|| Character == TEXT('\n')
			|| Character == TEXT('\r')
			|| Character == static_cast<TCHAR>(0x3002) // Full-width ideographic period.
			|| Character == static_cast<TCHAR>(0xFF1F) // Full-width question mark.
			|| Character == static_cast<TCHAR>(0xFF01) // Full-width exclamation mark.
			|| Character == static_cast<TCHAR>(0x2026); // Unicode ellipsis.
	}
}

void UNpcDialogueBubbleWidget::NativeConstruct()
{
	Super::NativeConstruct();
}

void UNpcDialogueBubbleWidget::NativeDestruct()
{
	bIsDestroying = true;

	if (TypewriterTimer.IsValid())
	{
		if (GetWorld())
		{
			GetWorld()->GetTimerManager().ClearTimer(TypewriterTimer);
		}
		TypewriterTimer.Invalidate();
	}

	if (BoundNpcComponent.IsValid())
	{
		BoundNpcComponent->OnDialogueSessionStarted.RemoveDynamic(this, &UNpcDialogueBubbleWidget::HandleDialogueSessionStarted);
		BoundNpcComponent->OnDialogueResponse.RemoveDynamic(this, &UNpcDialogueBubbleWidget::ShowResponseText);
		BoundNpcComponent->OnPartialResponse.RemoveDynamic(this, &UNpcDialogueBubbleWidget::HandlePartialResponse);
		BoundNpcComponent.Reset();
	}

	Super::NativeDestruct();
}

void UNpcDialogueBubbleWidget::SetDialogueText(const FString& Text)
{
	ResetDisplayState(/*bClearCurrentText=*/false);
	CurrentText = Text;

	if (!bUseTypewriterEffect)
	{
		ApplyImmediateDisplayText(CurrentText);
		return;
	}

	if (UWorld* World = GetWorld())
	{
		const float Interval = CharactersPerSecond > 0.0f ? (1.0f / CharactersPerSecond) : 0.033f;
		World->GetTimerManager().SetTimer(TypewriterTimer, this, &UNpcDialogueBubbleWidget::TypewriterTick, Interval, true);
	}
	else if (ShouldApplyImmediateFallbackWhenWorldMissing())
	{
		ApplyImmediateDisplayText(CurrentText);
	}
}

void UNpcDialogueBubbleWidget::AppendPartialText(const FString& PartialText)
{
	CurrentText.Append(PartialText);
}

void UNpcDialogueBubbleWidget::HandlePartialResponse(const FString& PartialText)
{
	check(IsInGameThread());

	if (PartialText.IsEmpty())
	{
		return;
	}

	// Late chunk guard for stale callbacks after dialogue teardown.
	if (BoundNpcComponent.IsValid() && !BoundNpcComponent->IsDialogueActive())
	{
		return;
	}

	if (!bEnableStreamingDisplay || StreamingDisplayMode == EStreamingDisplayMode::Immediate)
	{
		VisibleText += PartialText;
		CurrentText = VisibleText;
		CurrentCharIndex = VisibleText.Len();
		BroadcastPartialSegment(PartialText);
		return;
	}

	if (StreamingDisplayMode == EStreamingDisplayMode::CharacterByCharacter)
	{
		CurrentText += PartialText;

		if (!bUseTypewriterEffect)
		{
			ApplyImmediateDisplayText(CurrentText);
			return;
		}

		if (CurrentCharIndex >= CurrentText.Len())
		{
			return;
		}

		if (UWorld* World = GetWorld())
		{
			if (!World->GetTimerManager().IsTimerActive(TypewriterTimer))
			{
				const float Interval = CharactersPerSecond > 0.0f ? (1.0f / CharactersPerSecond) : 0.033f;
				World->GetTimerManager().SetTimer(TypewriterTimer, this, &UNpcDialogueBubbleWidget::TypewriterTick, Interval, true);
			}
		}
		else if (ShouldApplyImmediateFallbackWhenWorldMissing())
		{
			ApplyImmediateDisplayText(CurrentText);
		}

		return;
	}

	StreamBuffer += PartialText;
	const bool bBoundDialogueActive = BoundNpcComponent.IsValid() && BoundNpcComponent->IsDialogueActive();
	if (bBoundDialogueActive)
	{
		// Keep full-response state synchronized for active bound dialogue sessions even when
		// sentence mode is still buffering undisplayed text.
		CurrentText = VisibleText + StreamBuffer;
	}
	bool bFlushedSentenceSegment = false;

	if (StreamingDisplayMode == EStreamingDisplayMode::SentenceBySentence)
	{
		int32 LastTerminator = INDEX_NONE;
		for (int32 i = StreamBuffer.Len() - 1; i >= 0; --i)
		{
			if (IsSentenceBoundaryCharacter(StreamBuffer[i]))
			{
				LastTerminator = i;
				break;
			}
		}

		if (LastTerminator != INDEX_NONE)
		{
			const FString Sentence = StreamBuffer.Left(LastTerminator + 1);
			VisibleText += Sentence;
			CurrentText = VisibleText;
			CurrentCharIndex = VisibleText.Len();
			StreamBuffer = StreamBuffer.Mid(LastTerminator + 1);
			BroadcastPartialSegment(Sentence);
			bFlushedSentenceSegment = true;
		}
		else if (StreamBuffer.Len() > StreamBufferOverflowThreshold)
		{
			FlushStreamBuffer();
			bFlushedSentenceSegment = true;
		}
	}

	if (!bFlushedSentenceSegment)
	{
		BroadcastPartialSegment(PartialText);
	}
}

void UNpcDialogueBubbleWidget::ShowResponseText(const FString& Text)
{
	if (bEnableStreamingDisplay && Text.IsEmpty())
	{
		if (StreamingDisplayMode == EStreamingDisplayMode::SentenceBySentence)
		{
			FlushStreamBuffer();
		}
		return;
	}

	// Preserve buffered streaming tail when terminal callback duplicates the current visible response.
	if (bEnableStreamingDisplay
		&& StreamingDisplayMode == EStreamingDisplayMode::SentenceBySentence
		&& !StreamBuffer.IsEmpty()
		&& Text == VisibleText)
	{
		return;
	}

	CurrentText = Text;

	if (bUseTypewriterEffect)
	{
		const bool bStreamingExtension = bEnableStreamingDisplay
			&& !VisibleText.IsEmpty()
			&& CurrentText.StartsWith(VisibleText, ESearchCase::CaseSensitive);

		if (!bStreamingExtension)
		{
			VisibleText.Empty();
			CurrentCharIndex = 0;
		}
		else
		{
			CurrentCharIndex = FMath::Clamp(VisibleText.Len(), 0, CurrentText.Len());
		}

		StreamBuffer.Empty();

		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(TypewriterTimer);
			TypewriterTimer.Invalidate();

			if (CurrentCharIndex < CurrentText.Len())
			{
				const float Interval = CharactersPerSecond > 0.0f ? (1.0f / CharactersPerSecond) : 0.033f;
				World->GetTimerManager().SetTimer(TypewriterTimer, this, &UNpcDialogueBubbleWidget::TypewriterTick, Interval, true);
			}
		}
		else if (ShouldApplyImmediateFallbackWhenWorldMissing())
		{
			ApplyImmediateDisplayText(CurrentText);
		}
	}
	else
	{
		StreamBuffer.Empty();
		ApplyImmediateDisplayText(CurrentText);
	}
}

FString UNpcDialogueBubbleWidget::GetFullResponseText() const
{
	return CurrentText;
}

void UNpcDialogueBubbleWidget::FlushStreamBuffer()
{
	if (!StreamBuffer.IsEmpty())
	{
		const FString FlushedContent = StreamBuffer;
		VisibleText += FlushedContent;
		CurrentText = VisibleText;
		CurrentCharIndex = VisibleText.Len();
		StreamBuffer.Empty();
		BroadcastPartialSegment(FlushedContent);
	}
}

void UNpcDialogueBubbleWidget::TypewriterTick()
{
	if (bIsDestroying)
	{
		return;
	}

	if (CurrentCharIndex < CurrentText.Len())
	{
		const int32 PreviousIndex = CurrentCharIndex;
		CurrentCharIndex++;
		VisibleText = CurrentText.Left(CurrentCharIndex);
		const FString RevealedSegment = CurrentText.Mid(PreviousIndex, CurrentCharIndex - PreviousIndex);
		BroadcastPartialSegment(RevealedSegment);
		UE_LOG(LogAINpcUI, Verbose, TEXT("Typewriter effect executing, char %d/%d"), CurrentCharIndex, CurrentText.Len());
	}
	else
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(TypewriterTimer);
		}
		TypewriterTimer.Invalidate();
	}
}

void UNpcDialogueBubbleWidget::AdvanceTypewriterForTest(float DeltaTime)
{
	if (!bUseTypewriterEffect || CharactersPerSecond <= 0)
	{
		ApplyImmediateDisplayText(CurrentText);
		return;
	}

	int32 CharsToReveal = FMath::FloorToInt(DeltaTime * CharactersPerSecond);
	CharsToReveal = FMath::Clamp(CharsToReveal, 0, CurrentText.Len() - VisibleText.Len());
	CharsToReveal = FMath::Min(CharsToReveal, 3); // Clamp per-step reveal to keep hitch spikes visually stable.

	if (CharsToReveal > 0)
	{
		const int32 PreviousLen = VisibleText.Len();
		const int32 NewLen = FMath::Min(PreviousLen + CharsToReveal, CurrentText.Len());
		VisibleText = CurrentText.Left(NewLen);
		CurrentCharIndex = NewLen;
		BroadcastPartialSegment(CurrentText.Mid(PreviousLen, NewLen - PreviousLen));
	}
}

void UNpcDialogueBubbleWidget::BindToNpcComponent(class UAINpcComponent* Component)
{
	if (BoundNpcComponent.IsValid())
	{
		BoundNpcComponent->OnDialogueSessionStarted.RemoveDynamic(this, &UNpcDialogueBubbleWidget::HandleDialogueSessionStarted);
		BoundNpcComponent->OnDialogueResponse.RemoveDynamic(this, &UNpcDialogueBubbleWidget::ShowResponseText);
		BoundNpcComponent->OnPartialResponse.RemoveDynamic(this, &UNpcDialogueBubbleWidget::HandlePartialResponse);
		BoundNpcComponent.Reset();
	}

	if (!Component)
	{
		return;
	}

	BoundNpcComponent = Component;
	Component->OnDialogueSessionStarted.AddDynamic(this, &UNpcDialogueBubbleWidget::HandleDialogueSessionStarted);
	Component->OnDialogueResponse.AddDynamic(this, &UNpcDialogueBubbleWidget::ShowResponseText);
	Component->OnPartialResponse.AddDynamic(this, &UNpcDialogueBubbleWidget::HandlePartialResponse);

	if (Component->IsDialogueActive())
	{
		const FString ExistingResponse = Component->GetNpcResponse();
		if (!ExistingResponse.IsEmpty())
		{
			ShowResponseText(ExistingResponse);
		}
		else
		{
			ResetDisplayState(/*bClearCurrentText=*/true);
		}
	}
	else
	{
		ResetDisplayState(/*bClearCurrentText=*/true);
	}
}

void UNpcDialogueBubbleWidget::HandleDialogueSessionStarted()
{
	ResetDisplayState(/*bClearCurrentText=*/true);
}

void UNpcDialogueBubbleWidget::BroadcastPartialSegment(const FString& SegmentText)
{
	if (SegmentText.IsEmpty())
	{
		return;
	}

	OnPartialResponse.Broadcast(SegmentText);
	OnPartialResponseNativeDelegate.Broadcast(SegmentText);
}

void UNpcDialogueBubbleWidget::ResetDisplayState(const bool bClearCurrentText)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TypewriterTimer);
	}

	TypewriterTimer.Invalidate();
	VisibleText.Empty();
	StreamBuffer.Empty();
	CurrentCharIndex = 0;

	if (bClearCurrentText)
	{
		CurrentText.Empty();
	}
}

void UNpcDialogueBubbleWidget::ApplyImmediateDisplayText(const FString& Text)
{
	const FString PreviousVisibleText = VisibleText;
	CurrentText = Text;
	VisibleText = Text;
	CurrentCharIndex = CurrentText.Len();

	if (VisibleText.StartsWith(PreviousVisibleText, ESearchCase::CaseSensitive) && VisibleText.Len() > PreviousVisibleText.Len())
	{
		BroadcastPartialSegment(VisibleText.Mid(PreviousVisibleText.Len()));
	}
}
