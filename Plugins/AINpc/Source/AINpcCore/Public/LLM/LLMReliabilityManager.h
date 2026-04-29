#pragma once

#include "CoreMinimal.h"

// Retry strategy using exponential backoff
enum class ERetryStrategy : uint8
{
	ExponentialBackoff
};

// Fallback strategies for reliability
enum class EFallbackStrategy : uint8
{
	TimeoutFallback,
	TemplateResponse
};

// Type alias for degradation notification delegate
using FDegradationNotification = TMulticastDelegate<void(const FGuid&, int32)>;
