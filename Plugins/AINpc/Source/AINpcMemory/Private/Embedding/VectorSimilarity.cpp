#include "Embedding/VectorSimilarity.h"

float FVectorSimilarity::CosineSimilarity(const TArray<float>& A, const TArray<float>& B)
{
	if (A.Num() != B.Num() || A.Num() == 0)
	{
		return 0.0f;
	}

	const float Dot = DotProduct(A, B);
	const float MagA = Magnitude(A);
	const float MagB = Magnitude(B);

	if (MagA == 0.0f || MagB == 0.0f)
	{
		return 0.0f;
	}

	return Dot / (MagA * MagB);
}

float FVectorSimilarity::DotProduct(const TArray<float>& A, const TArray<float>& B)
{
	float Sum = 0.0f;
	for (int32 i = 0; i < A.Num(); ++i)
	{
		Sum += A[i] * B[i];
	}
	return Sum;
}

float FVectorSimilarity::Magnitude(const TArray<float>& Vector)
{
	float Sum = 0.0f;
	for (float Val : Vector)
	{
		Sum += Val * Val;
	}
	return FMath::Sqrt(Sum);
}
