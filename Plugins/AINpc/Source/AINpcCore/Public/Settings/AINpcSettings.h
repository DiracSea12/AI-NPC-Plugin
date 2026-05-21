#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "AINpcSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "AI NPC"))
class AINPCCORE_API UAINpcSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAINpcSettings();

	virtual FName GetCategoryName() const override;
	virtual FName GetSectionName() const override;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Concurrency", meta = (ClampMin = "1", UIMin = "1"))
	int32 DialogueRequestConcurrencyLimit = 2;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Concurrency", meta = (ClampMin = "1", UIMin = "1"))
	int32 MemoryMaintenanceConcurrencyLimit = 1;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Reliability", meta = (ClampMin = "0", UIMin = "0"))
	int32 MaxRequestRetries = 2;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Reliability", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RetryBackoffBaseSeconds = 0.5f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Reliability", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MaxRetryDelaySeconds = 16.0f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Reliability", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RequestTimeoutSeconds = 8.0f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Reliability", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MaxTotalRetryTimeSeconds = 30.0f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Reliability", meta = (MultiLine = "true"))
	FString FallbackResponseTemplate;
};
