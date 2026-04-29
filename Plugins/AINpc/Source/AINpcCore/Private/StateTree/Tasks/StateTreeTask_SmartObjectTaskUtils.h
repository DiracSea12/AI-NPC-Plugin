#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

namespace AINpc::SmartObjectTaskUtils
{
inline AActor* ResolveUserActorFromOwnerObject(UObject* OwnerObject, TObjectPtr<AActor>& CachedUserActor)
{
	if (CachedUserActor)
	{
		return CachedUserActor.Get();
	}

	if (AController* Controller = Cast<AController>(OwnerObject))
	{
		if (APawn* ControlledPawn = Controller->GetPawn())
		{
			CachedUserActor = ControlledPawn;
			return ControlledPawn;
		}
	}

	if (AActor* OwnerActor = Cast<AActor>(OwnerObject))
	{
		CachedUserActor = OwnerActor;
		return OwnerActor;
	}

	return nullptr;
}
}

