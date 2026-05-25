#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "CoreTypes.h"
#include "Templates/SharedPointer.h"
#include "UObject/NameTypes.h"

class UWorld;

struct FAINpcVisualAdapterRegistrationResult
{
	bool bSuccess = false;
	FString Diagnostic;

	bool IsSuccess() const { return bSuccess; }
	explicit operator bool() const { return bSuccess; }

	static FAINpcVisualAdapterRegistrationResult Success()
	{
		FAINpcVisualAdapterRegistrationResult Result;
		Result.bSuccess = true;
		return Result;
	}

	static FAINpcVisualAdapterRegistrationResult Failure(FString InDiagnostic)
	{
		FAINpcVisualAdapterRegistrationResult Result;
		Result.bSuccess = false;
		Result.Diagnostic = MoveTemp(InDiagnostic);
		return Result;
	}
};

class FAINpcVisualAdapterDiagnosticSink
{
public:
	void AddDiagnostic(FString Diagnostic)
	{
		Diagnostics.Add(MoveTemp(Diagnostic));
	}

	const TArray<FString>& GetDiagnostics() const
	{
		return Diagnostics;
	}

private:
	TArray<FString> Diagnostics;
};

struct FAINpcVisualAdapterCreateContext
{
	UWorld* World = nullptr;
	FString TestId;
	FString RunId;
	TArray<FString> StoryIds;
	TArray<FString> PhaseIds;
	FAINpcVisualAdapterDiagnosticSink* DiagnosticSink = nullptr;
};

class IAINpcVisualAdapterInstance
{
public:
	virtual ~IAINpcVisualAdapterInstance() = default;
};

class IAINpcVisualFixtureResolverAdapter : public IAINpcVisualAdapterInstance
{
public:
	~IAINpcVisualFixtureResolverAdapter() override = default;
};

class IAINpcVisualObservationProviderAdapter : public IAINpcVisualAdapterInstance
{
public:
	~IAINpcVisualObservationProviderAdapter() override = default;
};

class IAINpcVisualActionAdapter : public IAINpcVisualAdapterInstance
{
public:
	~IAINpcVisualActionAdapter() override = default;
};

struct FAINpcVisualAdapterFactoryResult
{
	TSharedPtr<IAINpcVisualAdapterInstance> Adapter;
	FString Diagnostic;

	bool IsSuccess() const { return Adapter.IsValid() && Diagnostic.IsEmpty(); }

	static FAINpcVisualAdapterFactoryResult Failure(FString InDiagnostic)
	{
		FAINpcVisualAdapterFactoryResult Result;
		Result.Diagnostic = MoveTemp(InDiagnostic);
		return Result;
	}
};

using FAINpcVisualFixtureResolverFactory = FAINpcVisualAdapterFactoryResult (*)(const FAINpcVisualAdapterCreateContext&);
using FAINpcVisualObservationProviderFactory = FAINpcVisualAdapterFactoryResult (*)(const FAINpcVisualAdapterCreateContext&);
using FAINpcVisualActionAdapterFactory = FAINpcVisualAdapterFactoryResult (*)(const FAINpcVisualAdapterCreateContext&);

enum class EAINpcVisualAdapterCategory : uint8
{
	FixtureResolver,
	ObservationProvider,
	ActionAdapter
};

struct FAINpcVisualAdapterDescriptor
{
	EAINpcVisualAdapterCategory Category = EAINpcVisualAdapterCategory::FixtureResolver;
	FName AdapterId;
	FName OwnerModuleName;
	TArray<FString> Capabilities;
	TArray<FString> ObservationDeclarations;
	FAINpcVisualFixtureResolverFactory CreateFixtureResolver = nullptr;
	FAINpcVisualObservationProviderFactory CreateObservationProvider = nullptr;
	FAINpcVisualActionAdapterFactory CreateActionAdapter = nullptr;
};

class AINPCCORE_API FAINpcVisualTestExtensionRegistry
{
public:
	static FAINpcVisualAdapterRegistrationResult RegisterVisualTestAdapter(const FAINpcVisualAdapterDescriptor& Descriptor);
	static FAINpcVisualAdapterRegistrationResult UnregisterVisualTestAdapter(EAINpcVisualAdapterCategory Category, FName AdapterId, FName OwnerModuleName);
};
