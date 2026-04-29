#include "Components/NpcMemoryComponent.h"

UNpcMemoryComponent::UNpcMemoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UNpcMemoryComponent::AddWorkingMemory(const FMemoryEntry& Entry)
{
	WorkingMemory.Add(Entry);
	if (WorkingMemory.Num() > MaxWorkingMemorySize)
	{
		WorkingMemory.RemoveAt(0);
	}
}

void UNpcMemoryComponent::AddEpisodicMemory(const FMemoryEntry& Entry)
{
	if (Entry.Importance < ImportanceThreshold)
	{
		return;
	}

	EpisodicMemory.Add(Entry);
	if (EpisodicMemory.Num() > MaxEpisodicMemorySize)
	{
		int32 LowestScoreIndex = 0;
		float LowestScore = CalculateEvictionScore(EpisodicMemory[0]);
		for (int32 i = 1; i < EpisodicMemory.Num(); ++i)
		{
			float Score = CalculateEvictionScore(EpisodicMemory[i]);
			if (Score < LowestScore)
			{
				LowestScore = Score;
				LowestScoreIndex = i;
			}
		}
		EpisodicMemory.RemoveAt(LowestScoreIndex);
	}
}

void UNpcMemoryComponent::ClearWorkingMemory()
{
	WorkingMemory.Empty();
}

float UNpcMemoryComponent::CalculateEvictionScore(const FMemoryEntry& Entry) const
{
	const float ImportanceWeight = 0.5f;
	const float RecencyWeight = 0.3f;
	const float AccessWeight = 0.2f;

	const FTimespan Age = FDateTime::Now() - Entry.Timestamp;
	const float RecencyScore = FMath::Exp(-Age.GetTotalHours() / 168.0f);

	const float MaxAccessCount = 10.0f;
	const float AccessScore = FMath::Clamp(Entry.AccessCount / MaxAccessCount, 0.0f, 1.0f);

	return Entry.Importance * ImportanceWeight + RecencyScore * RecencyWeight + AccessScore * AccessWeight;
}
