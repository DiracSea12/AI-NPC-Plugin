// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

DECLARE_DELEGATE_OneParam(FOnSSEData, const FString&);
DECLARE_DELEGATE(FOnSSEDone);
DECLARE_DELEGATE_OneParam(FOnSSEError, const FString&);

/**
 * SSE (Server-Sent Events) Parser
 * Parses streaming responses with data: prefix, [DONE] termination, and error handling
 */
class AINPCCORE_API FSSEParser
{
public:
	FSSEParser();

	/** Delegate called when data event is parsed */
	FOnSSEData OnData;

	/** Delegate called when [DONE] is received */
	FOnSSEDone OnDone;

	/** Delegate called when error occurs */
	FOnSSEError OnError;

	/** Process incoming chunk */
	void ProcessChunk(const FString& Chunk);

	/** Reset parser state */
	void Reset();

private:
	FString Buffer;
	FString CurrentEventName;
	TArray<FString> CurrentDataLines;
	bool bTerminalReceived = false;

	void ProcessLine(const FString& Line);
	void FlushCurrentEvent();
};
