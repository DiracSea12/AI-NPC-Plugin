#pragma once

#include "CoreMinimal.h"
#include "Test/AINpcVisualTest.h"

class AINPCCORE_API FAINpcVisualTestRegistry
{
public:
	static const TArray<FAINpcVisualTestDescriptor>& GetDescriptors();
	static const FAINpcVisualTestDescriptor* Find(const FString& TestId);
	static FString GetRegisteredTestIds();
};
