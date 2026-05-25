#include "Test/AINpcVisualTestExtension.h"

namespace
{
	TArray<FAINpcVisualAdapterDescriptor>& GetVisualAdapterDescriptors()
	{
		static TArray<FAINpcVisualAdapterDescriptor> Descriptors;
		return Descriptors;
	}

	FString LexToString(const EAINpcVisualAdapterCategory Category)
	{
		switch (Category)
		{
		case EAINpcVisualAdapterCategory::FixtureResolver:
			return TEXT("FixtureResolver");
		case EAINpcVisualAdapterCategory::ObservationProvider:
			return TEXT("ObservationProvider");
		case EAINpcVisualAdapterCategory::ActionAdapter:
			return TEXT("ActionAdapter");
		default:
			return TEXT("Unknown");
		}
	}

	bool HasOnlyFactoryForCategory(const FAINpcVisualAdapterDescriptor& Descriptor)
	{
		const int32 FactoryCount = (Descriptor.CreateFixtureResolver ? 1 : 0)
			+ (Descriptor.CreateObservationProvider ? 1 : 0)
			+ (Descriptor.CreateActionAdapter ? 1 : 0);
		if (FactoryCount != 1)
		{
			return false;
		}

		switch (Descriptor.Category)
		{
		case EAINpcVisualAdapterCategory::FixtureResolver:
			return Descriptor.CreateFixtureResolver != nullptr;
		case EAINpcVisualAdapterCategory::ObservationProvider:
			return Descriptor.CreateObservationProvider != nullptr;
		case EAINpcVisualAdapterCategory::ActionAdapter:
			return Descriptor.CreateActionAdapter != nullptr;
		default:
			return false;
		}
	}

	FAINpcVisualAdapterRegistrationResult ValidateCapabilities(const FAINpcVisualAdapterDescriptor& Descriptor)
	{
		if (Descriptor.Capabilities.IsEmpty())
		{
			return FAINpcVisualAdapterRegistrationResult::Failure(FString::Printf(TEXT("visual adapter '%s' requires at least one declared capability"), *Descriptor.AdapterId.ToString()));
		}

		for (const FString& Capability : Descriptor.Capabilities)
		{
			if (Capability.IsEmpty())
			{
				return FAINpcVisualAdapterRegistrationResult::Failure(FString::Printf(TEXT("visual adapter '%s' capability must not be empty"), *Descriptor.AdapterId.ToString()));
			}
		}

		return FAINpcVisualAdapterRegistrationResult::Success();
	}
}

FAINpcVisualAdapterRegistrationResult FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(const FAINpcVisualAdapterDescriptor& Descriptor)
{
	if (Descriptor.AdapterId.IsNone())
	{
		return FAINpcVisualAdapterRegistrationResult::Failure(TEXT("visual adapter registration requires a non-empty adapter id"));
	}
	if (Descriptor.OwnerModuleName.IsNone())
	{
		return FAINpcVisualAdapterRegistrationResult::Failure(FString::Printf(TEXT("visual adapter '%s' registration requires a non-empty owner module name"), *Descriptor.AdapterId.ToString()));
	}
	if (!HasOnlyFactoryForCategory(Descriptor))
	{
		return FAINpcVisualAdapterRegistrationResult::Failure(FString::Printf(TEXT("visual adapter '%s' category '%s' requires exactly one matching category-specific factory"), *Descriptor.AdapterId.ToString(), *LexToString(Descriptor.Category)));
	}

	const FAINpcVisualAdapterRegistrationResult CapabilityValidation = ValidateCapabilities(Descriptor);
	if (!CapabilityValidation.IsSuccess())
	{
		return CapabilityValidation;
	}

	for (const FAINpcVisualAdapterDescriptor& Existing : GetVisualAdapterDescriptors())
	{
		if (Existing.AdapterId == Descriptor.AdapterId)
		{
			return FAINpcVisualAdapterRegistrationResult::Failure(FString::Printf(TEXT("duplicate visual adapter id '%s' owned by '%s'; ids must be globally unique across categories"), *Descriptor.AdapterId.ToString(), *Existing.OwnerModuleName.ToString()));
		}
	}

	GetVisualAdapterDescriptors().Add(Descriptor);
	return FAINpcVisualAdapterRegistrationResult::Success();
}

FAINpcVisualAdapterRegistrationResult FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(const EAINpcVisualAdapterCategory Category, const FName AdapterId, const FName OwnerModuleName)
{
	if (AdapterId.IsNone())
	{
		return FAINpcVisualAdapterRegistrationResult::Failure(TEXT("visual adapter unregistration requires a non-empty adapter id"));
	}
	if (OwnerModuleName.IsNone())
	{
		return FAINpcVisualAdapterRegistrationResult::Failure(FString::Printf(TEXT("visual adapter '%s' unregistration requires a non-empty owner module name"), *AdapterId.ToString()));
	}

	TArray<FAINpcVisualAdapterDescriptor>& Descriptors = GetVisualAdapterDescriptors();
	for (int32 Index = 0; Index < Descriptors.Num(); ++Index)
	{
		const FAINpcVisualAdapterDescriptor& Existing = Descriptors[Index];
		if (Existing.AdapterId == AdapterId && Existing.Category == Category)
		{
			if (Existing.OwnerModuleName != OwnerModuleName)
			{
				return FAINpcVisualAdapterRegistrationResult::Failure(FString::Printf(TEXT("visual adapter '%s' category '%s' is owned by '%s', not '%s'"), *AdapterId.ToString(), *LexToString(Category), *Existing.OwnerModuleName.ToString(), *OwnerModuleName.ToString()));
			}
			Descriptors.RemoveAt(Index);
			return FAINpcVisualAdapterRegistrationResult::Success();
		}
	}

	return FAINpcVisualAdapterRegistrationResult::Failure(FString::Printf(TEXT("visual adapter '%s' category '%s' is not registered for owner '%s'"), *AdapterId.ToString(), *LexToString(Category), *OwnerModuleName.ToString()));
}
