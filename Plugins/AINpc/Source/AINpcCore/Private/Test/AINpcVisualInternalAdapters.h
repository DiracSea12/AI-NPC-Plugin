#pragma once

#include "CoreMinimal.h"

namespace AINpcVisualInternalAdapters
{
	struct FDescriptor
	{
		FString Id;
		TSet<FString> Capabilities;
	};

	inline const TCHAR* CharacterFixtureAdapterId() { return TEXT("builtin.characterFixture"); }
	inline const TCHAR* NpcEventAdapterId() { return TEXT("builtin.npcEvent"); }
	inline const TCHAR* SmartObjectActionAdapterId() { return TEXT("builtin.smartObjectAction"); }
	inline const TCHAR* FixturePrepareCapability() { return TEXT("fixture.prepare"); }
	inline const TCHAR* WorldEventEmitCapability() { return TEXT("world.event.emit"); }
	inline const TCHAR* ExecuteLatestIntentCapability() { return TEXT("action.executeLatestIntent"); }

	inline FDescriptor MakeDescriptor(const TCHAR* Id, const TCHAR* Capability)
	{
		FDescriptor Descriptor;
		Descriptor.Id = Id;
		Descriptor.Capabilities.Add(Capability);
		return Descriptor;
	}

	inline TArray<FDescriptor> GetBuiltInCatalog()
	{
		TArray<FDescriptor> Catalog;
		Catalog.Add(MakeDescriptor(CharacterFixtureAdapterId(), FixturePrepareCapability()));
		Catalog.Add(MakeDescriptor(NpcEventAdapterId(), WorldEventEmitCapability()));
		Catalog.Add(MakeDescriptor(SmartObjectActionAdapterId(), ExecuteLatestIntentCapability()));
		return Catalog;
	}

	inline bool ValidateUniqueAdapterIds(const TArray<FDescriptor>& Catalog, FString& OutFailure)
	{
		TSet<FString> SeenIds;
		for (const FDescriptor& Descriptor : Catalog)
		{
			if (SeenIds.Contains(Descriptor.Id))
			{
				OutFailure = FString::Printf(TEXT("duplicate internal adapter id '%s'"), *Descriptor.Id);
				return false;
			}
			SeenIds.Add(Descriptor.Id);
		}
		return true;
	}

	inline bool ValidateBuiltInCatalog(FString& OutFailure)
	{
		return ValidateUniqueAdapterIds(GetBuiltInCatalog(), OutFailure);
	}

	inline const FDescriptor* FindBuiltInDescriptor(const FString& AdapterId)
	{
		static const TArray<FDescriptor> Catalog = GetBuiltInCatalog();
		for (const FDescriptor& Descriptor : Catalog)
		{
			if (Descriptor.Id == AdapterId)
			{
				return &Descriptor;
			}
		}
		return nullptr;
	}

	inline bool RequireCapability(const FString& AdapterId, const FString& Capability, FString& OutFailure)
	{
		const FDescriptor* Descriptor = FindBuiltInDescriptor(AdapterId);
		if (!Descriptor || !Descriptor->Capabilities.Contains(Capability))
		{
			OutFailure = FString::Printf(TEXT("adapter '%s' unsupported capability '%s'"), *AdapterId, *Capability);
			return false;
		}
		return true;
	}
}
