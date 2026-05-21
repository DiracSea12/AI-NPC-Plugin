// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NpcDialogueBubbleWidget.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPartialResponse, const FString&, PartialText);

DECLARE_MULTICAST_DELEGATE_OneParam(FOnPartialResponseNative, const FString&);

UENUM(BlueprintType)
enum class EStreamingDisplayMode : uint8
{
	CharacterByCharacter UMETA(DisplayName = "Character By Character"),
	SentenceBySentence UMETA(DisplayName = "Sentence By Sentence"),
	Immediate UMETA(Hidden, DisplayName = "Immediate (Legacy)")
};

UCLASS(Blueprintable, BlueprintType)
class AINPCUI_API UNpcDialogueBubbleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Dialogue")
	FOnPartialResponse OnPartialResponse;

	FOnPartialResponseNative OnPartialResponseNativeDelegate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue", meta = (ClampMin = "1.0", ClampMax = "1000.0", UIMin = "1.0", UIMax = "120.0"))
	float CharactersPerSecond = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	bool bUseTypewriterEffect = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	bool bEnableStreamingDisplay = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	EStreamingDisplayMode StreamingDisplayMode = EStreamingDisplayMode::CharacterByCharacter;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	int32 StreamBufferOverflowThreshold = 150;

	UFUNCTION(BlueprintCallable, Category = "Dialogue") void SetDialogueText(const FString& Text);
	UFUNCTION(BlueprintCallable, Category = "Dialogue") void AppendPartialText(const FString& PartialText);
	UFUNCTION(BlueprintCallable, Category = "Dialogue") void HandlePartialResponse(const FString& PartialText);
	UFUNCTION(BlueprintCallable, Category = "Dialogue") void ShowResponseText(const FString& Text);
	UFUNCTION(BlueprintCallable, Category = "Dialogue") FString GetFullResponseText() const;

	// Test helpers
	FText GetDisplayedText() const { return FText::FromString(VisibleText); }
	void AdvanceTypewriterForTest(float DeltaTime);
	FOnPartialResponseNative& OnPartialResponseNative() { return OnPartialResponseNativeDelegate; }

	UFUNCTION(BlueprintCallable, Category = "Dialogue")
	void BindToNpcComponent(class UAINpcComponent* Component);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION()
	void HandleDialogueSessionStarted();

	FString CurrentText;
	FString StreamBuffer;
	FString VisibleText;
	FTimerHandle TypewriterTimer;
	int32 CurrentCharIndex = 0;
	TWeakObjectPtr<class UAINpcComponent> BoundNpcComponent;
	bool bIsDestroying = false;

	void BroadcastPartialSegment(const FString& SegmentText);
	void ResetDisplayState(bool bClearCurrentText);
	void ApplyImmediateDisplayText(const FString& Text);
	void TypewriterTick();
	void FlushStreamBuffer();
};
