// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "UObject/Class.h"
#include "Components/AINpcComponent.h"
#include "Data/NpcPersonaDataAsset.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAINpcBlueprintContractTest, "AINpc.US-1-T12.BlueprintContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcBlueprintContractTest::RunTest(const FString& Parameters)
{
	// US-1-T12: 验证纯蓝图可完成完整对话流程

	// 1. 验证 AINpcComponent 不是抽象类（可以被实例化）
	UClass* ComponentClass = UAINpcComponent::StaticClass();
	TestFalse(TEXT("AINpcComponent should not be abstract"),
		ComponentClass->HasAnyClassFlags(CLASS_Abstract));

	// 2. 验证关键方法是 BlueprintCallable
	UFunction* StartDialogueFunc = ComponentClass->FindFunctionByName(TEXT("StartDialogue"));
	TestNotNull(TEXT("StartDialogue method exists"), StartDialogueFunc);
	if (StartDialogueFunc)
	{
		TestTrue(TEXT("StartDialogue is BlueprintCallable"),
			StartDialogueFunc->HasAnyFunctionFlags(FUNC_BlueprintCallable));
	}

	UFunction* EndDialogueFunc = ComponentClass->FindFunctionByName(TEXT("EndDialogue"));
	TestNotNull(TEXT("EndDialogue method exists"), EndDialogueFunc);
	if (EndDialogueFunc)
	{
		TestTrue(TEXT("EndDialogue is BlueprintCallable"),
			EndDialogueFunc->HasAnyFunctionFlags(FUNC_BlueprintCallable));
	}

	UFunction* SetPersonaDataFunc = ComponentClass->FindFunctionByName(TEXT("SetPersonaData"));
	TestNotNull(TEXT("SetPersonaData method exists"), SetPersonaDataFunc);
	if (SetPersonaDataFunc)
	{
		TestTrue(TEXT("SetPersonaData is BlueprintCallable"),
			SetPersonaDataFunc->HasAnyFunctionFlags(FUNC_BlueprintCallable));
	}

	// 3. 验证委托属性是 BlueprintAssignable
	FProperty* OnDialogueResponseProp = ComponentClass->FindPropertyByName(TEXT("OnDialogueResponse"));
	TestNotNull(TEXT("OnDialogueResponse delegate exists"), OnDialogueResponseProp);
	if (OnDialogueResponseProp)
	{
		TestTrue(TEXT("OnDialogueResponse is BlueprintAssignable"),
			OnDialogueResponseProp->HasAnyPropertyFlags(CPF_BlueprintAssignable));
	}

	FProperty* OnDialogueErrorProp = ComponentClass->FindPropertyByName(TEXT("OnDialogueError"));
	TestNotNull(TEXT("OnDialogueError delegate exists"), OnDialogueErrorProp);
	if (OnDialogueErrorProp)
	{
		TestTrue(TEXT("OnDialogueError is BlueprintAssignable"),
			OnDialogueErrorProp->HasAnyPropertyFlags(CPF_BlueprintAssignable));
	}

	// 4. 验证 NpcPersonaDataAsset 不是抽象类
	UClass* PersonaClass = UNpcPersonaDataAsset::StaticClass();
	TestFalse(TEXT("NpcPersonaDataAsset should not be abstract"),
		PersonaClass->HasAnyClassFlags(CLASS_Abstract));

	// 5. 验证 ApiKeyOverride 属性是 BlueprintReadWrite (F1)
	FProperty* ApiKeyOverrideProp = ComponentClass->FindPropertyByName(TEXT("ApiKeyOverride"));
	TestNotNull(TEXT("ApiKeyOverride property exists"), ApiKeyOverrideProp);
	if (ApiKeyOverrideProp)
	{
		TestTrue(TEXT("ApiKeyOverride is BlueprintVisible"),
			ApiKeyOverrideProp->HasAnyPropertyFlags(CPF_BlueprintVisible));
	}

	// 6. 验证 ModelOverride 属性是 BlueprintReadWrite (F3)
	FProperty* ModelOverrideProp = ComponentClass->FindPropertyByName(TEXT("ModelOverride"));
	TestNotNull(TEXT("ModelOverride property exists"), ModelOverrideProp);
	if (ModelOverrideProp)
	{
		TestTrue(TEXT("ModelOverride is BlueprintVisible"),
			ModelOverrideProp->HasAnyPropertyFlags(CPF_BlueprintVisible));
	}

	// 7. 验证 SetApiKey 方法是 BlueprintCallable (F6)
	UFunction* SetApiKeyFunc = ComponentClass->FindFunctionByName(TEXT("SetApiKey"));
	TestNotNull(TEXT("SetApiKey method exists"), SetApiKeyFunc);
	if (SetApiKeyFunc)
	{
		TestTrue(TEXT("SetApiKey is BlueprintCallable"),
			SetApiKeyFunc->HasAnyFunctionFlags(FUNC_BlueprintCallable));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
