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
	if (bTerminalReceived)
	{
		return;
	}

	if (Line.IsEmpty())
	{
		FlushCurrentEvent();
		return;
	}

	// Filter : comment lines (heartbeat)
	if (Line.StartsWith(TEXT(":"))) return;

	if (Line.StartsWith(TEXT("event:")))
	{
		CurrentEventName = Line.Mid(6).TrimStartAndEnd();
		return;
	}

	// Parse data: prefix
	if (Line.StartsWith(TEXT("data:")))
	{
		const FString Data = Line.Mid(5).TrimStart();
		if (Data.Equals(TEXT("[DONE]"), ESearchCase::CaseSensitive) && CurrentDataLines.IsEmpty())
		{
			bTerminalReceived = true;
			Buffer.Empty();
			CurrentEventName.Empty();
			if (OnDone.IsBound())
			{
				OnDone.Execute();
			}
			return;
		}

		CurrentDataLines.Add(Data);
		return;
	}

	if (Line.StartsWith(TEXT("error:")))
	{
		bTerminalReceived = true;
		const FString ErrorMsg = Line.Mid(6).TrimStart();
		if (OnError.IsBound() && !ErrorMsg.IsEmpty())
		{
			OnError.Execute(ErrorMsg);
		}
		return;
	}
}

void FSSEParser::FlushCurrentEvent()
{
	if (CurrentEventName.IsEmpty() && CurrentDataLines.IsEmpty())
	{
		return;
	}

	const FString Data = FString::Join(CurrentDataLines, TEXT("\n"));
	const bool bIsErrorEvent = CurrentEventName.Equals(TEXT("error"), ESearchCase::IgnoreCase);
	if (bIsErrorEvent)
	{
		bTerminalReceived = true;
		if (OnError.IsBound())
		{
			OnError.Execute(Data.IsEmpty() ? TEXT("SSE error event received") : Data);
		}
	}
	else if (Data.Equals(TEXT("[DONE]"), ESearchCase::CaseSensitive))
	{
		bTerminalReceived = true;
		if (OnDone.IsBound())
		{
			OnDone.Execute();
		}
	}
	else if (!Data.IsEmpty() && OnData.IsBound())
	{
		OnData.Execute(Data);
	}

	CurrentEventName.Empty();
	CurrentDataLines.Reset();
	if (bTerminalReceived)
	{
		Buffer.Empty();
		CurrentEventName.Empty();
		CurrentDataLines.Reset();
	}
}

void FSSEParser::Reset()
{
	Buffer.Empty();
	CurrentEventName.Empty();
	CurrentDataLines.Reset();
	bTerminalReceived = false;
}
