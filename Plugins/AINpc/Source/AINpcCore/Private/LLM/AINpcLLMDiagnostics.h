#pragma once

#include "CoreMinimal.h"
#include "LLM/LLMResponseParser.h"

namespace AINpc::LLMDiagnostics
{
inline FString BuildSafeResponseSummary(const FString& ResponseBody)
{
	const FString Trimmed = ResponseBody.TrimStartAndEnd();
	const bool bEmpty = Trimmed.IsEmpty();
	const bool bStartsWithJson = Trimmed.StartsWith(TEXT("{")) || Trimmed.StartsWith(TEXT("["));
	const bool bLooksHtml = Trimmed.StartsWith(TEXT("<!DOCTYPE"), ESearchCase::IgnoreCase) ||
		Trimmed.StartsWith(TEXT("<html"), ESearchCase::IgnoreCase);
	const bool bHasErrorField = Trimmed.Find(TEXT("\"error\""), ESearchCase::IgnoreCase) != INDEX_NONE;

	return FString::Printf(
		TEXT("body=%s length=%d startsWithJson=%s looksHtml=%s errorField=%s"),
		bEmpty ? TEXT("empty") : TEXT("nonempty"),
		ResponseBody.Len(),
		bStartsWithJson ? TEXT("true") : TEXT("false"),
		bLooksHtml ? TEXT("true") : TEXT("false"),
		bHasErrorField ? TEXT("present") : TEXT("absent"));
}


inline const TCHAR* DescribeParseTier(const ELLMResponseParseTier ParseTier)
{
	switch (ParseTier)
	{
	case ELLMResponseParseTier::FunctionCalling:
		return TEXT("FunctionCalling");
	case ELLMResponseParseTier::StrictJsonSchema:
		return TEXT("StrictJsonSchema");
	case ELLMResponseParseTier::LooseJson:
		return TEXT("LooseJson");
	case ELLMResponseParseTier::LooseExtraction:
		return TEXT("LooseExtraction");
	case ELLMResponseParseTier::PlainText:
		return TEXT("PlainText");
	case ELLMResponseParseTier::Unknown:
		return TEXT("Unknown");
	case ELLMResponseParseTier::None:
	default:
		return TEXT("None");
	}
}
}
