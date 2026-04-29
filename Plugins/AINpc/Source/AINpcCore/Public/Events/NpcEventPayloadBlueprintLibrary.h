#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Events/NpcEventPayloadTypes.h"
#include "Events/NpcEventSubsystem.h"
#include "NpcEventPayloadBlueprintLibrary.generated.h"

UCLASS()
class AINPCCORE_API UNpcEventPayloadBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "AI NPC|Events|Payloads")
	static FNpcEventMessage MakeAttackEventMessage(
		FGameplayTag EventTag,
		const FGameplayTagContainer& RoutingTags,
		AActor* InstigatorActor,
		AActor* TargetActor,
		float DamageAmount,
		FGameplayTag DamageTypeTag);

	UFUNCTION(BlueprintPure, Category = "AI NPC|Events|Payloads")
	static FNpcEventMessage MakeGiftEventMessage(
		FGameplayTag EventTag,
		const FGameplayTagContainer& RoutingTags,
		AActor* GiverActor,
		AActor* ReceiverActor,
		FGameplayTag ItemTag,
		int32 Quantity);

	UFUNCTION(BlueprintPure, Category = "AI NPC|Events|Payloads")
	static FNpcEventMessage MakeTradeEventMessage(
		FGameplayTag EventTag,
		const FGameplayTagContainer& RoutingTags,
		AActor* InitiatorActor,
		AActor* CounterpartyActor,
		FGameplayTag OfferedItemTag,
		int32 OfferedQuantity,
		FGameplayTag RequestedItemTag,
		int32 RequestedQuantity);

	UFUNCTION(BlueprintPure, Category = "AI NPC|Events|Payloads")
	static bool TryGetAttackPayloadFromMessage(const FNpcEventMessage& EventMessage, FNpcAttackEventPayload& OutPayload);

	UFUNCTION(BlueprintPure, Category = "AI NPC|Events|Payloads")
	static bool TryGetGiftPayloadFromMessage(const FNpcEventMessage& EventMessage, FNpcGiftEventPayload& OutPayload);

	UFUNCTION(BlueprintPure, Category = "AI NPC|Events|Payloads")
	static bool TryGetTradePayloadFromMessage(const FNpcEventMessage& EventMessage, FNpcTradeEventPayload& OutPayload);

	UFUNCTION(BlueprintPure, Category = "AI NPC|Events|Payloads")
	static UScriptStruct* GetPayloadStructType(const FNpcEventMessage& EventMessage);
};
