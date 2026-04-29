#pragma once

#include "CoreMinimal.h"

class AINPCMEMORY_API FVectorSimilarity
{
public:
	static float CosineSimilarity(const TArray<float>& A, const TArray<float>& B);
	static float DotProduct(const TArray<float>& A, const TArray<float>& B);
	static float Magnitude(const TArray<float>& Vector);
};
