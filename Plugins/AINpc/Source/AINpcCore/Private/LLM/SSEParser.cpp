// Copyright Epic Games, Inc. All Rights Reserved.

#include "LLM/SSEParser.h"

FSSEParser::FSSEParser()
{
}

void FSSEParser::ProcessChunk(const FString& Chunk)
{
	Buffer += Chunk;

	int32 LineEnd;
	while (Buffer.FindChar(TEXT('\n'), LineEnd))
	{
		FString Line = Buffer.Left(LineEnd).TrimEnd();
		Buffer = Buffer.Mid(LineEnd + 1);
		ProcessLine(Line);
	}
}

void FSSEParser::ProcessLine(const FString& Line)
{
	if (Line.IsEmpty()) return;

	// Filter : comment lines (heartbeat)
	if (Line.StartsWith(TEXT(":"))) return;

	// Check for [DONE]
	if (Line.Contains(TEXT("[DONE]")))
	{
		if (OnDone.IsBound())
		{
			OnDone.Execute();
		}
		return;
	}

	// Parse data: prefix
	if (Line.StartsWith(TEXT("data:")))
	{
		FString Data = Line.Mid(5).TrimStart();
		if (OnData.IsBound())
		{
			OnData.Execute(Data);
		}
		return;
	}

	// Parse error event
	if (Line.StartsWith(TEXT("event:")) && Line.Contains(TEXT("error")))
	{
		if (OnError.IsBound())
		{
			OnError.Execute(TEXT("SSE error event received"));
		}
		return;
	}

	if (Line.StartsWith(TEXT("error:")))
	{
		FString ErrorMsg = Line.Mid(6).TrimStart();
		if (OnError.IsBound())
		{
			OnError.Execute(ErrorMsg);
		}
	}
}

void FSSEParser::Reset()
{
	Buffer.Empty();
}
