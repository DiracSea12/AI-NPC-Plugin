// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "UObject/Class.h"
#include "Components/AINpcComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAINpcBlueprintAPITest, "AINpc.Blueprint.DialogueFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcBlueprintAPITest::RunTest(const FString& Parameters)
{
	bool bStartExists = false;
	bool bResponseExists = false;
	bool bWidgetExists = false;

	// Check StartDialogue
	UClass* ComponentClass = UAINpcComponent::StaticClass();
	if (ComponentClass)
	{
		UFunction* StartFunc = ComponentClass->FindFunctionByName(TEXT("StartDialogue"));
		if (StartFunc && StartFunc->HasAnyFunctionFlags(FUNC_BlueprintCallable))
		{
			bStartExists = true;
		}
	}

	// Check OnDialogueResponse
	if (ComponentClass)
	{
		FProperty* ResponseProp = ComponentClass->FindPropertyByName(TEXT("OnDialogueResponse"));
		if (ResponseProp && ResponseProp->HasAnyPropertyFlags(CPF_BlueprintAssignable))
		{
			bResponseExists = true;
		}
	}

	// Check Widget using reflection (avoid cross-module dependency)
	UClass* WidgetClass = FindObject<UClass>(nullptr, TEXT("/Script/AINpcUI.NpcDialogueBubbleWidget"));
	if (WidgetClass && !WidgetClass->HasAnyClassFlags(CLASS_Abstract))
	{
		bWidgetExists = true;
	}

	if (bStartExists && bResponseExists && bWidgetExists)
	{
		UE_LOG(LogTemp, Display, TEXT("Blueprint dialogue ready"));
		return true;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("RUNTIME_FAIL: Missing Blueprint API - Start:%s Response:%s Widget:%s"),
			bStartExists ? TEXT("True") : TEXT("False"),
			bResponseExists ? TEXT("True") : TEXT("False"),
			bWidgetExists ? TEXT("True") : TEXT("False"));
		return false;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
