#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NpcPersonaDataAsset.generated.h"

class UAnimMontage;

UENUM(BlueprintType)
enum class ENpcSpeakingLength : uint8
{
	VeryShort UMETA(DisplayName = "Very Short"),
	Short UMETA(DisplayName = "Short"),
	Medium UMETA(DisplayName = "Medium"),
	Long UMETA(DisplayName = "Long")
};

UCLASS(BlueprintType)
class AINPCCORE_API UNpcPersonaDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Persona")
	FString PersonaName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Persona", meta = (MultiLine = "true"))
	FString Background;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Persona", meta = (MultiLine = "true"))
	FString SpeakingStyle;

	// Avoid shipping secrets in content assets; prefer env-var based key injection for runtime.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Provider")
	FString ApiKey;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Provider")
	FString BaseUrl;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Provider")
	FString Model;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	ENpcSpeakingLength SpeakingLength = ENpcSpeakingLength::Medium;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Delay Masking")
	TArray<TSoftObjectPtr<UAnimMontage>> DelayMaskingMontages;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Delay Masking|Events")
	TArray<TSoftObjectPtr<UAnimMontage>> HitReactionDelayMaskingMontages;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Delay Masking|Events")
	TArray<TSoftObjectPtr<UAnimMontage>> InspectDelayMaskingMontages;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Delay Masking", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DelayFillerThreshold = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Delay Masking")
	TArray<FText> DelayFillerTexts;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Failure Handling", meta = (MultiLine = "true"))
	FText FailureFallbackResponse;
};
