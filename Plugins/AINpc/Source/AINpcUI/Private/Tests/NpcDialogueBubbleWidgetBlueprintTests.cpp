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
	const auto RequireFunctionFlags = [this, WidgetClass](const TCHAR* FunctionName, const EFunctionFlags RequiredFlags)
	{
		UFunction* Function = WidgetClass->FindFunctionByName(FunctionName);
		TestNotNull(FString::Printf(TEXT("%s method exists"), FunctionName), Function);
		if (Function)
		{
			TestTrue(
				FString::Printf(TEXT("%s has required Blueprint flags"), FunctionName),
				Function->HasAnyFunctionFlags(RequiredFlags));
		}
	};

	const auto RequirePropertyFlags = [this, WidgetClass](const TCHAR* PropertyName, const EPropertyFlags RequiredFlags)
	{
		FProperty* Property = WidgetClass->FindPropertyByName(PropertyName);
		TestNotNull(FString::Printf(TEXT("%s property exists"), PropertyName), Property);
		if (Property)
		{
			TestTrue(
				FString::Printf(TEXT("%s has required Blueprint/edit flags"), PropertyName),
				Property->HasAnyPropertyFlags(RequiredFlags));
		}
	};

	// 1. 验证 Widget 不是抽象类（可以被实例化）
	TestFalse(TEXT("NpcDialogueBubbleWidget should not be abstract"),
		WidgetClass->HasAnyClassFlags(CLASS_Abstract));

	// 2. 验证 Widget 操作方法存在且是 BlueprintCallable
	RequireFunctionFlags(TEXT("BindToNpcComponent"), FUNC_BlueprintCallable);
	RequireFunctionFlags(TEXT("SetDialogueText"), FUNC_BlueprintCallable);
	RequireFunctionFlags(TEXT("AppendPartialText"), FUNC_BlueprintCallable);
	RequireFunctionFlags(TEXT("HandlePartialResponse"), FUNC_BlueprintCallable);
	RequireFunctionFlags(TEXT("ShowResponseText"), FUNC_BlueprintCallable);
	RequireFunctionFlags(TEXT("GetFullResponseText"), FUNC_BlueprintCallable);

	// 3. 验证显示设置和 partial delegate 可供蓝图使用
	RequirePropertyFlags(TEXT("OnPartialResponse"), CPF_BlueprintAssignable);
	RequirePropertyFlags(TEXT("CharactersPerSecond"), CPF_Edit | CPF_BlueprintVisible);
	RequirePropertyFlags(TEXT("bUseTypewriterEffect"), CPF_Edit | CPF_BlueprintVisible);
	RequirePropertyFlags(TEXT("bEnableStreamingDisplay"), CPF_Edit | CPF_BlueprintVisible);
	RequirePropertyFlags(TEXT("StreamingDisplayMode"), CPF_Edit | CPF_BlueprintVisible);
	RequirePropertyFlags(TEXT("StreamBufferOverflowThreshold"), CPF_Edit | CPF_BlueprintVisible);

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


