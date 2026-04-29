#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "NpcEventPayloadTypes.generated.h"

class AActor;

USTRUCT(BlueprintType)
struct AINPCCORE_API FNpcAttackEventPayload
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI NPC|Events|Payload")
	TObjectPtr<AActor> InstigatorActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI NPC|Events|Payload")
	TObjectPtr<AActor> TargetActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI NPC|Events|Payload")
	float DamageAmount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI NPC|Events|Payload")
	FGameplayTag DamageTypeTag;
};

USTRUCT(BlueprintType)
struct AINPCCORE_API FNpcGiftEventPayload
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI NPC|Events|Payload")
	TObjectPtr<AActor> GiverActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI NPC|Events|Payload")
	TObjectPtr<AActor> ReceiverActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI NPC|Events|Payload")
	FGameplayTag ItemTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI NPC|Events|Payload")
	int32 Quantity = 1;
};

USTRUCT(BlueprintType)
struct AINPCCORE_API FNpcTradeEventPayload
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI NPC|Events|Payload")
	TObjectPtr<AActor> InitiatorActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI NPC|Events|Payload")
	TObjectPtr<AActor> CounterpartyActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI NPC|Events|Payload")
	FGameplayTag OfferedItemTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI NPC|Events|Payload")
	int32 OfferedQuantity = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI NPC|Events|Payload")
	FGameplayTag RequestedItemTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI NPC|Events|Payload")
	int32 RequestedQuantity = 1;
};

USTRUCT(BlueprintType)
struct AINPCCORE_API FNpcLLMDegradationEventPayload
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI NPC|Events|Payload")
	TObjectPtr<AActor> NpcActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI NPC|Events|Payload")
	FString Reason;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI NPC|Events|Payload")
	int32 RetryCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI NPC|Events|Payload")
	bool bUsedTemplate = false;
};
