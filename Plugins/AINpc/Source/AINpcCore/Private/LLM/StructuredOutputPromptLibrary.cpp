#include "LLM/StructuredOutputPromptLibrary.h"

#include "AINpcCoreLog.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
FString LoadPromptTextFile(const TCHAR* FileName)
{
	const FString PromptPath = FPaths::Combine(FPaths::ProjectConfigDir(), FileName);
	FString PromptText;
	if (!FFileHelper::LoadFileToString(PromptText, *PromptPath))
	{
		UE_LOG(LogAINpc, Error, TEXT("Failed to load structured-output prompt file: %s"), *PromptPath);
	}

	return PromptText;
}
}

namespace AINpc::StructuredOutputPrompts
{
const FString& GetToolDescription()
{
	static const FString PromptText = LoadPromptTextFile(TEXT("AINpcStructuredOutputToolDescription.txt"));
	return PromptText;
}

const FString& GetJsonInstruction()
{
	static const FString PromptText = LoadPromptTextFile(TEXT("AINpcStructuredOutputJsonInstruction.txt"));
	return PromptText;
}

const FString& GetStrictJsonInstruction()
{
	static const FString PromptText = LoadPromptTextFile(TEXT("AINpcStructuredOutputStrictJsonInstruction.txt"));
	return PromptText;
}
}
