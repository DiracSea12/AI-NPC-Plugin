#pragma once

#include "IRelevanceScorer.h"

class AINPCMEMORY_API FSimpleRelevanceScorer : public IRelevanceScorer
{
public:
	virtual float CalculateRelevance(const FString& Query, const FString& Content) const override;
};
