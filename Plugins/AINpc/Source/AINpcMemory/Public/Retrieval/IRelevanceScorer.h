#pragma once

#include "CoreMinimal.h"

class AINPCMEMORY_API IRelevanceScorer
{
public:
	virtual ~IRelevanceScorer() = default;
	virtual float CalculateRelevance(const FString& Query, const FString& Content) const = 0;
};
