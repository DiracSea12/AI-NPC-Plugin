#pragma once

#include "LLM/LLMProviderTypes.h"

class AINPCCORE_API ILLMProvider
{
public:
	virtual ~ILLMProvider() = default;

	virtual FGuid SendRequest(const FLLMRequest& Request, FLLMResponseCallback CompletionCallback) = 0;
	virtual bool CancelRequest(const FGuid& RequestId) = 0;
	virtual FLLMProviderCapabilities GetCapabilities() const = 0;
};
