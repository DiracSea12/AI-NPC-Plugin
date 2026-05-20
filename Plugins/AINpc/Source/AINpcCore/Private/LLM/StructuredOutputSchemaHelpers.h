#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include <initializer_list>

namespace StructuredOutputSchemaHelpers
{
	static const TCHAR* StructuredOutputToolName = TEXT("emit_npc_response");

	inline TSharedRef<FJsonValueObject> MakeTypeOnlySchemaField(const TCHAR* TypeName)
	{
		const TSharedRef<FJsonObject> FieldSchema = MakeShared<FJsonObject>();
		FieldSchema->SetStringField(TEXT("type"), TypeName);
		return MakeShared<FJsonValueObject>(FieldSchema);
	}

	inline void AddRequiredFieldList(FJsonObject& SchemaObject, std::initializer_list<const TCHAR*> RequiredFields)
	{
		TArray<TSharedPtr<FJsonValue>> RequiredValues;
		for (const TCHAR* RequiredField : RequiredFields)
		{
			RequiredValues.Add(MakeShared<FJsonValueString>(RequiredField));
		}
		SchemaObject.SetArrayField(TEXT("required"), RequiredValues);
	}

	inline void SetAdditionalPropertiesAllowed(FJsonObject& SchemaObject, const bool bAllowed)
	{
		SchemaObject.SetBoolField(TEXT("additionalProperties"), bAllowed);
	}

	inline TSharedRef<FJsonObject> BuildStructuredOutputParametersSchema()
	{
		const TSharedRef<FJsonObject> RootSchema = MakeShared<FJsonObject>();
		RootSchema->SetStringField(TEXT("type"), TEXT("object"));
		SetAdditionalPropertiesAllowed(*RootSchema, false);

		const TSharedRef<FJsonObject> RootProperties = MakeShared<FJsonObject>();
		RootProperties->SetField(TEXT("dialogue"), MakeTypeOnlySchemaField(TEXT("string")));

		const TSharedRef<FJsonObject> ActionItemSchema = MakeShared<FJsonObject>();
		ActionItemSchema->SetStringField(TEXT("type"), TEXT("object"));
		const TSharedRef<FJsonObject> ActionItemProperties = MakeShared<FJsonObject>();
		ActionItemProperties->SetField(TEXT("type"), MakeTypeOnlySchemaField(TEXT("string")));
		ActionItemProperties->SetField(TEXT("target"), MakeTypeOnlySchemaField(TEXT("string")));
		ActionItemSchema->SetObjectField(TEXT("properties"), ActionItemProperties);
		AddRequiredFieldList(*ActionItemSchema, { TEXT("type") });
		SetAdditionalPropertiesAllowed(*ActionItemSchema, false);

		const TSharedRef<FJsonObject> ActionsSchema = MakeShared<FJsonObject>();
		ActionsSchema->SetStringField(TEXT("type"), TEXT("array"));
		ActionsSchema->SetObjectField(TEXT("items"), ActionItemSchema);
		RootProperties->SetObjectField(TEXT("actions"), ActionsSchema);

		const TSharedRef<FJsonObject> EmotionDeltaSchema = MakeShared<FJsonObject>();
		EmotionDeltaSchema->SetStringField(TEXT("type"), TEXT("object"));
		const TSharedRef<FJsonObject> EmotionDeltaProperties = MakeShared<FJsonObject>();
		EmotionDeltaProperties->SetField(TEXT("valence"), MakeTypeOnlySchemaField(TEXT("number")));
		EmotionDeltaProperties->SetField(TEXT("arousal"), MakeTypeOnlySchemaField(TEXT("number")));
		EmotionDeltaProperties->SetField(TEXT("dominance"), MakeTypeOnlySchemaField(TEXT("number")));
		EmotionDeltaSchema->SetObjectField(TEXT("properties"), EmotionDeltaProperties);
		AddRequiredFieldList(*EmotionDeltaSchema, { TEXT("valence"), TEXT("arousal"), TEXT("dominance") });
		SetAdditionalPropertiesAllowed(*EmotionDeltaSchema, false);
		RootProperties->SetObjectField(TEXT("emotion_delta"), EmotionDeltaSchema);

		const TSharedRef<FJsonObject> RelationshipDeltaSchema = MakeShared<FJsonObject>();
		RelationshipDeltaSchema->SetStringField(TEXT("type"), TEXT("object"));
		const TSharedRef<FJsonObject> RelationshipDeltaProperties = MakeShared<FJsonObject>();
		RelationshipDeltaProperties->SetField(TEXT("affinity"), MakeTypeOnlySchemaField(TEXT("number")));
		RelationshipDeltaProperties->SetField(TEXT("trust"), MakeTypeOnlySchemaField(TEXT("number")));
		RelationshipDeltaProperties->SetField(TEXT("familiarity"), MakeTypeOnlySchemaField(TEXT("number")));
		RelationshipDeltaSchema->SetObjectField(TEXT("properties"), RelationshipDeltaProperties);
		AddRequiredFieldList(*RelationshipDeltaSchema, { TEXT("affinity"), TEXT("trust"), TEXT("familiarity") });
		SetAdditionalPropertiesAllowed(*RelationshipDeltaSchema, false);
		RootProperties->SetObjectField(TEXT("relationship_delta"), RelationshipDeltaSchema);

		RootSchema->SetObjectField(TEXT("properties"), RootProperties);
		AddRequiredFieldList(*RootSchema, { TEXT("dialogue"), TEXT("actions"), TEXT("emotion_delta"), TEXT("relationship_delta") });

		return RootSchema;
	}

	inline TSharedRef<FJsonObject> BuildStructuredOutputInputSchema()
	{
		return BuildStructuredOutputParametersSchema();
	}
}
