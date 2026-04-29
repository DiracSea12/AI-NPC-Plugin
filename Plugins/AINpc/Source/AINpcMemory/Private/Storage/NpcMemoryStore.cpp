#include "Storage/NpcMemoryStore.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FNpcMemoryStore::FNpcMemoryStore()
{
}

FNpcMemoryStore::~FNpcMemoryStore()
{
}

FString FNpcMemoryStore::GetStoragePath(const FString& NpcId) const
{
	FString SafeNpcId = FPaths::MakeValidFileName(NpcId);
	if (SafeNpcId.IsEmpty())
	{
		SafeNpcId = TEXT("UnknownNpc");
	}

	return FPaths::ProjectSavedDir() / TEXT("AINpc") / TEXT("Memory") / SafeNpcId + TEXT(".json");
}

bool FNpcMemoryStore::SaveMemory(const FString& NpcId, const TArray<FMemoryEntry>& Memories)
{
	TArray<TSharedPtr<FJsonValue>> JsonArray;
	for (const FMemoryEntry& Entry : Memories)
	{
		TSharedRef<FJsonObject> JsonObj = MakeShared<FJsonObject>();
		JsonObj->SetStringField(TEXT("Content"), Entry.Content);
		JsonObj->SetStringField(TEXT("Timestamp"), Entry.Timestamp.ToString());
		JsonObj->SetNumberField(TEXT("Importance"), Entry.Importance);
		JsonObj->SetNumberField(TEXT("AccessCount"), Entry.AccessCount);
		JsonObj->SetNumberField(TEXT("SchemaVersion"), Entry.SchemaVersion);
		JsonObj->SetBoolField(TEXT("Contradicted"), Entry.bContradicted);
		JsonArray.Add(MakeShared<FJsonValueObject>(JsonObj));
	}

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(JsonArray, Writer);

	const FString FilePath = GetStoragePath(NpcId);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), true);
	return FFileHelper::SaveStringToFile(OutputString, *FilePath);
}

bool FNpcMemoryStore::LoadMemory(const FString& NpcId, TArray<FMemoryEntry>& OutMemories)
{
	const FString FilePath = GetStoragePath(NpcId);
	FString FileContent;
	if (!FFileHelper::LoadFileToString(FileContent, *FilePath))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonValue>> JsonArray;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContent);
	if (!FJsonSerializer::Deserialize(Reader, JsonArray))
	{
		return false;
	}

	OutMemories.Empty();
	for (const TSharedPtr<FJsonValue>& JsonValue : JsonArray)
	{
		const TSharedPtr<FJsonObject>* JsonObj;
		if (!JsonValue->TryGetObject(JsonObj))
		{
			continue;
		}

		FMemoryEntry Entry;
		(*JsonObj)->TryGetStringField(TEXT("Content"), Entry.Content);
		
		FString TimestampStr;
		if ((*JsonObj)->TryGetStringField(TEXT("Timestamp"), TimestampStr))
		{
			FDateTime::Parse(TimestampStr, Entry.Timestamp);
		}
		
		(*JsonObj)->TryGetNumberField(TEXT("Importance"), Entry.Importance);
		(*JsonObj)->TryGetNumberField(TEXT("AccessCount"), Entry.AccessCount);
		(*JsonObj)->TryGetNumberField(TEXT("SchemaVersion"), Entry.SchemaVersion);
		(*JsonObj)->TryGetBoolField(TEXT("Contradicted"), Entry.bContradicted);
		
		OutMemories.Add(Entry);
	}

	return true;
}

bool FNpcMemoryStore::SearchMemory(const FString& NpcId, const FString& Query, TArray<FMemoryEntry>& OutResults)
{
	TArray<FMemoryEntry> AllMemories;
	if (!LoadMemory(NpcId, AllMemories))
	{
		return false;
	}

	OutResults.Empty();
	for (const FMemoryEntry& Entry : AllMemories)
	{
		if (Entry.Content.Contains(Query))
		{
			OutResults.Add(Entry);
		}
	}

	return true;
}

bool FNpcMemoryStore::ClearMemory(const FString& NpcId)
{
	const FString FilePath = GetStoragePath(NpcId);
	return IFileManager::Get().Delete(*FilePath);
}
