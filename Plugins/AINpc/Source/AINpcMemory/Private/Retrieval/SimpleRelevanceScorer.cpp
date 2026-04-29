#include "Retrieval/SimpleRelevanceScorer.h"

float FSimpleRelevanceScorer::CalculateRelevance(const FString& Query, const FString& Content) const
{
	if (Query.IsEmpty() || Content.IsEmpty())
	{
		return 0.0f;
	}

	TArray<FString> QueryWords;
	Query.ParseIntoArray(QueryWords, TEXT(" "), true);

	int32 MatchCount = 0;
	for (const FString& Word : QueryWords)
	{
		if (Content.Contains(Word, ESearchCase::IgnoreCase))
		{
			MatchCount++;
		}
	}

	return QueryWords.Num() > 0 ? static_cast<float>(MatchCount) / QueryWords.Num() : 0.0f;
}
