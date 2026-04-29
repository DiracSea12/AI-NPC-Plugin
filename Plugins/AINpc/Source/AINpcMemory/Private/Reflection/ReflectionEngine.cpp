#include "Reflection/ReflectionEngine.h"

bool UReflectionEngine::ShouldTriggerReflection(const TArray<FMemoryEntry>& RecentMemories, float Threshold) const
{
	return CalculateCumulativeImportance(RecentMemories) > Threshold;
}

FMemoryEntry UReflectionEngine::GenerateInsight(const TArray<FMemoryEntry>& SourceMemories)
{
	FMemoryEntry Insight;
	Insight.Content = TEXT("Reflection insight generated from recent memories");
	Insight.Importance = 5.0f;
	Insight.Timestamp = FDateTime::Now();
	return Insight;
}

float UReflectionEngine::CalculateCumulativeImportance(const TArray<FMemoryEntry>& Memories) const
{
	float Total = 0.0f;
	for (const FMemoryEntry& Memory : Memories)
	{
		Total += Memory.Importance;
	}
	return Total;
}
