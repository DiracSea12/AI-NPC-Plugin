#include "LLM/LLMResponseParser.h"
#include "Dom/JsonObject.h"
#include "LLM/SSEParser.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogLLMParser, Log, All);

static bool ParseFunctionArguments(const FString& ArgumentsStr, FParsedLLMResponse& OutParsedResponse)
{
	TSharedPtr<FJsonObject> ArgsObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgumentsStr);
	if (!FJsonSerializer::Deserialize(Reader, ArgsObject) || !ArgsObject.IsValid())
	{
		return false;
	}

	if (!ArgsObject->HasField(TEXT("dialogue")) || !ArgsObject->HasField(TEXT("actions")) ||
		!ArgsObject->HasField(TEXT("emotion_delta")) || !ArgsObject->HasField(TEXT("relationship_delta")))
	{
		return false;
	}

	ArgsObject->TryGetStringField(TEXT("dialogue"), OutParsedResponse.Dialogue);

	const TArray<TSharedPtr<FJsonValue>>* ActionsArray = nullptr;
	if (ArgsObject->TryGetArrayField(TEXT("actions"), ActionsArray))
	{
		for (const TSharedPtr<FJsonValue>& ActionValue : *ActionsArray)
		{
			const TSharedPtr<FJsonObject>* ActionObj = nullptr;
			if (ActionValue->TryGetObject(ActionObj))
			{
				FNpcAction Action;
				if (!(*ActionObj)->TryGetStringField(TEXT("action_type"), Action.ActionType))
				{
					(*ActionObj)->TryGetStringField(TEXT("type"), Action.ActionType);
				}
				(*ActionObj)->TryGetStringField(TEXT("target"), Action.Target);
				OutParsedResponse.Actions.Add(Action);
			}
		}
	}

	const TSharedPtr<FJsonObject>* EmotionDeltaObj = nullptr;
	if (ArgsObject->TryGetObjectField(TEXT("emotion_delta"), EmotionDeltaObj))
	{
		(*EmotionDeltaObj)->TryGetNumberField(TEXT("valence"), OutParsedResponse.EmotionDelta.Valence);
		(*EmotionDeltaObj)->TryGetNumberField(TEXT("arousal"), OutParsedResponse.EmotionDelta.Arousal);
		(*EmotionDeltaObj)->TryGetNumberField(TEXT("dominance"), OutParsedResponse.EmotionDelta.Dominance);
		OutParsedResponse.EmotionDelta.Valence = FMath::Clamp(OutParsedResponse.EmotionDelta.Valence, -1.0, 1.0);
		OutParsedResponse.EmotionDelta.Arousal = FMath::Clamp(OutParsedResponse.EmotionDelta.Arousal, -1.0, 1.0);
		OutParsedResponse.EmotionDelta.Dominance = FMath::Clamp(OutParsedResponse.EmotionDelta.Dominance, -1.0, 1.0);
	}

	const TSharedPtr<FJsonObject>* RelationshipDeltaObj = nullptr;
	if (ArgsObject->TryGetObjectField(TEXT("relationship_delta"), RelationshipDeltaObj))
	{
		(*RelationshipDeltaObj)->TryGetNumberField(TEXT("affinity"), OutParsedResponse.RelationshipDelta.Affinity);
		(*RelationshipDeltaObj)->TryGetNumberField(TEXT("trust"), OutParsedResponse.RelationshipDelta.Trust);
		(*RelationshipDeltaObj)->TryGetNumberField(TEXT("familiarity"), OutParsedResponse.RelationshipDelta.Familiarity);
		OutParsedResponse.RelationshipDelta.Affinity = FMath::Clamp(OutParsedResponse.RelationshipDelta.Affinity, -1.0, 1.0);
		OutParsedResponse.RelationshipDelta.Trust = FMath::Clamp(OutParsedResponse.RelationshipDelta.Trust, -1.0, 1.0);
		OutParsedResponse.RelationshipDelta.Familiarity = FMath::Clamp(OutParsedResponse.RelationshipDelta.Familiarity, -1.0, 1.0);
	}

	return true;
}

static bool TryParseToolCalls(const TSharedPtr<FJsonObject>& MessageObj, FParsedLLMResponse& OutParsedResponse, FString& OutFallbackContent)
{
	const TArray<TSharedPtr<FJsonValue>>* ToolCallsArray = nullptr;
	if (!MessageObj->TryGetArrayField(TEXT("tool_calls"), ToolCallsArray) || ToolCallsArray->Num() == 0)
	{
		return false;
	}

	MessageObj->TryGetStringField(TEXT("content"), OutFallbackContent);

	for (const TSharedPtr<FJsonValue>& ToolCallValue : *ToolCallsArray)
	{
		const TSharedPtr<FJsonObject>* ToolCallObj = nullptr;
		if (!ToolCallValue->TryGetObject(ToolCallObj))
		{
			continue;
		}

		const TSharedPtr<FJsonObject>* FunctionObj = nullptr;
		if (!(*ToolCallObj)->TryGetObjectField(TEXT("function"), FunctionObj))
		{
			continue;
		}

		FString FunctionName;
		(*FunctionObj)->TryGetStringField(TEXT("name"), FunctionName);

		FString ArgumentsStr;
		if (!(*FunctionObj)->TryGetStringField(TEXT("arguments"), ArgumentsStr))
		{
			continue;
		}

		TSharedPtr<FJsonObject> ArgsObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgumentsStr);

		if (FunctionName == TEXT("emit_npc_response"))
		{
			if (!FJsonSerializer::Deserialize(Reader, ArgsObject) || !ArgsObject.IsValid())
			{
				return false;
			}

			if (!ArgsObject->HasField(TEXT("dialogue")) || !ArgsObject->HasField(TEXT("actions")) ||
				!ArgsObject->HasField(TEXT("emotion_delta")) || !ArgsObject->HasField(TEXT("relationship_delta")))
			{
				return false;
			}

			const TSharedPtr<FJsonObject>* RelationshipDeltaObj = nullptr;
			if (!ArgsObject->TryGetObjectField(TEXT("relationship_delta"), RelationshipDeltaObj))
			{
				return false;
			}

			ArgsObject->TryGetStringField(TEXT("dialogue"), OutParsedResponse.Dialogue);

			const TArray<TSharedPtr<FJsonValue>>* ActionsArray = nullptr;
			if (ArgsObject->TryGetArrayField(TEXT("actions"), ActionsArray))
			{
				for (const TSharedPtr<FJsonValue>& ActionValue : *ActionsArray)
				{
					const TSharedPtr<FJsonObject>* ActionObj = nullptr;
					if (ActionValue->TryGetObject(ActionObj))
					{
						FNpcAction Action;
						if (!(*ActionObj)->TryGetStringField(TEXT("action_type"), Action.ActionType))
						{
							(*ActionObj)->TryGetStringField(TEXT("type"), Action.ActionType);
						}
						(*ActionObj)->TryGetStringField(TEXT("target"), Action.Target);
						OutParsedResponse.Actions.Add(Action);
					}
				}
			}

			const TSharedPtr<FJsonObject>* EmotionDeltaObj = nullptr;
			if (ArgsObject->TryGetObjectField(TEXT("emotion_delta"), EmotionDeltaObj))
			{
				(*EmotionDeltaObj)->TryGetNumberField(TEXT("valence"), OutParsedResponse.EmotionDelta.Valence);
				(*EmotionDeltaObj)->TryGetNumberField(TEXT("arousal"), OutParsedResponse.EmotionDelta.Arousal);
				(*EmotionDeltaObj)->TryGetNumberField(TEXT("dominance"), OutParsedResponse.EmotionDelta.Dominance);
				OutParsedResponse.EmotionDelta.Valence = FMath::Clamp(OutParsedResponse.EmotionDelta.Valence, -1.0, 1.0);
				OutParsedResponse.EmotionDelta.Arousal = FMath::Clamp(OutParsedResponse.EmotionDelta.Arousal, -1.0, 1.0);
				OutParsedResponse.EmotionDelta.Dominance = FMath::Clamp(OutParsedResponse.EmotionDelta.Dominance, -1.0, 1.0);
			}

			(*RelationshipDeltaObj)->TryGetNumberField(TEXT("affinity"), OutParsedResponse.RelationshipDelta.Affinity);
			(*RelationshipDeltaObj)->TryGetNumberField(TEXT("trust"), OutParsedResponse.RelationshipDelta.Trust);
			(*RelationshipDeltaObj)->TryGetNumberField(TEXT("familiarity"), OutParsedResponse.RelationshipDelta.Familiarity);
			OutParsedResponse.RelationshipDelta.Affinity = FMath::Clamp(OutParsedResponse.RelationshipDelta.Affinity, -1.0, 1.0);
			OutParsedResponse.RelationshipDelta.Trust = FMath::Clamp(OutParsedResponse.RelationshipDelta.Trust, -1.0, 1.0);
			OutParsedResponse.RelationshipDelta.Familiarity = FMath::Clamp(OutParsedResponse.RelationshipDelta.Familiarity, -1.0, 1.0);

			OutParsedResponse.bParsedAsJson = true;
			OutParsedResponse.ParseTier = ELLMResponseParseTier::FunctionCalling;
			return true;
		}
		else if (FunctionName.StartsWith(TEXT("Action.")))
		{
			if (!FJsonSerializer::Deserialize(Reader, ArgsObject) || !ArgsObject.IsValid())
			{
				OutParsedResponse.Dialogue = OutFallbackContent;
			}
			else
			{
				ArgsObject->TryGetStringField(TEXT("dialogue"), OutParsedResponse.Dialogue);
			}

			FNpcAction Action;
			Action.ActionType = FunctionName;
			if (ArgsObject.IsValid())
			{
				ArgsObject->TryGetStringField(TEXT("target"), Action.Target);
			}
			OutParsedResponse.Actions.Add(Action);

			OutParsedResponse.bParsedAsJson = true;
			OutParsedResponse.ParseTier = ELLMResponseParseTier::FunctionCalling;
			return true;
		}
	}

	return false;
}

static bool TryParseFunctionCall(const TSharedPtr<FJsonObject>& JsonObject, FParsedLLMResponse& OutParsedResponse)
{
	const TSharedPtr<FJsonObject>* FunctionCallObj = nullptr;
	if (!JsonObject->TryGetObjectField(TEXT("function_call"), FunctionCallObj) || !FunctionCallObj->IsValid())
	{
		return false;
	}

	FString ArgumentsStr;
	if (!(*FunctionCallObj)->TryGetStringField(TEXT("arguments"), ArgumentsStr))
	{
		return false;
	}

	if (!ParseFunctionArguments(ArgumentsStr, OutParsedResponse))
	{
		return false;
	}

	OutParsedResponse.bParsedAsJson = true;
	OutParsedResponse.ParseTier = ELLMResponseParseTier::FunctionCalling;
	return true;
}

static bool TryParseStructuredOutputToolInput(const TSharedPtr<FJsonObject>& ArgsObject, FParsedLLMResponse& OutParsedResponse)
{
	if (!ArgsObject.IsValid())
	{
		return false;
	}

	FString ArgumentsStr;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ArgumentsStr);
	FJsonSerializer::Serialize(ArgsObject.ToSharedRef(), Writer);
	if (!ParseFunctionArguments(ArgumentsStr, OutParsedResponse))
	{
		return false;
	}

	OutParsedResponse.bParsedAsJson = true;
	OutParsedResponse.ParseTier = ELLMResponseParseTier::FunctionCalling;
	return true;
}

// FR-27 Tier 2: ParseStrictJSON - strict schema validation
static bool TryParseStrictJson(const FString& ContentStr, FParsedLLMResponse& OutParsedResponse)
{
	TSharedPtr<FJsonObject> JsonObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ContentStr);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return false;
	}

	if (!JsonObject->HasField(TEXT("dialogue")) || !JsonObject->HasField(TEXT("actions")) ||
		!JsonObject->HasField(TEXT("emotion_delta")) || !JsonObject->HasField(TEXT("relationship_delta")))
	{
		return false;
	}

	TArray<FString> Keys;
	JsonObject->Values.GetKeys(Keys);
	if (Keys.Num() > 4)
	{
		return false;
	}

	JsonObject->TryGetStringField(TEXT("dialogue"), OutParsedResponse.Dialogue);

	const TArray<TSharedPtr<FJsonValue>>* ActionsArray = nullptr;
	if (JsonObject->TryGetArrayField(TEXT("actions"), ActionsArray))
	{
		for (const TSharedPtr<FJsonValue>& ActionValue : *ActionsArray)
		{
			const TSharedPtr<FJsonObject>* ActionObj = nullptr;
			if (ActionValue->TryGetObject(ActionObj))
			{
				FNpcAction Action;
				if (!(*ActionObj)->TryGetStringField(TEXT("action_type"), Action.ActionType))
				{
					(*ActionObj)->TryGetStringField(TEXT("type"), Action.ActionType);
				}
				(*ActionObj)->TryGetStringField(TEXT("target"), Action.Target);
				OutParsedResponse.Actions.Add(Action);
			}
		}
	}

	const TSharedPtr<FJsonObject>* EmotionDeltaObj = nullptr;
	if (JsonObject->TryGetObjectField(TEXT("emotion_delta"), EmotionDeltaObj))
	{
		(*EmotionDeltaObj)->TryGetNumberField(TEXT("valence"), OutParsedResponse.EmotionDelta.Valence);
		(*EmotionDeltaObj)->TryGetNumberField(TEXT("arousal"), OutParsedResponse.EmotionDelta.Arousal);
		(*EmotionDeltaObj)->TryGetNumberField(TEXT("dominance"), OutParsedResponse.EmotionDelta.Dominance);
		OutParsedResponse.EmotionDelta.Valence = FMath::Clamp(OutParsedResponse.EmotionDelta.Valence, -1.0, 1.0);
		OutParsedResponse.EmotionDelta.Arousal = FMath::Clamp(OutParsedResponse.EmotionDelta.Arousal, -1.0, 1.0);
		OutParsedResponse.EmotionDelta.Dominance = FMath::Clamp(OutParsedResponse.EmotionDelta.Dominance, -1.0, 1.0);
	}

	const TSharedPtr<FJsonObject>* RelationshipDeltaObj = nullptr;
	if (JsonObject->TryGetObjectField(TEXT("relationship_delta"), RelationshipDeltaObj))
	{
		(*RelationshipDeltaObj)->TryGetNumberField(TEXT("affinity"), OutParsedResponse.RelationshipDelta.Affinity);
		(*RelationshipDeltaObj)->TryGetNumberField(TEXT("trust"), OutParsedResponse.RelationshipDelta.Trust);
		(*RelationshipDeltaObj)->TryGetNumberField(TEXT("familiarity"), OutParsedResponse.RelationshipDelta.Familiarity);
		OutParsedResponse.RelationshipDelta.Affinity = FMath::Clamp(OutParsedResponse.RelationshipDelta.Affinity, -1.0, 1.0);
		OutParsedResponse.RelationshipDelta.Trust = FMath::Clamp(OutParsedResponse.RelationshipDelta.Trust, -1.0, 1.0);
		OutParsedResponse.RelationshipDelta.Familiarity = FMath::Clamp(OutParsedResponse.RelationshipDelta.Familiarity, -1.0, 1.0);
	}

	OutParsedResponse.bParsedAsJson = true;
	OutParsedResponse.ParseTier = ELLMResponseParseTier::StrictJsonSchema;
	return true;
}

// FR-27 Tier 3: ExtractLooseJSON - extract JSON from mixed text (markdown code blocks, etc.)
static bool TryLooseExtraction(const FString& ContentStr, FParsedLLMResponse& OutParsedResponse)
{
	FString JsonStr = ContentStr;

	int32 CodeBlockStart = ContentStr.Find(TEXT("```json"));
	if (CodeBlockStart != INDEX_NONE)
	{
		int32 JsonContentStart = ContentStr.Find(TEXT("\n"), ESearchCase::IgnoreCase, ESearchDir::FromStart, CodeBlockStart);
		int32 CodeBlockEnd = ContentStr.Find(TEXT("```"), ESearchCase::IgnoreCase, ESearchDir::FromStart, JsonContentStart);
		if (JsonContentStart != INDEX_NONE && CodeBlockEnd != INDEX_NONE)
		{
			JsonStr = ContentStr.Mid(JsonContentStart + 1, CodeBlockEnd - JsonContentStart - 1).TrimStartAndEnd();
		}
	}

	int32 JsonStart = JsonStr.Find(TEXT("{"));
	int32 JsonEnd = JsonStr.Find(TEXT("}"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);

	if (JsonStart != INDEX_NONE && JsonEnd != INDEX_NONE && JsonEnd > JsonStart)
	{
		FString ExtractedJson = JsonStr.Mid(JsonStart, JsonEnd - JsonStart + 1);
		TSharedPtr<FJsonObject> JsonObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ExtractedJson);
		if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
		{
			bool bExtractedAny = false;

			if (JsonObject->TryGetStringField(TEXT("dialogue"), OutParsedResponse.Dialogue))
			{
				bExtractedAny = true;
			}

			const TArray<TSharedPtr<FJsonValue>>* ActionsArray = nullptr;
			if (JsonObject->TryGetArrayField(TEXT("actions"), ActionsArray))
			{
				for (const TSharedPtr<FJsonValue>& ActionValue : *ActionsArray)
				{
					const TSharedPtr<FJsonObject>* ActionObj = nullptr;
					if (ActionValue->TryGetObject(ActionObj))
					{
						FNpcAction Action;
						if (!(*ActionObj)->TryGetStringField(TEXT("action_type"), Action.ActionType))
						{
							(*ActionObj)->TryGetStringField(TEXT("type"), Action.ActionType);
						}
						(*ActionObj)->TryGetStringField(TEXT("target"), Action.Target);
						OutParsedResponse.Actions.Add(Action);
						bExtractedAny = true;
					}
				}
			}

			const TArray<TSharedPtr<FJsonValue>>* ActionIntentsArray = nullptr;
			if (JsonObject->TryGetArrayField(TEXT("action_intents"), ActionIntentsArray))
			{
				for (const TSharedPtr<FJsonValue>& IntentValue : *ActionIntentsArray)
				{
					const TSharedPtr<FJsonObject>* IntentObj = nullptr;
					if (IntentValue->TryGetObject(IntentObj))
					{
						FNpcAction Action;
						if (!(*IntentObj)->TryGetStringField(TEXT("action_type"), Action.ActionType))
						{
							(*IntentObj)->TryGetStringField(TEXT("type"), Action.ActionType);
						}
						(*IntentObj)->TryGetStringField(TEXT("target"), Action.Target);
						OutParsedResponse.Actions.Add(Action);
						bExtractedAny = true;
					}
					else
					{
						FString IntentStr;
						if (IntentValue->TryGetString(IntentStr))
						{
							FNpcAction Action;
							Action.ActionType = IntentStr;
							OutParsedResponse.Actions.Add(Action);
							bExtractedAny = true;
						}
					}
				}
			}

			if (bExtractedAny)
			{
				OutParsedResponse.bParsedAsJson = true;
				OutParsedResponse.ParseTier = ELLMResponseParseTier::LooseExtraction;
				return true;
			}
		}
	}

	return false;
}

// FR-27 Tier 4: FallbackPlainText - plain text downgrade when all parsing fails
static void FallbackToPlainText(const FString& ContentStr, FParsedLLMResponse& OutParsedResponse)
{
	OutParsedResponse.Dialogue = ContentStr.TrimStartAndEnd();
	OutParsedResponse.bParsedAsJson = false;
	OutParsedResponse.ParseTier = ELLMResponseParseTier::PlainText;

	FNpcAction DefaultAction;
	DefaultAction.ActionType = AINpc::Actions::DefaultTalkActionType;
	OutParsedResponse.Actions.Add(DefaultAction);
}

static bool TryBuildAnthropicMessageBodyFromEventStream(const FString& ResponseBody, FString& OutMessageBody)
{
	TSharedRef<FJsonObject> MessageObject = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> ContentArray;
	TSharedPtr<FJsonObject> ActiveContentBlock;
	FString ActiveContentBlockType;
	FString ActiveText;
	FString ActiveInputJson;
	bool bSawEvent = false;

	auto FlushActiveContentBlock = [&]()
	{
		if (!ActiveContentBlock.IsValid())
		{
			return;
		}

		if (ActiveContentBlockType == TEXT("text"))
		{
			ActiveContentBlock->SetStringField(TEXT("text"), ActiveText);
		}
		else if (ActiveContentBlockType == TEXT("tool_use"))
		{
			TSharedPtr<FJsonObject> InputObject;
			const TSharedRef<TJsonReader<>> InputReader = TJsonReaderFactory<>::Create(ActiveInputJson);
			if (FJsonSerializer::Deserialize(InputReader, InputObject) && InputObject.IsValid())
			{
				ActiveContentBlock->SetObjectField(TEXT("input"), InputObject);
			}
		}

		ContentArray.Add(MakeShared<FJsonValueObject>(ActiveContentBlock));
		ActiveContentBlock.Reset();
		ActiveContentBlockType.Reset();
		ActiveText.Reset();
		ActiveInputJson.Reset();
	};

	FSSEParser Parser;
	Parser.OnData.BindLambda([&](const FString& Data)
	{
		TSharedPtr<FJsonObject> EventObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Data);
		if (!FJsonSerializer::Deserialize(Reader, EventObject) || !EventObject.IsValid())
		{
			return;
		}

		FString EventType;
		if (!EventObject->TryGetStringField(TEXT("type"), EventType))
		{
			return;
		}

		bSawEvent = true;
		if (EventType == TEXT("content_block_start"))
		{
			FlushActiveContentBlock();

			const TSharedPtr<FJsonObject>* ContentBlockObject = nullptr;
			if (!EventObject->TryGetObjectField(TEXT("content_block"), ContentBlockObject) ||
				!ContentBlockObject ||
				!ContentBlockObject->IsValid())
			{
				return;
			}

			ActiveContentBlock = MakeShared<FJsonObject>();
			(*ContentBlockObject)->TryGetStringField(TEXT("type"), ActiveContentBlockType);
			ActiveContentBlock->SetStringField(TEXT("type"), ActiveContentBlockType);

			if (ActiveContentBlockType == TEXT("tool_use"))
			{
				FString ToolId;
				FString ToolName;
				if ((*ContentBlockObject)->TryGetStringField(TEXT("id"), ToolId))
				{
					ActiveContentBlock->SetStringField(TEXT("id"), ToolId);
				}
				if ((*ContentBlockObject)->TryGetStringField(TEXT("name"), ToolName))
				{
					ActiveContentBlock->SetStringField(TEXT("name"), ToolName);
				}
			}
		}
		else if (EventType == TEXT("content_block_delta"))
		{
			const TSharedPtr<FJsonObject>* DeltaObject = nullptr;
			if (!EventObject->TryGetObjectField(TEXT("delta"), DeltaObject) ||
				!DeltaObject ||
				!DeltaObject->IsValid())
			{
				return;
			}

			FString DeltaType;
			(*DeltaObject)->TryGetStringField(TEXT("type"), DeltaType);
			if (DeltaType == TEXT("text_delta"))
			{
				FString Text;
				if ((*DeltaObject)->TryGetStringField(TEXT("text"), Text))
				{
					ActiveText += Text;
				}
			}
			else if (DeltaType == TEXT("input_json_delta"))
			{
				FString PartialJson;
				if ((*DeltaObject)->TryGetStringField(TEXT("partial_json"), PartialJson))
				{
					ActiveInputJson += PartialJson;
				}
			}
		}
		else if (EventType == TEXT("content_block_stop"))
		{
			FlushActiveContentBlock();
		}
	});

	Parser.ProcessChunk(ResponseBody);
	FlushActiveContentBlock();

	if (!bSawEvent)
	{
		return false;
	}

	MessageObject->SetArrayField(TEXT("content"), ContentArray);
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutMessageBody);
	FJsonSerializer::Serialize(MessageObject, Writer);
	return true;
}

bool FLLMResponseParser::ParseOpenAIChatCompletion(
	const FString& ResponseBody,
	FParsedLLMResponse& OutParsedResponse,
	FString& OutErrorMessage)
{
	TSharedPtr<FJsonObject> JsonObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		OutErrorMessage = TEXT("Failed to parse response as JSON");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* ChoicesArray = nullptr;
	if (!JsonObject->TryGetArrayField(TEXT("choices"), ChoicesArray) || ChoicesArray->Num() == 0)
	{
		OutErrorMessage = TEXT("No choices in response");
		return false;
	}

	const TSharedPtr<FJsonObject>* ChoiceObj = nullptr;
	if (!(*ChoicesArray)[0]->TryGetObject(ChoiceObj))
	{
		OutErrorMessage = TEXT("Invalid choice object");
		return false;
	}

	const TSharedPtr<FJsonObject>* MessageObj = nullptr;
	if (!(*ChoiceObj)->TryGetObjectField(TEXT("message"), MessageObj))
	{
		OutErrorMessage = TEXT("No message in choice");
		return false;
	}

	FString FallbackContent;
	if (TryParseToolCalls(*MessageObj, OutParsedResponse, FallbackContent))
	{
		return true;
	}

	if (TryParseFunctionCall(*MessageObj, OutParsedResponse))
	{
		return true;
	}

	FString ContentStr;
	if (!(*MessageObj)->TryGetStringField(TEXT("content"), ContentStr))
	{
		if (!FallbackContent.IsEmpty())
		{
			ContentStr = FallbackContent;
		}
		else
		{
			OutErrorMessage = TEXT("No content in message");
			return false;
		}
	}

	if (TryParseStrictJson(ContentStr, OutParsedResponse))
	{
		return true;
	}

	UE_LOG(LogLLMParser, Warning, TEXT("Strict JSON parse failed, falling back to loose extraction"));

	if (TryLooseExtraction(ContentStr, OutParsedResponse))
	{
		return true;
	}

	UE_LOG(LogLLMParser, Warning, TEXT("Loose extraction failed, falling back to plain text"));

	FallbackToPlainText(ContentStr, OutParsedResponse);
	return true;
}

bool FLLMResponseParser::ParseAnthropicMessages(
	const FString& ResponseBody,
	FParsedLLMResponse& OutParsedResponse,
	FString& OutErrorMessage)
{
	TSharedPtr<FJsonObject> JsonObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		FString MessageBody;
		if (TryBuildAnthropicMessageBodyFromEventStream(ResponseBody, MessageBody))
		{
			return ParseAnthropicMessages(MessageBody, OutParsedResponse, OutErrorMessage);
		}

		OutErrorMessage = TEXT("Failed to parse response as JSON");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* ContentArray = nullptr;
	if (!JsonObject->TryGetArrayField(TEXT("content"), ContentArray) || ContentArray->Num() == 0)
	{
		OutErrorMessage = TEXT("No content in response");
		return false;
	}

	FString ContentStr;
	for (const TSharedPtr<FJsonValue>& ContentValue : *ContentArray)
	{
		const TSharedPtr<FJsonObject>* ContentObj = nullptr;
		if (ContentValue->TryGetObject(ContentObj))
		{
			FString Type;
			(*ContentObj)->TryGetStringField(TEXT("type"), Type);
			
			if (Type == TEXT("text"))
			{
				FString Text;
				(*ContentObj)->TryGetStringField(TEXT("text"), Text);
				ContentStr += Text;
			}
			else if (Type == TEXT("tool_use"))
			{
				FString ToolName;
				(*ContentObj)->TryGetStringField(TEXT("name"), ToolName);
				const TSharedPtr<FJsonObject>* InputObject = nullptr;
				if (ToolName == TEXT("emit_npc_response") &&
					(*ContentObj)->TryGetObjectField(TEXT("input"), InputObject) &&
					InputObject &&
					TryParseStructuredOutputToolInput(*InputObject, OutParsedResponse))
				{
					return true;
				}
			}
		}
	}

	if (ContentStr.IsEmpty())
	{
		OutErrorMessage = TEXT("Empty content");
		return false;
	}

	if (TryParseStrictJson(ContentStr, OutParsedResponse))
	{
		return true;
	}

	if (TryLooseExtraction(ContentStr, OutParsedResponse))
	{
		return true;
	}

	FallbackToPlainText(ContentStr, OutParsedResponse);
	return true;
}
