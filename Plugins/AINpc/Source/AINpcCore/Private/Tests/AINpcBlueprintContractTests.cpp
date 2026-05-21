// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "UObject/Class.h"
#include "Components/AINpcComponent.h"
#include "Controllers/AINpcController.h"
#include "Data/NpcPersonaDataAsset.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAINpcBlueprintContractTest, "AINpc.US-1-T12.BlueprintContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAINpcBlueprintContractTest::RunTest(const FString& Parameters)
{
	// US-1-T12: 验证纯蓝图可完成完整对话流程

	const auto RequireFunctionFlags = [this](UClass* Class, const TCHAR* FunctionName, const EFunctionFlags RequiredFlags)
	{
		UFunction* Function = Class ? Class->FindFunctionByName(FunctionName) : nullptr;
		TestNotNull(FString::Printf(TEXT("%s function exists"), FunctionName), Function);
		if (Function)
		{
			TestTrue(
				FString::Printf(TEXT("%s has required Blueprint flags"), FunctionName),
				Function->HasAnyFunctionFlags(RequiredFlags));
		}
	};

	const auto RequirePropertyFlags = [this](UClass* Class, const TCHAR* PropertyName, const EPropertyFlags RequiredFlags)
	{
		FProperty* Property = Class ? Class->FindPropertyByName(PropertyName) : nullptr;
		TestNotNull(FString::Printf(TEXT("%s property exists"), PropertyName), Property);
		if (Property)
		{
			TestTrue(
				FString::Printf(TEXT("%s has required Blueprint/edit flags"), PropertyName),
				Property->HasAnyPropertyFlags(RequiredFlags));
		}
	};

	// 1. 验证 AINpcComponent 不是抽象类（可以被实例化）
	UClass* ComponentClass = UAINpcComponent::StaticClass();
	TestFalse(TEXT("AINpcComponent should not be abstract"),
		ComponentClass->HasAnyClassFlags(CLASS_Abstract));

	// 2. 验证关键方法是 BlueprintCallable
	RequireFunctionFlags(ComponentClass, TEXT("StartDialogue"), FUNC_BlueprintCallable);
	RequireFunctionFlags(ComponentClass, TEXT("SendMessage"), FUNC_BlueprintCallable);
	RequireFunctionFlags(ComponentClass, TEXT("EndDialogue"), FUNC_BlueprintCallable);
	RequireFunctionFlags(ComponentClass, TEXT("GetNpcResponse"), FUNC_BlueprintCallable);
	RequireFunctionFlags(ComponentClass, TEXT("SetPersonaData"), FUNC_BlueprintCallable);
	RequireFunctionFlags(ComponentClass, TEXT("SetDialogueStateFromStateTree"), FUNC_BlueprintCallable);
	RequireFunctionFlags(ComponentClass, TEXT("IsDialogueActive"), FUNC_BlueprintPure);
	RequireFunctionFlags(ComponentClass, TEXT("IsRequestInFlight"), FUNC_BlueprintPure);
	RequireFunctionFlags(ComponentClass, TEXT("GetDialogueState"), FUNC_BlueprintPure);
	RequireFunctionFlags(ComponentClass, TEXT("SupportsStateTreeAutoController"), FUNC_BlueprintPure);
	RequireFunctionFlags(ComponentClass, TEXT("HasBeenInDialogueStateLongerThan"), FUNC_BlueprintPure);
	RequireFunctionFlags(ComponentClass, TEXT("IsDelayMaskingActive"), FUNC_BlueprintPure);

	// 3. 验证委托属性是 BlueprintAssignable
	RequirePropertyFlags(ComponentClass, TEXT("OnDialogueSessionStarted"), CPF_BlueprintAssignable);
	RequirePropertyFlags(ComponentClass, TEXT("OnDialogueResponse"), CPF_BlueprintAssignable);
	RequirePropertyFlags(ComponentClass, TEXT("OnPartialResponse"), CPF_BlueprintAssignable);
	RequirePropertyFlags(ComponentClass, TEXT("OnDialogueError"), CPF_BlueprintAssignable);
	RequirePropertyFlags(ComponentClass, TEXT("OnDialogueSessionEnded"), CPF_BlueprintAssignable);
	RequirePropertyFlags(ComponentClass, TEXT("OnDelayMaskingStart"), CPF_BlueprintAssignable);
	RequirePropertyFlags(ComponentClass, TEXT("OnDelayMaskingEnd"), CPF_BlueprintAssignable);
	RequirePropertyFlags(ComponentClass, TEXT("OnDialogueDegraded"), CPF_BlueprintAssignable);

	// 4. 验证 NpcPersonaDataAsset 不是抽象类
	UClass* PersonaClass = UNpcPersonaDataAsset::StaticClass();
	TestFalse(TEXT("NpcPersonaDataAsset should not be abstract"),
		PersonaClass->HasAnyClassFlags(CLASS_Abstract));

	// 5. 验证组件设置项是可在蓝图/Details 中配置的 US-1 设置面
	RequirePropertyFlags(ComponentClass, TEXT("PersonaDataAsset"), CPF_Edit | CPF_BlueprintVisible);
	RequirePropertyFlags(ComponentClass, TEXT("DefaultStateTreeAsset"), CPF_Edit | CPF_BlueprintVisible);
	RequirePropertyFlags(ComponentClass, TEXT("bAutoCreateNpcController"), CPF_Edit | CPF_BlueprintVisible);

	// 6. 验证 Controller 的 StateTree setup / diagnostics 也是蓝图可达的
	UClass* ControllerClass = AAINpcController::StaticClass();
	TestFalse(TEXT("AINpcController should not be abstract"),
		ControllerClass->HasAnyClassFlags(CLASS_Abstract));
	RequireFunctionFlags(ControllerClass, TEXT("ConfigureFromComponent"), FUNC_BlueprintCallable);
	RequireFunctionFlags(ControllerClass, TEXT("SetDefaultStateTreeAsset"), FUNC_BlueprintCallable);
	RequireFunctionFlags(ControllerClass, TEXT("GetStateTreeAIComponent"), FUNC_BlueprintPure);
	RequireFunctionFlags(ControllerClass, TEXT("HasValidStateTreeBinding"), FUNC_BlueprintPure);
	RequireFunctionFlags(ControllerClass, TEXT("GetStateTreeBindingFailureReason"), FUNC_BlueprintPure);
	RequireFunctionFlags(ControllerClass, TEXT("GetResolvedStateTreeAsset"), FUNC_BlueprintPure);

	// 7. 验证 Persona/provider/delay masking 数据可由内容作者编辑
	RequirePropertyFlags(PersonaClass, TEXT("PersonaName"), CPF_Edit | CPF_BlueprintVisible);
	RequirePropertyFlags(PersonaClass, TEXT("Background"), CPF_Edit | CPF_BlueprintVisible);
	RequirePropertyFlags(PersonaClass, TEXT("SpeakingStyle"), CPF_Edit | CPF_BlueprintVisible);
	RequirePropertyFlags(PersonaClass, TEXT("DelayMaskingMontages"), CPF_Edit | CPF_BlueprintVisible);
	RequirePropertyFlags(PersonaClass, TEXT("DelayFillerThreshold"), CPF_Edit | CPF_BlueprintVisible);
	RequirePropertyFlags(PersonaClass, TEXT("DelayFillerTexts"), CPF_Edit | CPF_BlueprintVisible);
	RequirePropertyFlags(PersonaClass, TEXT("FailureFallbackResponse"), CPF_Edit | CPF_BlueprintVisible);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
