#include "Events/NpcEventPayloadBlueprintLibrary.h"

namespace NpcEventPayload
{
	template <typename PayloadType>
	FNpcEventMessage MakeMessage(const FGameplayTag& EventTag, const FGameplayTagContainer& RoutingTags, const PayloadType& Payload)
	{
		FNpcEventMessage EventMessage;
		EventMessage.EventTag = EventTag;
		EventMessage.RoutingTags = RoutingTags;
		EventMessage.Payload.InitializeAs<PayloadType>(Payload);
		return EventMessage;
	}

	template <typename PayloadType>
	bool TryExtractPayload(const FNpcEventMessage& EventMessage, PayloadType& OutPayload)
	{
		if (const PayloadType* TypedPayload = EventMessage.Payload.GetPtr<PayloadType>())
		{
			OutPayload = *TypedPayload;
			return true;
		}

		return false;
	}
}

FNpcEventMessage UNpcEventPayloadBlueprintLibrary::MakeAttackEventMessage(
	const FGameplayTag EventTag,
	const FGameplayTagContainer& RoutingTags,
	AActor* InstigatorActor,
	AActor* TargetActor,
	const float DamageAmount,
	const FGameplayTag DamageTypeTag)
{
	FNpcAttackEventPayload Payload;
	Payload.InstigatorActor = InstigatorActor;
	Payload.TargetActor = TargetActor;
	Payload.DamageAmount = DamageAmount;
	Payload.DamageTypeTag = DamageTypeTag;

	return NpcEventPayload::MakeMessage(EventTag, RoutingTags, Payload);
}

FNpcEventMessage UNpcEventPayloadBlueprintLibrary::MakeGiftEventMessage(
	const FGameplayTag EventTag,
	const FGameplayTagContainer& RoutingTags,
	AActor* GiverActor,
	AActor* ReceiverActor,
	const FGameplayTag ItemTag,
	const int32 Quantity)
{
	FNpcGiftEventPayload Payload;
	Payload.GiverActor = GiverActor;
	Payload.ReceiverActor = ReceiverActor;
	Payload.ItemTag = ItemTag;
	Payload.Quantity = Quantity;

	return NpcEventPayload::MakeMessage(EventTag, RoutingTags, Payload);
}

FNpcEventMessage UNpcEventPayloadBlueprintLibrary::MakeTradeEventMessage(
	const FGameplayTag EventTag,
	const FGameplayTagContainer& RoutingTags,
	AActor* InitiatorActor,
	AActor* CounterpartyActor,
	const FGameplayTag OfferedItemTag,
	const int32 OfferedQuantity,
	const FGameplayTag RequestedItemTag,
	const int32 RequestedQuantity)
{
	FNpcTradeEventPayload Payload;
	Payload.InitiatorActor = InitiatorActor;
	Payload.CounterpartyActor = CounterpartyActor;
	Payload.OfferedItemTag = OfferedItemTag;
	Payload.OfferedQuantity = OfferedQuantity;
	Payload.RequestedItemTag = RequestedItemTag;
	Payload.RequestedQuantity = RequestedQuantity;

	return NpcEventPayload::MakeMessage(EventTag, RoutingTags, Payload);
}

FNpcEventMessage UNpcEventPayloadBlueprintLibrary::MakeLLMDegradationEventMessage(
	const FGameplayTag EventTag,
	const FGameplayTagContainer& RoutingTags,
	AActor* NpcActor,
	const FString& Reason,
	const int32 RetryCount,
	const bool bUsedTemplate)
{
	FNpcLLMDegradationEventPayload Payload;
	Payload.NpcActor = NpcActor;
	Payload.Reason = Reason;
	Payload.RetryCount = RetryCount;
	Payload.bUsedTemplate = bUsedTemplate;

	return NpcEventPayload::MakeMessage(EventTag, RoutingTags, Payload);
}

bool UNpcEventPayloadBlueprintLibrary::TryGetAttackPayloadFromMessage(const FNpcEventMessage& EventMessage, FNpcAttackEventPayload& OutPayload)
{
	return NpcEventPayload::TryExtractPayload(EventMessage, OutPayload);
}

bool UNpcEventPayloadBlueprintLibrary::TryGetGiftPayloadFromMessage(const FNpcEventMessage& EventMessage, FNpcGiftEventPayload& OutPayload)
{
	return NpcEventPayload::TryExtractPayload(EventMessage, OutPayload);
}

bool UNpcEventPayloadBlueprintLibrary::TryGetTradePayloadFromMessage(const FNpcEventMessage& EventMessage, FNpcTradeEventPayload& OutPayload)
{
	return NpcEventPayload::TryExtractPayload(EventMessage, OutPayload);
}

bool UNpcEventPayloadBlueprintLibrary::TryGetLLMDegradationPayloadFromMessage(const FNpcEventMessage& EventMessage, FNpcLLMDegradationEventPayload& OutPayload)
{
	return NpcEventPayload::TryExtractPayload(EventMessage, OutPayload);
}

UScriptStruct* UNpcEventPayloadBlueprintLibrary::GetPayloadStructType(const FNpcEventMessage& EventMessage)
{
	return const_cast<UScriptStruct*>(EventMessage.Payload.GetScriptStruct());
}
