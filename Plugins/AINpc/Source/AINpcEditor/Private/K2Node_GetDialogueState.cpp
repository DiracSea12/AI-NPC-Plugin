// Copyright Epic Games, Inc. All Rights Reserved.

#include "K2Node_GetDialogueState.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "KismetCompiler.h"
#include "Components/AINpcComponent.h"

#define LOCTEXT_NAMESPACE "K2Node_GetDialogueState"

const FName UK2Node_GetDialogueState::PN_Execute(TEXT("Execute"));
const FName UK2Node_GetDialogueState::PN_Then(TEXT("Then"));
const FName UK2Node_GetDialogueState::PN_Target(TEXT("Target"));
const FName UK2Node_GetDialogueState::PN_ReturnValue(TEXT("DialogueState"));

void UK2Node_GetDialogueState::AllocateDefaultPins()
{
	// Execution pins
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, PN_Execute);
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, PN_Then);

	// Target (AINpcComponent)
	UEdGraphPin* TargetPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, UAINpcComponent::StaticClass(), PN_Target);
	TargetPin->PinToolTip = TEXT("The NPC component to query");

	// Return value (ENpcDialogueState)
	UEdGraphPin* ReturnPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Byte, StaticEnum<ENpcDialogueState>(), PN_ReturnValue);
	ReturnPin->PinToolTip = TEXT("The current dialogue state of the NPC");

	Super::AllocateDefaultPins();
}

FText UK2Node_GetDialogueState::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("NodeTitle", "Get Dialogue State");
}

FText UK2Node_GetDialogueState::GetTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Gets the current dialogue state of the NPC (Idle, WaitingForLLM, Speaking, Cooldown)");
}

FLinearColor UK2Node_GetDialogueState::GetNodeTitleColor() const
{
	return FLinearColor(0.2f, 0.6f, 0.8f);
}

FSlateIcon UK2Node_GetDialogueState::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = FLinearColor::White;
	return FSlateIcon("EditorStyle", "GraphEditor.Macro.Loop_16x");
}

void UK2Node_GetDialogueState::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		check(NodeSpawner != nullptr);
		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

FText UK2Node_GetDialogueState::GetMenuCategory() const
{
	return LOCTEXT("MenuCategory", "AI NPC");
}

void UK2Node_GetDialogueState::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	// Find the function to call
	UFunction* Function = UAINpcComponent::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UAINpcComponent, GetDialogueState));
	if (!Function)
	{
		CompilerContext.MessageLog.Error(*LOCTEXT("MissingFunction", "Could not find GetDialogueState function on UAINpcComponent").ToString());
		return;
	}

	// Create a call function node
	UK2Node_CallFunction* CallFunctionNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CallFunctionNode->FunctionReference.SetExternalMember(Function->GetFName(), UAINpcComponent::StaticClass());
	CallFunctionNode->AllocateDefaultPins();

	// Move pins from this node to the call function node
	UEdGraphPin* ExecPin = GetExecPin();
	UEdGraphPin* ThenPin = FindPinChecked(PN_Then);
	UEdGraphPin* TargetPin = FindPinChecked(PN_Target);
	UEdGraphPin* ReturnPin = FindPinChecked(PN_ReturnValue);

	UEdGraphPin* CallExecPin = CallFunctionNode->GetExecPin();
	UEdGraphPin* CallThenPin = CallFunctionNode->GetThenPin();
	UEdGraphPin* CallTargetPin = CallFunctionNode->FindPinChecked(UEdGraphSchema_K2::PN_Self);
	UEdGraphPin* CallReturnPin = CallFunctionNode->GetReturnValuePin();

	// Connect execution
	CompilerContext.MovePinLinksToIntermediate(*ExecPin, *CallExecPin);
	CompilerContext.MovePinLinksToIntermediate(*ThenPin, *CallThenPin);

	// Connect parameters
	CompilerContext.MovePinLinksToIntermediate(*TargetPin, *CallTargetPin);
	CompilerContext.MovePinLinksToIntermediate(*ReturnPin, *CallReturnPin);

	BreakAllNodeLinks();
}

#undef LOCTEXT_NAMESPACE
