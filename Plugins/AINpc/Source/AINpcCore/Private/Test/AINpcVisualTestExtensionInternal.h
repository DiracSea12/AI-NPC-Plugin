#pragma once

#include "Test/AINpcVisualTestExtension.h"

#include "CoreMinimal.h"
#include "Templates/Function.h"

namespace AINpc::Visual::TestInternal
{
	struct FAdapterOwnerAvailabilityState
	{
		FName OwnerModuleName;
		bool bAvailable = true;
	};

	struct FAdapterRunViewCreateResult
	{
		FString Diagnostic;

		bool IsSuccess() const { return Diagnostic.IsEmpty(); }
	};

	struct FAdapterViewLookupResult
	{
		FString Diagnostic;

		static FAdapterViewLookupResult Success(TSharedPtr<IAINpcVisualAdapterInstance> InAdapter, TSharedPtr<FAdapterOwnerAvailabilityState> InOwnerState, FString InUnavailableDiagnostic)
		{
			FAdapterViewLookupResult Result;
			Result.Adapter = MoveTemp(InAdapter);
			Result.OwnerState = MoveTemp(InOwnerState);
			Result.UnavailableDiagnostic = MoveTemp(InUnavailableDiagnostic);
			return Result;
		}

		static FAdapterViewLookupResult Failure(FString InDiagnostic)
		{
			FAdapterViewLookupResult Result;
			Result.Diagnostic = MoveTemp(InDiagnostic);
			return Result;
		}

		bool IsSuccess() const
		{
			check(IsInGameThread());
			const TSharedPtr<FAdapterOwnerAvailabilityState> Owner = OwnerState.Pin();
			return Adapter.IsValid() && Diagnostic.IsEmpty() && Owner.IsValid() && Owner->bAvailable;
		}

		bool UseAdapter(TFunctionRef<void(IAINpcVisualAdapterInstance&)> Use, FString* OutDiagnostic = nullptr) const
		{
			check(IsInGameThread());
			const TSharedPtr<FAdapterOwnerAvailabilityState> Owner = OwnerState.Pin();
			const TSharedPtr<IAINpcVisualAdapterInstance> Instance = Adapter.Pin();
			if (!Instance.IsValid() || !Diagnostic.IsEmpty() || !Owner.IsValid() || !Owner->bAvailable)
			{
				if (OutDiagnostic != nullptr)
				{
					*OutDiagnostic = !Diagnostic.IsEmpty() ? Diagnostic : UnavailableDiagnostic;
				}
				return false;
			}
			Use(*Instance);
			return true;
		}

	private:
		TWeakPtr<IAINpcVisualAdapterInstance> Adapter;
		TWeakPtr<FAdapterOwnerAvailabilityState> OwnerState;
		FString UnavailableDiagnostic;
	};

	struct FVisualAdapterDescriptorValidationResult
	{
		FAINpcVisualAdapterDescriptor Descriptor;
		FString Diagnostic;

		bool IsSuccess() const { return Diagnostic.IsEmpty(); }
	};

	FString LexToString(EAINpcVisualAdapterCategory Category);
	FVisualAdapterDescriptorValidationResult FindRegisteredAdapterDescriptor(FName AdapterId, const TCHAR* Stage, const FString& TestId);
	FVisualAdapterDescriptorValidationResult FindRegisteredAdapterDescriptor(EAINpcVisualAdapterCategory Category, FName AdapterId, const TCHAR* Stage, const FString& TestId);
	FVisualAdapterDescriptorValidationResult FindObservationProviderDeclaration(const FString& ObservationName, FAINpcVisualObservationDeclaration& OutDeclaration, const TCHAR* Stage, const FString& TestId);
	bool SetRegisteredAdapterOwnerAvailabilityForTest(EAINpcVisualAdapterCategory Category, FName AdapterId, FName OwnerModuleName, bool bAvailable);

	class FAdapterRunView
	{
	public:
		~FAdapterRunView();

		FAdapterRunView(const FAdapterRunView&) = delete;
		FAdapterRunView& operator=(const FAdapterRunView&) = delete;
		FAdapterRunView(FAdapterRunView&&) = default;
		FAdapterRunView& operator=(FAdapterRunView&&) = default;

		static FAdapterRunViewCreateResult Create(const FAINpcVisualAdapterCreateContext& Context, TSharedPtr<FAdapterRunView>& OutView);

		FAdapterViewLookupResult FindAdapter(EAINpcVisualAdapterCategory Category, FName AdapterId, const TCHAR* Stage) const;
		void AddReleaseCallback(TFunction<void()> ReleaseCallback);
		void EndScenario(const TCHAR* Stage);

	private:
		FAdapterRunView(FString InTestId, FString InRunId);

		struct FEntry;

		FString TestId;
		FString RunId;
		TArray<FEntry> Entries;
		TArray<TFunction<void()>> ReleaseCallbacks;
		bool bEnded = false;
	};
}
