#include "SmartObjectBridge/SmartObjectBridgeContext.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/ObjectKey.h"

#if defined(WITH_SMARTOBJECTS) && WITH_SMARTOBJECTS
#include "SmartObjectDefinition.h"
#include "SmartObjectRequestTypes.h"
#include "SmartObjectRuntime.h"
#include "SmartObjectSubsystem.h"
#include "SmartObjectTypes.h"
#include "StructUtils/StructView.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(SmartObjectBridgeContext)

#if defined(WITH_SMARTOBJECTS) && WITH_SMARTOBJECTS
#define AINPC_WITH_SMARTOBJECTS 1
#else
#define AINPC_WITH_SMARTOBJECTS 0
#endif

struct FSmartObjectBridgeRuntimeData;

namespace
{
struct FTrackedSmartObjectClaim
{
#if AINPC_WITH_SMARTOBJECTS
	FSmartObjectClaimHandle Handle = FSmartObjectClaimHandle::InvalidHandle;
#endif
	FObjectKey OwnerKey;
	TWeakObjectPtr<UWorld> World;
};

#if AINPC_WITH_SMARTOBJECTS
ESmartObjectClaimPriority ToSmartObjectClaimPriority(const int32 Priority)
{
	switch (FMath::Clamp(Priority, 0, 4))
	{
	case 0:
		return ESmartObjectClaimPriority::Low;
	case 1:
		return ESmartObjectClaimPriority::BelowNormal;
	case 3:
		return ESmartObjectClaimPriority::AboveNormal;
	case 4:
		return ESmartObjectClaimPriority::High;
	default:
		return ESmartObjectClaimPriority::Normal;
	}
}

struct FSmartObjectUserRuntimeState
{
	FSmartObjectRequestResult PendingSlotResult;
	FSmartObjectClaimHandle ActiveClaimHandle = FSmartObjectClaimHandle::InvalidHandle;
};

bool ResolveUserKey(AActor* UserActor, FObjectKey& OutUserKey)
{
	if (!IsValid(UserActor))
	{
		return false;
	}

	OutUserKey = FObjectKey(UserActor);
	return true;
}
#endif
}

struct FSmartObjectBridgeRuntimeData
{
	TArray<FTrackedSmartObjectClaim> ActiveClaims;
	TMap<FObjectKey, TWeakObjectPtr<AActor>> UserActors;

#if AINPC_WITH_SMARTOBJECTS
	TMap<FObjectKey, FSmartObjectUserRuntimeState> UserStateByKey;
#endif
	TWeakObjectPtr<UWorld> LastKnownWorld;
	TWeakObjectPtr<AActor> LastResolvedUserActor;
};

#if AINPC_WITH_SMARTOBJECTS
namespace
{
UWorld* ResolveClaimWorld(const FSmartObjectBridgeRuntimeData& RuntimeData, const FSmartObjectClaimHandle& ClaimHandle, const UObject* FallbackContext)
{
	for (const FTrackedSmartObjectClaim& TrackedClaim : RuntimeData.ActiveClaims)
	{
		if (TrackedClaim.Handle == ClaimHandle)
		{
			return TrackedClaim.World.Get();
		}
	}

	UWorld* World = RuntimeData.LastKnownWorld.Get();
	if (!IsValid(World) && IsValid(FallbackContext))
	{
		World = FallbackContext->GetWorld();
	}

	return World;
}

FSmartObjectUserRuntimeState* FindUserState(
	FSmartObjectBridgeRuntimeData& RuntimeData,
	const FObjectKey& UserKey,
	AActor* UserActor,
	const bool bCreateIfMissing)
{
	if (TWeakObjectPtr<AActor>* ExistingActor = RuntimeData.UserActors.Find(UserKey))
	{
		if (!ExistingActor->IsValid())
		{
			RuntimeData.UserActors.Remove(UserKey);
			RuntimeData.UserStateByKey.Remove(UserKey);
		}
	}

	if (bCreateIfMissing)
	{
		RuntimeData.UserActors.FindOrAdd(UserKey) = UserActor;
		return &RuntimeData.UserStateByKey.FindOrAdd(UserKey);
	}

	return RuntimeData.UserStateByKey.Find(UserKey);
}

const FSmartObjectUserRuntimeState* FindUserState(
	const FSmartObjectBridgeRuntimeData& RuntimeData,
	const FObjectKey& UserKey)
{
	return RuntimeData.UserStateByKey.Find(UserKey);
}
}
#endif

void USmartObjectBridgeContext::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	RuntimeData = MakeUnique<FSmartObjectBridgeRuntimeData>();
	WorldCleanupDelegateHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &USmartObjectBridgeContext::HandleWorldCleanup);
}

void USmartObjectBridgeContext::Deinitialize()
{
	if (RuntimeData.IsValid())
	{
		ReleaseActiveClaimHandlesForWorld(nullptr);
	}

	if (WorldCleanupDelegateHandle.IsValid())
	{
		FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupDelegateHandle);
		WorldCleanupDelegateHandle.Reset();
	}

	RuntimeData.Reset();
	Super::Deinitialize();
}

bool USmartObjectBridgeContext::FindSlot(AActor* UserActor, const float SearchRadius)
{
	return FindSlotWithPreferredTarget(UserActor, SearchRadius, FString());
}

bool USmartObjectBridgeContext::FindSlotWithPreferredTarget(AActor* UserActor, const float SearchRadius, const FString& PreferredSlotId)
{
#if AINPC_WITH_SMARTOBJECTS
	if (!RuntimeData.IsValid() || !IsValid(UserActor))
	{
		return false;
	}

	FObjectKey UserKey;
	if (!ResolveUserKey(UserActor, UserKey))
	{
		return false;
	}

	FSmartObjectUserRuntimeState* UserState = FindUserState(*RuntimeData, UserKey, UserActor, true);
	if (!UserState)
	{
		return false;
	}

	UWorld* World = UserActor->GetWorld();
	if (!IsValid(World))
	{
		UserState->PendingSlotResult = FSmartObjectRequestResult();
		return false;
	}

	RuntimeData->LastKnownWorld = World;
	RuntimeData->LastResolvedUserActor = UserActor;

	USmartObjectSubsystem* SmartObjectSubsystem = USmartObjectSubsystem::GetCurrent(World);
	if (!IsValid(SmartObjectSubsystem))
	{
		UserState->PendingSlotResult = FSmartObjectRequestResult();
		return false;
	}

	const FVector QueryExtent(FMath::Max(0.0f, SearchRadius));
	const FBox QueryBox = FBox::BuildAABB(UserActor->GetActorLocation(), QueryExtent);
	const FSmartObjectRequestFilter RequestFilter;
	const FSmartObjectRequest Request(QueryBox, RequestFilter);

	TArray<FSmartObjectRequestResult> Results;
	const bool bFound = SmartObjectSubsystem->FindSmartObjects(
		Request,
		Results,
		FConstStructView::Make(FSmartObjectActorUserData(UserActor)));
	if (!bFound || Results.IsEmpty())
	{
		UserState->PendingSlotResult = FSmartObjectRequestResult();
		return false;
	}

	const FString RequestedSlotId = PreferredSlotId.TrimStartAndEnd();
	if (RequestedSlotId.IsEmpty())
	{
		UserState->PendingSlotResult = Results[0];
		return UserState->PendingSlotResult.IsValid();
	}

	for (const FSmartObjectRequestResult& Result : Results)
	{
		if (!Result.IsValid())
		{
			continue;
		}

		const FString CandidateSlotId = LexToString(Result.SlotHandle).TrimStartAndEnd();
		if (CandidateSlotId.Equals(RequestedSlotId, ESearchCase::CaseSensitive))
		{
			UserState->PendingSlotResult = Result;
			return true;
		}
	}

	UserState->PendingSlotResult = FSmartObjectRequestResult();
	return false;
#else
	return false;
#endif
}

bool USmartObjectBridgeContext::ClaimSlot(AActor* UserActor, const int32 ClaimPriority)
{
#if AINPC_WITH_SMARTOBJECTS
	if (!RuntimeData.IsValid() || !IsValid(UserActor))
	{
		return false;
	}

	FObjectKey UserKey;
	if (!ResolveUserKey(UserActor, UserKey))
	{
		return false;
	}

	FSmartObjectUserRuntimeState* UserState = FindUserState(*RuntimeData, UserKey, UserActor, false);
	if (!UserState || !UserState->PendingSlotResult.IsValid())
	{
		return false;
	}

	UWorld* World = UserActor->GetWorld();
	if (!IsValid(World))
	{
		return false;
	}

	RuntimeData->LastKnownWorld = World;
	RuntimeData->LastResolvedUserActor = UserActor;

	USmartObjectSubsystem* SmartObjectSubsystem = USmartObjectSubsystem::GetCurrent(World);
	if (!IsValid(SmartObjectSubsystem))
	{
		return false;
	}

	if (UserState->ActiveClaimHandle.IsValid())
	{
		ReleaseSlotForUser(UserActor);
	}

	const FSmartObjectClaimHandle NewClaimHandle = SmartObjectSubsystem->MarkSlotAsClaimed(
		UserState->PendingSlotResult.SlotHandle,
		ToSmartObjectClaimPriority(ClaimPriority),
		FConstStructView::Make(FSmartObjectActorUserData(UserActor)));
	if (!NewClaimHandle.IsValid())
	{
		return false;
	}

	UserState->ActiveClaimHandle = NewClaimHandle;
	RuntimeData->ActiveClaims.RemoveAllSwap(
		[&NewClaimHandle](const FTrackedSmartObjectClaim& TrackedClaim)
		{
			return TrackedClaim.Handle == NewClaimHandle;
		},
		EAllowShrinking::No);
	FTrackedSmartObjectClaim& TrackedClaim = RuntimeData->ActiveClaims.AddDefaulted_GetRef();
	TrackedClaim.Handle = NewClaimHandle;
	TrackedClaim.OwnerKey = UserKey;
	TrackedClaim.World = World;
	UserState->PendingSlotResult = FSmartObjectRequestResult();
	return true;
#else
	return false;
#endif
}

bool USmartObjectBridgeContext::UseSlot()
{
#if AINPC_WITH_SMARTOBJECTS
	if (!RuntimeData.IsValid())
	{
		return false;
	}

	if (AActor* LastUserActor = RuntimeData->LastResolvedUserActor.Get())
	{
		return UseSlotForUser(LastUserActor);
	}

	for (const TPair<FObjectKey, TWeakObjectPtr<AActor>>& Entry : RuntimeData->UserActors)
	{
		if (!Entry.Value.IsValid())
		{
			continue;
		}

		if (const FSmartObjectUserRuntimeState* UserState = FindUserState(*RuntimeData, Entry.Key))
		{
			if (UserState->ActiveClaimHandle.IsValid())
			{
				return UseSlotForUser(Entry.Value.Get());
			}
		}
	}

	return false;
#else
	return false;
#endif
}

bool USmartObjectBridgeContext::UseSlotForUser(AActor* UserActor)
{
#if AINPC_WITH_SMARTOBJECTS
	if (!RuntimeData.IsValid())
	{
		return false;
	}

	FObjectKey UserKey;
	if (!ResolveUserKey(UserActor, UserKey))
	{
		return false;
	}

	FSmartObjectUserRuntimeState* UserState = FindUserState(*RuntimeData, UserKey, UserActor, false);
	if (!UserState || !UserState->ActiveClaimHandle.IsValid())
	{
		return false;
	}

	UWorld* World = ResolveClaimWorld(*RuntimeData, UserState->ActiveClaimHandle, this);
	if (!IsValid(World))
	{
		return false;
	}

	RuntimeData->LastKnownWorld = World;
	RuntimeData->LastResolvedUserActor = UserActor;

	USmartObjectSubsystem* SmartObjectSubsystem = USmartObjectSubsystem::GetCurrent(World);
	if (!IsValid(SmartObjectSubsystem))
	{
		return false;
	}

	if (!SmartObjectSubsystem->IsClaimedSmartObjectValid(UserState->ActiveClaimHandle))
	{
		return false;
	}

	const USmartObjectBehaviorDefinition* OccupiedBehavior = SmartObjectSubsystem->MarkSlotAsOccupied(
		UserState->ActiveClaimHandle,
		USmartObjectBehaviorDefinition::StaticClass());
	if (!IsValid(OccupiedBehavior))
	{
		ReleaseSlot();
		return false;
	}

	return true;
#else
	return false;
#endif
}

bool USmartObjectBridgeContext::ReleaseSlot()
{
#if AINPC_WITH_SMARTOBJECTS
	if (!RuntimeData.IsValid())
	{
		return false;
	}

	if (AActor* LastUserActor = RuntimeData->LastResolvedUserActor.Get())
	{
		return ReleaseSlotForUser(LastUserActor);
	}

	for (const TPair<FObjectKey, TWeakObjectPtr<AActor>>& Entry : RuntimeData->UserActors)
	{
		if (!Entry.Value.IsValid())
		{
			continue;
		}

		if (const FSmartObjectUserRuntimeState* UserState = FindUserState(*RuntimeData, Entry.Key))
		{
			if (UserState->ActiveClaimHandle.IsValid())
			{
				return ReleaseSlotForUser(Entry.Value.Get());
			}
		}
	}

	return false;
#else
	return false;
#endif
}

bool USmartObjectBridgeContext::ReleaseSlotForUser(AActor* UserActor)
{
#if AINPC_WITH_SMARTOBJECTS
	if (!RuntimeData.IsValid())
	{
		return false;
	}

	FObjectKey UserKey;
	if (!ResolveUserKey(UserActor, UserKey))
	{
		return false;
	}

	FSmartObjectUserRuntimeState* UserState = FindUserState(*RuntimeData, UserKey, UserActor, false);
	if (!UserState || !UserState->ActiveClaimHandle.IsValid())
	{
		return false;
	}

	const FSmartObjectClaimHandle ClaimToRelease = UserState->ActiveClaimHandle;
	UWorld* World = ResolveClaimWorld(*RuntimeData, ClaimToRelease, this);

	USmartObjectSubsystem* SmartObjectSubsystem = IsValid(World) ? USmartObjectSubsystem::GetCurrent(World) : nullptr;
	if (IsValid(SmartObjectSubsystem) && SmartObjectSubsystem->IsClaimedSmartObjectValid(ClaimToRelease))
	{
		SmartObjectSubsystem->MarkSlotAsFree(ClaimToRelease);
	}

	RuntimeData->ActiveClaims.RemoveAllSwap(
		[&ClaimToRelease](const FTrackedSmartObjectClaim& TrackedClaim)
		{
			return TrackedClaim.Handle == ClaimToRelease;
		},
		EAllowShrinking::No);
	UserState->ActiveClaimHandle = FSmartObjectClaimHandle::InvalidHandle;
	RuntimeData->LastResolvedUserActor = UserActor;
	return true;
#else
	return false;
#endif
}

bool USmartObjectBridgeContext::HasActiveClaim() const
{
#if AINPC_WITH_SMARTOBJECTS
	if (!RuntimeData.IsValid())
	{
		return false;
	}

	if (const AActor* LastUserActor = RuntimeData->LastResolvedUserActor.Get())
	{
		return HasActiveClaimForUser(LastUserActor);
	}

	for (const TPair<FObjectKey, TWeakObjectPtr<AActor>>& Entry : RuntimeData->UserActors)
	{
		if (Entry.Value.IsValid() && HasActiveClaimForUser(Entry.Value.Get()))
		{
			return true;
		}
	}

	return false;
#else
	return false;
#endif
}

bool USmartObjectBridgeContext::HasActiveClaimForUser(const AActor* UserActor) const
{
#if AINPC_WITH_SMARTOBJECTS
	if (!RuntimeData.IsValid() || !IsValid(UserActor))
	{
		return false;
	}

	const FObjectKey UserKey(UserActor);
	const FSmartObjectUserRuntimeState* UserState = FindUserState(*RuntimeData, UserKey);
	return UserState && UserState->ActiveClaimHandle.IsValid();
#else
	return false;
#endif
}

bool USmartObjectBridgeContext::GetClaimedSlotTransform(FTransform& OutTransform) const
{
#if AINPC_WITH_SMARTOBJECTS
	if (!RuntimeData.IsValid())
	{
		return false;
	}

	if (const AActor* LastUserActor = RuntimeData->LastResolvedUserActor.Get())
	{
		return GetClaimedSlotTransformForUser(LastUserActor, OutTransform);
	}

	for (const TPair<FObjectKey, TWeakObjectPtr<AActor>>& Entry : RuntimeData->UserActors)
	{
		if (Entry.Value.IsValid() && GetClaimedSlotTransformForUser(Entry.Value.Get(), OutTransform))
		{
			return true;
		}
	}

	return false;
#else
	OutTransform = FTransform::Identity;
	return false;
#endif
}

bool USmartObjectBridgeContext::GetClaimedSlotTransformForUser(const AActor* UserActor, FTransform& OutTransform) const
{
#if AINPC_WITH_SMARTOBJECTS
	if (!RuntimeData.IsValid() || !IsValid(UserActor))
	{
		return false;
	}

	const FObjectKey UserKey(UserActor);
	const FSmartObjectUserRuntimeState* UserState = FindUserState(*RuntimeData, UserKey);
	if (!UserState || !UserState->ActiveClaimHandle.IsValid())
	{
		return false;
	}

	UWorld* World = ResolveClaimWorld(*RuntimeData, UserState->ActiveClaimHandle, this);
	if (!IsValid(World))
	{
		return false;
	}

	USmartObjectSubsystem* SmartObjectSubsystem = USmartObjectSubsystem::GetCurrent(World);
	if (!IsValid(SmartObjectSubsystem))
	{
		return false;
	}

	return SmartObjectSubsystem->GetSlotTransform(UserState->ActiveClaimHandle, OutTransform);
#else
	OutTransform = FTransform::Identity;
	return false;
#endif
}

void USmartObjectBridgeContext::HandleWorldCleanup(UWorld* World, const bool bSessionEnded, const bool bCleanupResources)
{
	ReleaseActiveClaimHandlesForWorld(World);
}

void USmartObjectBridgeContext::ReleaseActiveClaimHandlesForWorld(UWorld* World)
{
	if (!RuntimeData.IsValid())
	{
		return;
	}

#if AINPC_WITH_SMARTOBJECTS
	USmartObjectSubsystem* CleanupWorldSubsystem = IsValid(World) ? USmartObjectSubsystem::GetCurrent(World) : nullptr;
#endif

	for (int32 ClaimIndex = RuntimeData->ActiveClaims.Num() - 1; ClaimIndex >= 0; --ClaimIndex)
	{
		const FTrackedSmartObjectClaim& TrackedClaim = RuntimeData->ActiveClaims[ClaimIndex];
		UWorld* ClaimWorld = TrackedClaim.World.Get();
		const bool bClaimBelongsToCleanupWorld = !IsValid(World) || ClaimWorld == World;
		if (!bClaimBelongsToCleanupWorld)
		{
			continue;
		}

#if AINPC_WITH_SMARTOBJECTS
		USmartObjectSubsystem* ClaimSubsystem = nullptr;
		if (IsValid(World) && ClaimWorld == World)
		{
			ClaimSubsystem = CleanupWorldSubsystem;
		}
		else if (IsValid(ClaimWorld))
		{
			ClaimSubsystem = USmartObjectSubsystem::GetCurrent(ClaimWorld);
		}

		if (TrackedClaim.Handle.IsValid() && IsValid(ClaimSubsystem) && ClaimSubsystem->IsClaimedSmartObjectValid(TrackedClaim.Handle))
		{
			ClaimSubsystem->MarkSlotAsFree(TrackedClaim.Handle);
		}
		if (FSmartObjectUserRuntimeState* UserState = RuntimeData->UserStateByKey.Find(TrackedClaim.OwnerKey))
		{
			if (UserState->ActiveClaimHandle == TrackedClaim.Handle)
			{
				UserState->ActiveClaimHandle = FSmartObjectClaimHandle::InvalidHandle;
			}
		}
#endif

		RuntimeData->ActiveClaims.RemoveAtSwap(ClaimIndex, 1, EAllowShrinking::No);
	}

#if AINPC_WITH_SMARTOBJECTS
	TArray<FObjectKey> KeysToRemove;
	for (const TPair<FObjectKey, TWeakObjectPtr<AActor>>& Entry : RuntimeData->UserActors)
	{
		AActor* UserActor = Entry.Value.Get();
		const bool bRemoveForInvalidActor = !IsValid(UserActor);
		const bool bRemoveForWorldCleanup = IsValid(World) && IsValid(UserActor) && UserActor->GetWorld() == World;
		if (!IsValid(World) || bRemoveForInvalidActor || bRemoveForWorldCleanup)
		{
			KeysToRemove.Add(Entry.Key);
			continue;
		}

		if (FSmartObjectUserRuntimeState* UserState = RuntimeData->UserStateByKey.Find(Entry.Key))
		{
			if (!IsValid(World) || (IsValid(UserActor) && UserActor->GetWorld() == World))
			{
				UserState->PendingSlotResult = FSmartObjectRequestResult();
			}
		}
	}

	for (const FObjectKey& UserKey : KeysToRemove)
	{
		RuntimeData->UserActors.Remove(UserKey);
		RuntimeData->UserStateByKey.Remove(UserKey);
	}

	if (IsValid(RuntimeData->LastResolvedUserActor.Get()))
	{
		const AActor* LastUserActor = RuntimeData->LastResolvedUserActor.Get();
		if (!IsValid(World) || LastUserActor->GetWorld() == World)
		{
			RuntimeData->LastResolvedUserActor.Reset();
		}
	}

	if (!IsValid(World) || RuntimeData->LastKnownWorld.Get() == World)
	{
		RuntimeData->LastKnownWorld.Reset();
	}
#endif

	if (!IsValid(World))
	{
		RuntimeData->LastResolvedUserActor.Reset();
	}
}

#if defined(WITH_DEV_AUTOMATION_TESTS) && WITH_DEV_AUTOMATION_TESTS
void USmartObjectBridgeContext::AddTrackedClaimForTest(UWorld* World)
{
	if (!RuntimeData.IsValid())
	{
		RuntimeData = MakeUnique<FSmartObjectBridgeRuntimeData>();
	}

	FTrackedSmartObjectClaim& TrackedClaim = RuntimeData->ActiveClaims.AddDefaulted_GetRef();
#if AINPC_WITH_SMARTOBJECTS
	TrackedClaim.Handle = FSmartObjectClaimHandle::InvalidHandle;
#endif
	TrackedClaim.World = World;
}

int32 USmartObjectBridgeContext::GetTrackedClaimCountForTest() const
{
	return RuntimeData.IsValid() ? RuntimeData->ActiveClaims.Num() : 0;
}

int32 USmartObjectBridgeContext::GetTrackedClaimCountForWorldForTest(UWorld* World) const
{
	if (!RuntimeData.IsValid())
	{
		return 0;
	}

	int32 ClaimCount = 0;
	for (const FTrackedSmartObjectClaim& TrackedClaim : RuntimeData->ActiveClaims)
	{
		if (TrackedClaim.World.Get() == World)
		{
			++ClaimCount;
		}
	}

	return ClaimCount;
}

void USmartObjectBridgeContext::TriggerWorldCleanupForTest(UWorld* World)
{
	HandleWorldCleanup(World, false, false);
}
#endif

#undef AINPC_WITH_SMARTOBJECTS
