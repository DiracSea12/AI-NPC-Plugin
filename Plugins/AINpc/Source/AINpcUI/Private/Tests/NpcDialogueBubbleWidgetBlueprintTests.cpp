// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Widgets/NpcDialogueBubbleWidget.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNpcDialogueBubbleWidgetBlueprintContractTest,
	"AINpc.US-1-T12.WidgetBlueprintContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNpcDialogueBubbleWidgetBlueprintContractTest::RunTest(const FString& Parameters)
{
	// US-1-T12: 验证 Widget 蓝图接口

	UClass* WidgetClass = UNpcDialogueBubbleWidget::StaticClass();

	// 1. 验证 Widget 不是抽象类（可以被实例化）
	TestFalse(TEXT("NpcDialogueBubbleWidget should not be abstract"),
		WidgetClass->HasAnyClassFlags(CLASS_Abstract));

	// 2. 验证 BindToNpcComponent 方法存在且是 BlueprintCallable
	UFunction* BindFunc = WidgetClass->FindFunctionByName(TEXT("BindToNpcComponent"));
	TestNotNull(TEXT("BindToNpcComponent method exists"), BindFunc);
	if (BindFunc)
	{
		TestTrue(TEXT("BindToNpcComponent is BlueprintCallable"),
			BindFunc->HasAnyFunctionFlags(FUNC_BlueprintCallable));
	}

	// 3. 验证 CharactersPerSecond 属性可在蓝图中编辑
	FProperty* CharPerSecProp = WidgetClass->FindPropertyByName(TEXT("CharactersPerSecond"));
	TestNotNull(TEXT("CharactersPerSecond property exists"), CharPerSecProp);
	if (CharPerSecProp)
	{
		TestTrue(TEXT("CharactersPerSecond is BlueprintReadWrite"),
			CharPerSecProp->HasAnyPropertyFlags(CPF_BlueprintVisible));
	}
	// 4. Verify default typewriter speed is 30 chars/sec (US-1-T9 contract).
	const UNpcDialogueBubbleWidget* WidgetCDO = WidgetClass->GetDefaultObject<UNpcDialogueBubbleWidget>();
	TestNotNull(TEXT("Widget CDO exists"), WidgetCDO);
	if (WidgetCDO)
	{
		TestEqual(TEXT("CharactersPerSecond default is 30"), WidgetCDO->CharactersPerSecond, 30.0f);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS


