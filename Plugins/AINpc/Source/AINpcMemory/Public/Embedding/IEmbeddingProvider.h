#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IEmbeddingProvider.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UEmbeddingProvider : public UInterface
{
	GENERATED_BODY()
};

class AINPCMEMORY_API IEmbeddingProvider
{
	GENERATED_BODY()

public:
	virtual TArray<float> GenerateEmbedding(const FString& Text) = 0;
	virtual bool IsAvailable() const = 0;
};
