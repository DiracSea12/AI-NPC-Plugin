// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "K2Node_GetDialogueState.generated.h"

/**
 * Blueprint node for getting dialogue state from an NPC
 * Exposes UAINpcComponent::GetDialogueState to Blueprint
 */
UCLASS()
class AINPCEDITOR_API UK2Node_GetDialogueState : public UK2Node
{
	GENERATED_BODY()

public:
	// UEdGraphNode interface
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	// End of UEdGraphNode interface

	// UK2Node interface
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;
	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	// End of UK2Node interface

private:
	/** Pin names */
	static const FName PN_Execute;
	static const FName PN_Then;
	static const FName PN_Target;
	static const FName PN_ReturnValue;
};
