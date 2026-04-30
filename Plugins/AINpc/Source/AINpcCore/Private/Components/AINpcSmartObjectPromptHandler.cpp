#include "Components/AINpcSmartObjectPromptHandler.h"

#include "Components/AINpcComponent.h"
#include "Data/NpcPersonaDataAsset.h"
#include "Misc/Optional.h"
#include "Prompt/PromptBuilder.h"

#if defined(WITH_SMARTOBJECTS) && WITH_SMARTOBJECTS
#define AINPC_WITH_SMARTOBJECTS 1
#include "SmartObjectRequestTypes.h"
#include "SmartObjectRuntime.h"
#include "SmartObjectSubsystem.h"
#include "StructUtils/StructView.h"
#else
#define AINPC_WITH_SMARTOBJECTS 0
#endif

namespace
{
	constexpr int32 MaxPromptTokens = 512;
	constexpr float PromptSmartObjectSearchRadius = 1000.0f;
	constexpr int32 MaxPromptTargets = 12;

#if WITH_EDITOR
	TOptional<TArray<FString>> GSmartObjectTargetsForPromptOverrideForTests;
#endif
}

FString FAINpcSmartObjectPromptHandler::BuildSystemPrompt(const UAINpcComponent& Component)
{
	FPromptBuilderConfig BuilderConfig;
	BuilderConfig.MaxPromptTokens = MaxPromptTokens;
	BuilderConfig.AvailableSmartObjectTargets = GetAvailableTargets(Component);
	return FPromptBuilder::BuildSystemPrompt(Component.PersonaDataAsset, BuilderConfig);
}

TArray<FString> FAINpcSmartObjectPromptHandler::GetAvailableTargets(const UAINpcComponent& Component)
{
	TArray<FString> AvailableTargets;

#if WITH_EDITOR
	if (GSmartObjectTargetsForPromptOverrideForTests.IsSet())
	{
		AvailableTargets = GSmartObjectTargetsForPromptOverrideForTests.GetValue();
		AvailableTargets.Sort();
		return AvailableTargets;
	}
#endif

#if AINPC_WITH_SMARTOBJECTS
	AActor* OwnerActor = Component.GetOwner();
	UWorld* World = Component.GetWorld();
	if (!IsValid(OwnerActor) || !IsValid(World))
	{
		return AvailableTargets;
	}

	USmartObjectSubsystem* SmartObjectSubsystem = USmartObjectSubsystem::GetCurrent(World);
	if (!IsValid(SmartObjectSubsystem))
	{
		return AvailableTargets;
	}

	const FVector QueryExtent(PromptSmartObjectSearchRadius);
	const FBox QueryBox = FBox::BuildAABB(OwnerActor->GetActorLocation(), QueryExtent);
	const FSmartObjectRequestFilter RequestFilter;
	const FSmartObjectRequest Request(QueryBox, RequestFilter);

	TArray<FSmartObjectRequestResult> Results;
	const bool bFound = SmartObjectSubsystem->FindSmartObjects(
		Request,
		Results,
		FConstStructView::Make(FSmartObjectActorUserData(OwnerActor)));
	if (!bFound || Results.IsEmpty())
	{
		return AvailableTargets;
	}

	TSet<FString> UniqueTargets;
	for (const FSmartObjectRequestResult& Result : Results)
	{
		if (!Result.IsValid())
		{
			continue;
		}

		const FString SlotIdentifier = LexToString(Result.SlotHandle);
		if (!SlotIdentifier.IsEmpty())
		{
			UniqueTargets.Add(SlotIdentifier);
		}

		if (UniqueTargets.Num() >= MaxPromptTargets)
		{
			break;
		}
	}

	AvailableTargets = UniqueTargets.Array();
	AvailableTargets.Sort();
	if (AvailableTargets.Num() > MaxPromptTargets)
	{
		AvailableTargets.SetNum(MaxPromptTargets);
	}
#endif

	return AvailableTargets;
}

void FAINpcSmartObjectPromptHandler::SetTargetsOverrideForTest(const TArray<FString>& InTargets)
{
#if WITH_EDITOR
	GSmartObjectTargetsForPromptOverrideForTests = InTargets;
#else
	(void)InTargets;
#endif
}

void FAINpcSmartObjectPromptHandler::ClearTargetsOverrideForTest()
{
#if WITH_EDITOR
	GSmartObjectTargetsForPromptOverrideForTests.Reset();
#endif
}

#undef AINPC_WITH_SMARTOBJECTS
