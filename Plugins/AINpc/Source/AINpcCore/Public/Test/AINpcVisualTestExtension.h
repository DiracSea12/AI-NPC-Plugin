#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "CoreTypes.h"
#include "Templates/SharedPointer.h"
#include "Test/AINpcVisualTest.h"
#include "UObject/NameTypes.h"
#include "UObject/WeakObjectPtrTemplates.h"

class AActor;
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

struct FAINpcVisualFixtureResolveRequest
{
	UWorld* World = nullptr;
	FString TestId;
	FString RunId;
	FName AdapterId;
	FString FixtureKind;
	FString ActorClass;
	FString ActorTag;
	FString TargetRef;
};

struct FAINpcVisualFixtureResolveResult
{
	bool bSuccess = false;
	TWeakObjectPtr<AActor> Actor;
	FString TargetRef;
	FString Diagnostic;
};

struct FAINpcVisualActionExecuteRequest
{
	FString TestId;
	FString RunId;
	int32 StepIndex = INDEX_NONE;
	FName AdapterId;
	FString ActionName;
	FString TargetRef;
	TWeakObjectPtr<AActor> TargetActor;
};

struct FAINpcVisualActionExecuteResult
{
	bool bAccepted = false;
	bool bSucceeded = false;
	FString Diagnostic;
	FString FailureReason;
};

struct FAINpcVisualObservationSampleRequest
{
	FString TestId;
	FString RunId;
	FName AdapterId;
	FString ObservationName;
	TWeakObjectPtr<AActor> SourceActor;
};

struct FAINpcVisualObservationSampleResult
{
	bool bSuccess = false;
	FAINpcVisualObservationRecord Observation;
	FString Diagnostic;
	FString FailureReason;
};

struct FAINpcVisualObservationDeclaration
{
	FString ObservationName;
	EAINpcVisualObservationValueType ValueType = EAINpcVisualObservationValueType::Boolean;
	FString SourceKind;
	FString SamplingMethod;
	FString Capability;
	bool bRequiresSourceObjectPath = false;
	bool bRequiresSourceClass = false;
};

class IAINpcVisualFixtureResolverAdapter : public IAINpcVisualAdapterInstance
{
public:
	~IAINpcVisualFixtureResolverAdapter() override = default;
	virtual FAINpcVisualFixtureResolveResult ResolveFixture(const FAINpcVisualFixtureResolveRequest& Request) = 0;
};

class IAINpcVisualObservationProviderAdapter : public IAINpcVisualAdapterInstance
{
public:
	~IAINpcVisualObservationProviderAdapter() override = default;
	virtual FAINpcVisualObservationSampleResult SampleObservation(const FAINpcVisualObservationSampleRequest& Request) = 0;
};

class IAINpcVisualActionAdapter : public IAINpcVisualAdapterInstance
{
public:
	~IAINpcVisualActionAdapter() override = default;
	virtual FAINpcVisualActionExecuteResult ExecuteAction(const FAINpcVisualActionExecuteRequest& Request) = 0;
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
	TArray<FAINpcVisualObservationDeclaration> ObservationDeclarations;
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
