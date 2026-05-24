#pragma once

#include "CoreMinimal.h"
#include "Test/AINpcVisualTest.h"

class FAINpcDataDrivenVisualScenarioTest final : public IAINpcVisualTest
{
public:
	FAINpcDataDrivenVisualScenarioTest(const FAINpcVisualTestContext& Context, FAINpcVisualScenarioConfig InConfig);
	~FAINpcDataDrivenVisualScenarioTest();

	bool Start(FString& OutFailureReason) override;
	void Poll() override;
	bool IsComplete() const override;
	bool HasFailed() const override;
	const FString& GetFailureReason() const override;
	FString BuildSummary() const override;
	FAINpcVisualTestObservations BuildObservations() const override;
	TArray<FAINpcVisualTestStepDiagnostic> BuildStepDiagnostics() const override;

private:
	struct FImplementation;
	TUniquePtr<FImplementation> Impl;
};
