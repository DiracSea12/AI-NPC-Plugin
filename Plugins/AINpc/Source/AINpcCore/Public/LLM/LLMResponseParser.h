#pragma once

#include "CoreMinimal.h"
#include "LLMResponseParser.generated.h"

namespace AINpc::Actions
{
inline constexpr const TCHAR* DefaultTalkActionType = TEXT("Action.DefaultTalk");
}

UENUM(BlueprintType)
enum class ELLMResponseParseTier : uint8
{
	Unknown UMETA(DisplayName = "Unknown"),
	None UMETA(DisplayName = "None"),
	FunctionCalling UMETA(DisplayName = "Function Calling"),
	StrictJsonSchema UMETA(DisplayName = "Strict JSON Schema"),
	LooseJson UMETA(DisplayName = "Loose JSON"),
	LooseExtraction UMETA(DisplayName = "Loose Extraction"),
	PlainText UMETA(DisplayName = "Plain Text")
};

USTRUCT(BlueprintType)
struct AINPCCORE_API FNpcAction
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AI NPC|LLM")
	FString ActionType;

	UPROPERTY(BlueprintReadOnly, Category = "AI NPC|LLM")
	FString Target;
};

USTRUCT(BlueprintType)
struct AINPCCORE_API FVADDelta
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AI NPC|LLM")
	float Valence = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "AI NPC|LLM")
	float Arousal = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "AI NPC|LLM")
	float Dominance = 0.0f;
};

USTRUCT(BlueprintType)
struct AINPCCORE_API FRelationshipDelta
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AI NPC|LLM")
	float Affinity = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "AI NPC|LLM")
	float Trust = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "AI NPC|LLM")
	float Familiarity = 0.0f;
};

USTRUCT(BlueprintType)
struct AINPCCORE_API FParsedLLMResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AI NPC|LLM")
	FString Dialogue;

	UPROPERTY(BlueprintReadOnly, Category = "AI NPC|LLM")
	TArray<FNpcAction> Actions;

	UPROPERTY(BlueprintReadOnly, Category = "AI NPC|LLM")
	FVADDelta EmotionDelta;

	UPROPERTY(BlueprintReadOnly, Category = "AI NPC|LLM")
	FRelationshipDelta RelationshipDelta;

	UPROPERTY(BlueprintReadOnly, Category = "AI NPC|LLM")
	bool bParsedAsJson = false;

	UPROPERTY(BlueprintReadOnly, Category = "AI NPC|LLM")
	ELLMResponseParseTier ParseTier = ELLMResponseParseTier::None;
};

class AINPCCORE_API FLLMResponseParser
{
public:
	// FR-27 fallback chain:
	// 1) Function Calling / Tool Use
	// 2) Strict JSON schema
	// 3) Loose extraction from mixed text
	// 4) Plain text downgrade
	static bool ParseOpenAIChatCompletion(
		const FString& ResponseBody,
		FParsedLLMResponse& OutParsedResponse,
		FString& OutErrorMessage);

	static bool ParseAnthropicMessages(
		const FString& ResponseBody,
		FParsedLLMResponse& OutParsedResponse,
		FString& OutErrorMessage);
};
