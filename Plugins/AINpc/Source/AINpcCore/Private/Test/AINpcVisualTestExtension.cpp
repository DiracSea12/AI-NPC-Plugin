#include "Test/AINpcVisualTestExtension.h"

#include "Test/AINpcVisualTestExtensionInternal.h"

namespace
{
	using FVisualAdapterOwnerState = AINpc::Visual::TestInternal::FAdapterOwnerAvailabilityState;

	struct FVisualAdapterRegistryEntry
	{
		FAINpcVisualAdapterDescriptor Descriptor;
		TSharedPtr<FVisualAdapterOwnerState> OwnerState;
	};

	TArray<FVisualAdapterRegistryEntry>& GetVisualAdapterDescriptors()
	{
		static TArray<FVisualAdapterRegistryEntry> Descriptors;
		return Descriptors;
	}

	TMap<FName, FName>& GetVisualAdapterOwnerHistory()
	{
		static TMap<FName, FName> OwnerHistory;
		return OwnerHistory;
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

	FString MakeLifecycleDiagnostic(const TCHAR* Stage, const FName OwnerModuleName, const EAINpcVisualAdapterCategory Category, const FName AdapterId, const FString& TestId, const TCHAR* Reason)
	{
		return FString::Printf(TEXT("visual adapter lifecycle failure stage=%s owner=%s category=%s adapter=%s testId=%s reason=%s"),
			Stage ? Stage : TEXT("Unknown"),
			*OwnerModuleName.ToString(),
			*LexToString(Category),
			*AdapterId.ToString(),
			TestId.IsEmpty() ? TEXT("<none>") : *TestId,
			Reason ? Reason : TEXT("unknown"));
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

	FAINpcVisualAdapterFactoryResult CreateAdapterInstance(const FAINpcVisualAdapterDescriptor& Descriptor, const FAINpcVisualAdapterCreateContext& Context)
	{
		switch (Descriptor.Category)
		{
		case EAINpcVisualAdapterCategory::FixtureResolver:
			return Descriptor.CreateFixtureResolver(Context);
		case EAINpcVisualAdapterCategory::ObservationProvider:
			return Descriptor.CreateObservationProvider(Context);
		case EAINpcVisualAdapterCategory::ActionAdapter:
			return Descriptor.CreateActionAdapter(Context);
		default:
			return FAINpcVisualAdapterFactoryResult::Failure(MakeLifecycleDiagnostic(TEXT("ScenarioStart"), Descriptor.OwnerModuleName, Descriptor.Category, Descriptor.AdapterId, Context.TestId, TEXT("unknown category")));
		}
	}
}

namespace AINpc::Visual::TestInternal
{
	struct FAdapterRunView::FEntry
	{
		EAINpcVisualAdapterCategory Category = EAINpcVisualAdapterCategory::FixtureResolver;
		FName AdapterId;
		FName OwnerModuleName;
		TSharedPtr<FVisualAdapterOwnerState> OwnerState;
		TSharedPtr<IAINpcVisualAdapterInstance> Adapter;
	};

	FAdapterRunView::FAdapterRunView(FString InTestId, FString InRunId)
		: TestId(MoveTemp(InTestId))
		, RunId(MoveTemp(InRunId))
	{
	}

	FAdapterRunView::~FAdapterRunView()
	{
		EndScenario(TEXT("ScenarioEnd"));
	}

	FAdapterRunViewCreateResult FAdapterRunView::Create(const FAINpcVisualAdapterCreateContext& Context, TSharedPtr<FAdapterRunView>& OutView)
	{
		check(IsInGameThread());
		OutView.Reset();
		if (Context.World == nullptr)
		{
			return { TEXT("visual adapter lifecycle failure stage=RuntimeStartup owner=<none> category=<none> adapter=<none> testId=") + (Context.TestId.IsEmpty() ? TEXT("<none>") : Context.TestId) + TEXT(" reason=world unavailable before adapter creation") };
		}

		TSharedPtr<FAdapterRunView> View = MakeShareable(new FAdapterRunView(Context.TestId, Context.RunId));
		for (const FVisualAdapterRegistryEntry& Registered : GetVisualAdapterDescriptors())
		{
			if (!Registered.OwnerState.IsValid() || !Registered.OwnerState->bAvailable)
			{
				return { MakeLifecycleDiagnostic(TEXT("ScenarioStart"), Registered.Descriptor.OwnerModuleName, Registered.Descriptor.Category, Registered.Descriptor.AdapterId, Context.TestId, TEXT("owner unavailable")) };
			}

			FAINpcVisualAdapterFactoryResult FactoryResult = CreateAdapterInstance(Registered.Descriptor, Context);
			if (!FactoryResult.IsSuccess())
			{
				return { FactoryResult.Diagnostic.IsEmpty()
					? MakeLifecycleDiagnostic(TEXT("ScenarioStart"), Registered.Descriptor.OwnerModuleName, Registered.Descriptor.Category, Registered.Descriptor.AdapterId, Context.TestId, TEXT("factory returned no adapter"))
					: FactoryResult.Diagnostic };
			}

			FEntry& Entry = View->Entries.AddDefaulted_GetRef();
			Entry.Category = Registered.Descriptor.Category;
			Entry.AdapterId = Registered.Descriptor.AdapterId;
			Entry.OwnerModuleName = Registered.Descriptor.OwnerModuleName;
			Entry.OwnerState = Registered.OwnerState;
			Entry.Adapter = FactoryResult.Adapter;
		}

		OutView = MoveTemp(View);
		return {};
	}

	FAdapterViewLookupResult FAdapterRunView::FindAdapter(const EAINpcVisualAdapterCategory Category, const FName AdapterId, const TCHAR* Stage) const
	{
		check(IsInGameThread());
		for (const FEntry& Entry : Entries)
		{
			if (Entry.Category == Category && Entry.AdapterId == AdapterId)
			{
				if (bEnded)
				{
					return FAdapterViewLookupResult::Failure(MakeLifecycleDiagnostic(Stage, Entry.OwnerModuleName, Category, AdapterId, TestId, TEXT("scenario view released")));
				}
				if (!Entry.OwnerState.IsValid() || !Entry.OwnerState->bAvailable)
				{
					return FAdapterViewLookupResult::Failure(MakeLifecycleDiagnostic(Stage, Entry.OwnerModuleName, Category, AdapterId, TestId, TEXT("owner unavailable")));
				}
				return FAdapterViewLookupResult::Success(Entry.Adapter, Entry.OwnerState, MakeLifecycleDiagnostic(Stage, Entry.OwnerModuleName, Category, AdapterId, TestId, TEXT("owner unavailable")));
			}
		}

		const FName* OwnerModuleName = GetVisualAdapterOwnerHistory().Find(AdapterId);
		return FAdapterViewLookupResult::Failure(MakeLifecycleDiagnostic(Stage, OwnerModuleName ? *OwnerModuleName : NAME_None, Category, AdapterId, TestId, TEXT("adapter not present in run view")));
	}

	void FAdapterRunView::AddReleaseCallback(TFunction<void()> ReleaseCallback)
	{
		check(IsInGameThread());
		if (bEnded)
		{
			return;
		}
		ReleaseCallbacks.Add(MoveTemp(ReleaseCallback));
	}

	void FAdapterRunView::EndScenario(const TCHAR*)
	{
		check(IsInGameThread());
		if (bEnded)
		{
			return;
		}

		bEnded = true;
		TArray<TFunction<void()>> CallbacksToRelease = MoveTemp(ReleaseCallbacks);
		ReleaseCallbacks.Reset();
		for (FEntry& Entry : Entries)
		{
			Entry.Adapter.Reset();
		}
		for (TFunction<void()>& ReleaseCallback : CallbacksToRelease)
		{
			ReleaseCallback();
		}
	}
}

FAINpcVisualAdapterRegistrationResult FAINpcVisualTestExtensionRegistry::RegisterVisualTestAdapter(const FAINpcVisualAdapterDescriptor& Descriptor)
{
	check(IsInGameThread());
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

	for (const FVisualAdapterRegistryEntry& Existing : GetVisualAdapterDescriptors())
	{
		if (Existing.Descriptor.AdapterId == Descriptor.AdapterId)
		{
			return FAINpcVisualAdapterRegistrationResult::Failure(FString::Printf(TEXT("duplicate visual adapter id '%s' owned by '%s'; ids must be globally unique across categories"), *Descriptor.AdapterId.ToString(), *Existing.Descriptor.OwnerModuleName.ToString()));
		}
	}

	FVisualAdapterRegistryEntry& Entry = GetVisualAdapterDescriptors().AddDefaulted_GetRef();
	Entry.Descriptor = Descriptor;
	Entry.OwnerState = MakeShared<FVisualAdapterOwnerState>();
	Entry.OwnerState->OwnerModuleName = Descriptor.OwnerModuleName;
	GetVisualAdapterOwnerHistory().Add(Descriptor.AdapterId, Descriptor.OwnerModuleName);
	return FAINpcVisualAdapterRegistrationResult::Success();
}

FAINpcVisualAdapterRegistrationResult FAINpcVisualTestExtensionRegistry::UnregisterVisualTestAdapter(const EAINpcVisualAdapterCategory Category, const FName AdapterId, const FName OwnerModuleName)
{
	check(IsInGameThread());
	if (AdapterId.IsNone())
	{
		return FAINpcVisualAdapterRegistrationResult::Failure(TEXT("visual adapter unregistration requires a non-empty adapter id"));
	}
	if (OwnerModuleName.IsNone())
	{
		return FAINpcVisualAdapterRegistrationResult::Failure(FString::Printf(TEXT("visual adapter '%s' unregistration requires a non-empty owner module name"), *AdapterId.ToString()));
	}

	TArray<FVisualAdapterRegistryEntry>& Descriptors = GetVisualAdapterDescriptors();
	for (int32 Index = 0; Index < Descriptors.Num(); ++Index)
	{
		const FVisualAdapterRegistryEntry& Existing = Descriptors[Index];
		if (Existing.Descriptor.AdapterId == AdapterId && Existing.Descriptor.Category == Category)
		{
			if (Existing.Descriptor.OwnerModuleName != OwnerModuleName)
			{
				return FAINpcVisualAdapterRegistrationResult::Failure(MakeLifecycleDiagnostic(TEXT("ModuleShutdown"), Existing.Descriptor.OwnerModuleName, Category, AdapterId, FString(), *FString::Printf(TEXT("wrong owner '%s'"), *OwnerModuleName.ToString())));
			}
			Existing.OwnerState->bAvailable = false;
			Descriptors.RemoveAt(Index);
			return FAINpcVisualAdapterRegistrationResult::Success();
		}
	}

	return FAINpcVisualAdapterRegistrationResult::Failure(MakeLifecycleDiagnostic(TEXT("ModuleShutdown"), OwnerModuleName, Category, AdapterId, FString(), TEXT("adapter not registered for owner")));
}
