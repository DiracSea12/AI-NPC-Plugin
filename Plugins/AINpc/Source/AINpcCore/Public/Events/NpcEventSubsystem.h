#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "Events/NpcEventPayloadTypes.h"
#include "GameplayTagContainer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "NpcEventSubsystem.generated.h"

UENUM(BlueprintType)
enum class ENpcEventDispatchStage : uint8
{
	DelayMasking UMETA(DisplayName = "Delay Masking"),
	EmotionAppraisal UMETA(DisplayName = "Emotion Appraisal"),
	MemoryWrite UMETA(DisplayName = "Memory Write"),
	PromptUpdate UMETA(DisplayName = "Prompt Update")
};

USTRUCT(BlueprintType)
struct AINPCCORE_API FNpcEventMessage
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI NPC|Events")
	FGameplayTag EventTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI NPC|Events")
	FGameplayTagContainer RoutingTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI NPC|Events")
	FInstancedStruct Payload;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNpcEventStageDispatchedDynamic, const FNpcEventMessage&, EventMessage, ENpcEventDispatchStage, DispatchStage);
DECLARE_MULTICAST_DELEGATE_TwoParams(FNpcEventStageDispatchedNative, const FNpcEventMessage&, ENpcEventDispatchStage);

UCLASS()
class AINPCCORE_API UNpcEventSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "AI NPC|Events")
	void BroadcastEvent(const FNpcEventMessage& EventMessage);

	UFUNCTION(BlueprintPure, Category = "AI NPC|Events")
	static TArray<ENpcEventDispatchStage> GetDefaultDispatchOrder();

	FNpcEventStageDispatchedNative& OnEventStageDispatchedNative();

public:
	UPROPERTY(BlueprintAssignable, Category = "AI NPC|Events")
	FNpcEventStageDispatchedDynamic OnEventStageDispatched;

private:
	FNpcEventStageDispatchedNative EventStageDispatchedNative;
};
