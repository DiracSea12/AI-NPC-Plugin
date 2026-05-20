// Copyright Epic Games, Inc. All Rights Reserved.

#include "K2Node_StartDialogue.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "KismetCompiler.h"
#include "Components/AINpcComponent.h"

#define LOCTEXT_NAMESPACE "K2Node_StartDialogue"

const FName UK2Node_StartDialogue::PN_Execute(TEXT("Execute"));
const FName UK2Node_StartDialogue::PN_Then(TEXT("Then"));
const FName UK2Node_StartDialogue::PN_Target(TEXT("Target"));
const FName UK2Node_StartDialogue::PN_PlayerInput(TEXT("PlayerInput"));

void UK2Node_StartDialogue::AllocateDefaultPins()
{
	// Execution pins
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, PN_Execute);
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, PN_Then);

	// Target (AINpcComponent)
	UEdGraphPin* TargetPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, UAINpcComponent::StaticClass(), PN_Target);
	TargetPin->PinToolTip = TEXT("The NPC component to start dialogue with");

	// PlayerInput (FString)
	UEdGraphPin* InputPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_String, PN_PlayerInput);
	InputPin->PinToolTip = TEXT("The player's dialogue input");

	Super::AllocateDefaultPins();
}

FText UK2Node_StartDialogue::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("NodeTitle", "Start Dialogue");
}

FText UK2Node_StartDialogue::GetTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Starts a dialogue with the NPC using the provided player input");
}

FLinearColor UK2Node_StartDialogue::GetNodeTitleColor() const
{
	return FLinearColor(0.2f, 0.8f, 0.2f);
}

FSlateIcon UK2Node_StartDialogue::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = FLinearColor::White;
	return FSlateIcon("EditorStyle", "GraphEditor.Event_16x");
}

void UK2Node_StartDialogue::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		check(NodeSpawner != nullptr);
		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

FText UK2Node_StartDialogue::GetMenuCategory() const
{
	return LOCTEXT("MenuCategory", "AI NPC");
}

void UK2Node_StartDialogue::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	// Find the function to call
	UFunction* Function = UAINpcComponent::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UAINpcComponent, StartDialogue));
	if (!Function)
	{
		CompilerContext.MessageLog.Error(*LOCTEXT("MissingFunction", "Could not find StartDialogue function on UAINpcComponent").ToString());
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
	UEdGraphPin* PlayerInputPin = FindPinChecked(PN_PlayerInput);

	UEdGraphPin* CallExecPin = CallFunctionNode->GetExecPin();
	UEdGraphPin* CallThenPin = CallFunctionNode->GetThenPin();
	UEdGraphPin* CallTargetPin = CallFunctionNode->FindPinChecked(UEdGraphSchema_K2::PN_Self);
	UEdGraphPin* CallPlayerInputPin = CallFunctionNode->FindPinChecked(TEXT("PlayerInput"));

	// Connect execution
	CompilerContext.MovePinLinksToIntermediate(*ExecPin, *CallExecPin);
	CompilerContext.MovePinLinksToIntermediate(*ThenPin, *CallThenPin);

	// Connect parameters
	CompilerContext.MovePinLinksToIntermediate(*TargetPin, *CallTargetPin);
	CompilerContext.MovePinLinksToIntermediate(*PlayerInputPin, *CallPlayerInputPin);

	BreakAllNodeLinks();
}

#undef LOCTEXT_NAMESPACE
