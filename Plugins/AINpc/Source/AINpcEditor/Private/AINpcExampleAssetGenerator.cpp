// Copyright Epic Games, Inc. All Rights Reserved.

#include "AINpcExampleAssetGenerator.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/BlueprintFactory.h"
#include "Factories/DataAssetFactory.h"
#include "StateTreeFactory.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Components/AINpcComponent.h"
#include "Data/NpcPersonaDataAsset.h"
#include "GameFramework/Character.h"
#include "Components/StateTreeComponentSchema.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/SavePackage.h"
#include "PackageTools.h"
#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_ComponentBoundEvent.h"
#include "EdGraphSchema_K2.h"
#include "Kismet/KismetSystemLibrary.h"
#include "StateTree.h"

namespace
{
	const TCHAR* ExamplePersonaConfigFileName = TEXT("AINpcExamplePersona.txt");

	bool TryReadExamplePersonaField(const FString& Line, const TCHAR* FieldName, FString& OutValue)
	{
		FString Left;
		FString Right;
		if (!Line.Split(TEXT("="), &Left, &Right, ESearchCase::CaseSensitive))
		{
			return false;
		}

		Left.TrimStartAndEndInline();
		if (Left != FieldName)
		{
			return false;
		}

		Right.TrimStartAndEndInline();
		OutValue = MoveTemp(Right);
		return true;
	}

	bool LoadExamplePersonaText(FString& OutPersonaName, FString& OutBackground, FString& OutSpeakingStyle, FString& OutDefaultPlayerInput, FString& OutMessage)
	{
		const FString ConfigPath = FPaths::Combine(FPaths::ProjectConfigDir(), ExamplePersonaConfigFileName);
		FString ConfigText;
		if (!FFileHelper::LoadFileToString(ConfigText, *ConfigPath))
		{
			OutMessage = TEXT("Failed to load example persona config: ") + ConfigPath;
			return false;
		}

		TArray<FString> Lines;
		ConfigText.ParseIntoArrayLines(Lines, false);
		for (const FString& RawLine : Lines)
		{
			FString Line = RawLine;
			Line.TrimStartAndEndInline();
			if (Line.IsEmpty())
			{
				continue;
			}

			FString Value;
			if (TryReadExamplePersonaField(Line, TEXT("PersonaName"), Value))
			{
				OutPersonaName = MoveTemp(Value);
			}
			else if (TryReadExamplePersonaField(Line, TEXT("Background"), Value))
			{
				OutBackground = MoveTemp(Value);
			}
			else if (TryReadExamplePersonaField(Line, TEXT("SpeakingStyle"), Value))
			{
				OutSpeakingStyle = MoveTemp(Value);
			}
			else if (TryReadExamplePersonaField(Line, TEXT("DefaultPlayerInput"), Value))
			{
				OutDefaultPlayerInput = MoveTemp(Value);
			}
		}

		if (OutPersonaName.IsEmpty() || OutBackground.IsEmpty() || OutSpeakingStyle.IsEmpty() || OutDefaultPlayerInput.IsEmpty())
		{
			OutMessage = TEXT("Example persona config must define PersonaName, Background, SpeakingStyle, and DefaultPlayerInput: ") + ConfigPath;
			return false;
		}

		return true;
	}
}

bool UAINpcExampleAssetGenerator::GenerateExampleAssets(FString& OutMessage)
{
	return GenerateExampleAssetsForStateTree(nullptr, OutMessage);
}

bool UAINpcExampleAssetGenerator::GenerateExampleAssetsForStateTree(UStateTree* DefaultStateTreeAsset, FString& OutMessage)
{
	const FString PersonaAssetPath = TEXT("/AINpc/Examples/DA_ExamplePersona");
	const FString NpcBlueprintPath = TEXT("/AINpc/Examples/BP_ExampleNpc");
	const FString StateTreeAssetPath = TEXT("/AINpc/Examples/ST_AINpcBasicDialogue");

	if (!DefaultStateTreeAsset)
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		UStateTreeFactory* StateTreeFactory = NewObject<UStateTreeFactory>();
		StateTreeFactory->SetSchemaClass(UStateTreeComponentSchema::StaticClass());
		DefaultStateTreeAsset = Cast<UStateTree>(AssetTools.CreateAsset(
			FPaths::GetBaseFilename(StateTreeAssetPath),
			FPaths::GetPath(StateTreeAssetPath),
			UStateTree::StaticClass(),
			StateTreeFactory));
		if (!DefaultStateTreeAsset)
		{
			OutMessage = TEXT("Failed to create example StateTree asset: ") + StateTreeAssetPath;
			return false;
		}

		DefaultStateTreeAsset->MarkPackageDirty();
		UPackage* StateTreePackage = DefaultStateTreeAsset->GetPackage();
		if (StateTreePackage)
		{
			const FString PackageFileName = FPackageName::LongPackageNameToFilename(StateTreePackage->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			if (!UPackage::SavePackage(StateTreePackage, DefaultStateTreeAsset, *PackageFileName, SaveArgs))
			{
				OutMessage = TEXT("Failed to save example StateTree package: ") + StateTreeAssetPath;
				return false;
			}
		}
	}

	// Create DataAsset first
	UNpcPersonaDataAsset* PersonaAsset = nullptr;
	if (!CreateExamplePersonaDataAsset(PersonaAssetPath, PersonaAsset, OutMessage))
	{
		return false;
	}

	// Create Blueprint
	if (!CreateExampleNpcBlueprint(NpcBlueprintPath, PersonaAsset, DefaultStateTreeAsset, OutMessage))
	{
		return false;
	}

	OutMessage = TEXT("Successfully generated example Phase-1 assets:\n- ") + PersonaAssetPath
		+ TEXT("\n- ") + NpcBlueprintPath
		+ TEXT("\n- ") + DefaultStateTreeAsset->GetPathName()
		+ TEXT("\nStateTree note: this generated asset proves the setup path assigns a valid UStateTree, but it is still diagnostic/editor setup evidence, not final visible dialogue acceptance.");
	return true;
}

bool UAINpcExampleAssetGenerator::CreateExamplePersonaDataAsset(const FString& AssetPath, UNpcPersonaDataAsset*& OutPersonaAsset, FString& OutMessage)
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
	Factory->DataAssetClass = UNpcPersonaDataAsset::StaticClass();

	UObject* NewAsset = AssetTools.CreateAsset(
		FPaths::GetBaseFilename(AssetPath),
		FPaths::GetPath(AssetPath),
		UNpcPersonaDataAsset::StaticClass(),
		Factory
	);

	if (!NewAsset)
	{
		OutMessage = TEXT("Failed to create DataAsset: ") + AssetPath;
		return false;
	}

	// Configure default values
	UNpcPersonaDataAsset* PersonaAsset = Cast<UNpcPersonaDataAsset>(NewAsset);
	if (!PersonaAsset)
	{
		OutMessage = TEXT("Failed to cast DataAsset: ") + AssetPath;
		return false;
	}

	FString PersonaName;
	FString Background;
	FString SpeakingStyle;
	FString DefaultPlayerInput;
	if (!LoadExamplePersonaText(PersonaName, Background, SpeakingStyle, DefaultPlayerInput, OutMessage))
	{
		return false;
	}

	PersonaAsset->PersonaName = MoveTemp(PersonaName);
	PersonaAsset->Background = MoveTemp(Background);
	PersonaAsset->SpeakingStyle = MoveTemp(SpeakingStyle);
	PersonaAsset->MarkPackageDirty();

	// Save package to disk
	UPackage* Package = PersonaAsset->GetPackage();
	if (Package)
	{
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(Package, PersonaAsset, *PackageFileName, SaveArgs))
		{
			OutMessage = TEXT("Failed to save DataAsset package: ") + AssetPath;
			return false;
		}
	}

	OutPersonaAsset = PersonaAsset;
	return true;
}

bool UAINpcExampleAssetGenerator::CreateExampleNpcBlueprint(
	const FString& AssetPath,
	UNpcPersonaDataAsset* PersonaAsset,
	UStateTree* DefaultStateTreeAsset,
	FString& OutMessage)
{
	if (!PersonaAsset)
	{
		OutMessage = TEXT("Invalid PersonaAsset provided");
		return false;
	}

	if (!DefaultStateTreeAsset)
	{
		OutMessage = TEXT("Invalid StateTree asset provided. Phase 1 example generation requires a valid StateTree asset.");
		return false;
	}

	FString PersonaName;
	FString Background;
	FString SpeakingStyle;
	FString DefaultPlayerInput;
	if (!LoadExamplePersonaText(PersonaName, Background, SpeakingStyle, DefaultPlayerInput, OutMessage))
	{
		return false;
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
	Factory->ParentClass = ACharacter::StaticClass();

	UObject* NewAsset = AssetTools.CreateAsset(
		FPaths::GetBaseFilename(AssetPath),
		FPaths::GetPath(AssetPath),
		UBlueprint::StaticClass(),
		Factory
	);

	if (!NewAsset)
	{
		OutMessage = TEXT("Failed to create Blueprint: ") + AssetPath;
		return false;
	}

	UBlueprint* Blueprint = Cast<UBlueprint>(NewAsset);
	if (!Blueprint)
	{
		OutMessage = TEXT("Failed to cast Blueprint: ") + AssetPath;
		return false;
	}

	// Add AINpcComponent
	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	USCS_Node* ComponentNode = nullptr;
	if (SCS)
	{
		ComponentNode = SCS->CreateNode(UAINpcComponent::StaticClass());
		if (ComponentNode)
		{
			SCS->AddNode(ComponentNode);
			UAINpcComponent* ComponentTemplate = Cast<UAINpcComponent>(ComponentNode->ComponentTemplate);
			if (ComponentTemplate)
			{
				ComponentTemplate->PersonaDataAsset = PersonaAsset;
				ComponentTemplate->DefaultStateTreeAsset = DefaultStateTreeAsset;
				ComponentTemplate->bAutoCreateNpcController = true;
			}
		}
	}

	// Add Event Graph nodes
	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	if (EventGraph)
	{
		// Create BeginPlay event
		UK2Node_Event* BeginPlayNode = NewObject<UK2Node_Event>(EventGraph);
		BeginPlayNode->EventReference.SetExternalMember(FName(TEXT("ReceiveBeginPlay")), AActor::StaticClass());
		BeginPlayNode->bOverrideFunction = true;
		EventGraph->AddNode(BeginPlayNode);
		BeginPlayNode->CreateNewGuid();
		BeginPlayNode->PostPlacedNewNode();
		BeginPlayNode->AllocateDefaultPins();
		BeginPlayNode->NodePosX = 0;
		BeginPlayNode->NodePosY = 0;

		// Create variable get node for AINpcComponent
		UK2Node_VariableGet* GetComponentNode = NewObject<UK2Node_VariableGet>(EventGraph);
		if (ComponentNode)
		{
			GetComponentNode->VariableReference.SetSelfMember(ComponentNode->GetVariableName());
		}
		EventGraph->AddNode(GetComponentNode);
		GetComponentNode->CreateNewGuid();
		GetComponentNode->PostPlacedNewNode();
		GetComponentNode->AllocateDefaultPins();
		GetComponentNode->NodePosX = 250;
		GetComponentNode->NodePosY = 50;

		// Create StartDialogue call
		UK2Node_CallFunction* StartDialogueNode = NewObject<UK2Node_CallFunction>(EventGraph);
		StartDialogueNode->FunctionReference.SetExternalMember(FName(TEXT("StartDialogue")), UAINpcComponent::StaticClass());
		EventGraph->AddNode(StartDialogueNode);
		StartDialogueNode->CreateNewGuid();
		StartDialogueNode->PostPlacedNewNode();
		StartDialogueNode->AllocateDefaultPins();
		StartDialogueNode->NodePosX = 500;
		StartDialogueNode->NodePosY = 0;

		// Connect BeginPlay -> StartDialogue exec pins
		UEdGraphPin* BeginPlayExecPin = BeginPlayNode->FindPin(UEdGraphSchema_K2::PN_Then);
		UEdGraphPin* StartDialogueExecPin = StartDialogueNode->FindPin(UEdGraphSchema_K2::PN_Execute);
		if (BeginPlayExecPin && StartDialogueExecPin)
		{
			BeginPlayExecPin->MakeLinkTo(StartDialogueExecPin);
		}

		// Connect component reference to StartDialogue target
		UEdGraphPin* GetComponentOutPin = GetComponentNode->GetValuePin();
		UEdGraphPin* StartDialogueTargetPin = StartDialogueNode->FindPin(UEdGraphSchema_K2::PN_Self);
		if (GetComponentOutPin && StartDialogueTargetPin)
		{
			GetComponentOutPin->MakeLinkTo(StartDialogueTargetPin);
		}

		// Set default value for PlayerInput parameter
		UEdGraphPin* PlayerInputPin = StartDialogueNode->FindPin(FName(TEXT("PlayerInput")));
		if (PlayerInputPin)
		{
			PlayerInputPin->DefaultValue = DefaultPlayerInput;
		}
	}

	// Compile Blueprint FIRST - GeneratedClass must exist before event binding
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	// Verify compilation succeeded
	if (!Blueprint->GeneratedClass)
	{
		OutMessage = TEXT("Blueprint compilation failed - GeneratedClass is null");
		return false;
	}

	// Now bind events - ComponentProperty requires GeneratedClass
	if (EventGraph && ComponentNode)
	{
		FObjectProperty* ComponentProperty = FindFProperty<FObjectProperty>(Blueprint->GeneratedClass, ComponentNode->GetVariableName());
		if (!ComponentProperty)
		{
			OutMessage = TEXT("Failed to find component property for event binding");
			return false;
		}

		// Create OnDialogueResponse bound event
		FMulticastDelegateProperty* OnResponseDelegate = FindFProperty<FMulticastDelegateProperty>(UAINpcComponent::StaticClass(), FName(TEXT("OnDialogueResponse")));
		if (!OnResponseDelegate)
		{
			OutMessage = TEXT("Failed to find OnDialogueResponse delegate");
			return false;
		}

		UK2Node_ComponentBoundEvent* OnResponseEvent = NewObject<UK2Node_ComponentBoundEvent>(EventGraph);
		OnResponseEvent->InitializeComponentBoundEventParams(ComponentProperty, OnResponseDelegate);
		EventGraph->AddNode(OnResponseEvent);
		OnResponseEvent->CreateNewGuid();
		OnResponseEvent->PostPlacedNewNode();
		OnResponseEvent->AllocateDefaultPins();
		OnResponseEvent->NodePosX = 0;
		OnResponseEvent->NodePosY = 300;

		// Create Print String for response
		UK2Node_CallFunction* PrintResponseNode = NewObject<UK2Node_CallFunction>(EventGraph);
		PrintResponseNode->FunctionReference.SetExternalMember(FName(TEXT("PrintString")), UKismetSystemLibrary::StaticClass());
		EventGraph->AddNode(PrintResponseNode);
		PrintResponseNode->CreateNewGuid();
		PrintResponseNode->PostPlacedNewNode();
		PrintResponseNode->AllocateDefaultPins();
		PrintResponseNode->NodePosX = 300;
		PrintResponseNode->NodePosY = 300;

		// Connect OnDialogueResponse -> Print String
		UEdGraphPin* ResponseExecPin = OnResponseEvent->FindPin(UEdGraphSchema_K2::PN_Then);
		UEdGraphPin* PrintResponseExecPin = PrintResponseNode->FindPin(UEdGraphSchema_K2::PN_Execute);
		if (ResponseExecPin && PrintResponseExecPin)
		{
			ResponseExecPin->MakeLinkTo(PrintResponseExecPin);
		}

		// Connect ResponseText to Print String input
		UEdGraphPin* ResponseTextPin = OnResponseEvent->FindPin(FName(TEXT("ResponseText")));
		UEdGraphPin* PrintStringInPin = PrintResponseNode->FindPin(FName(TEXT("InString")));
		if (ResponseTextPin && PrintStringInPin)
		{
			ResponseTextPin->MakeLinkTo(PrintStringInPin);
		}

		// Create OnDialogueError bound event
		FMulticastDelegateProperty* OnErrorDelegate = FindFProperty<FMulticastDelegateProperty>(UAINpcComponent::StaticClass(), FName(TEXT("OnDialogueError")));
		if (!OnErrorDelegate)
		{
			OutMessage = TEXT("Failed to find OnDialogueError delegate");
			return false;
		}

		UK2Node_ComponentBoundEvent* OnErrorEvent = NewObject<UK2Node_ComponentBoundEvent>(EventGraph);
		OnErrorEvent->InitializeComponentBoundEventParams(ComponentProperty, OnErrorDelegate);
		EventGraph->AddNode(OnErrorEvent);
		OnErrorEvent->CreateNewGuid();
		OnErrorEvent->PostPlacedNewNode();
		OnErrorEvent->AllocateDefaultPins();
		OnErrorEvent->NodePosX = 0;
		OnErrorEvent->NodePosY = 500;

		// Create Print String for error
		UK2Node_CallFunction* PrintErrorNode = NewObject<UK2Node_CallFunction>(EventGraph);
		PrintErrorNode->FunctionReference.SetExternalMember(FName(TEXT("PrintString")), UKismetSystemLibrary::StaticClass());
		EventGraph->AddNode(PrintErrorNode);
		PrintErrorNode->CreateNewGuid();
		PrintErrorNode->PostPlacedNewNode();
		PrintErrorNode->AllocateDefaultPins();
		PrintErrorNode->NodePosX = 300;
		PrintErrorNode->NodePosY = 500;

		// Connect OnDialogueError -> Print String
		UEdGraphPin* ErrorExecPin = OnErrorEvent->FindPin(UEdGraphSchema_K2::PN_Then);
		UEdGraphPin* PrintErrorExecPin = PrintErrorNode->FindPin(UEdGraphSchema_K2::PN_Execute);
		if (ErrorExecPin && PrintErrorExecPin)
		{
			ErrorExecPin->MakeLinkTo(PrintErrorExecPin);
		}

		// Connect ErrorMessage to Print String input
		UEdGraphPin* ErrorMessagePin = OnErrorEvent->FindPin(FName(TEXT("ErrorMessage")));
		UEdGraphPin* PrintErrorInPin = PrintErrorNode->FindPin(FName(TEXT("InString")));
		if (ErrorMessagePin && PrintErrorInPin)
		{
			ErrorMessagePin->MakeLinkTo(PrintErrorInPin);
		}

		// Recompile after adding event nodes
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
	}

	// Save package to disk
	UPackage* Package = Blueprint->GetPackage();
	if (Package)
	{
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(Package, Blueprint, *PackageFileName, SaveArgs))
		{
			OutMessage = TEXT("Failed to save Blueprint package: ") + AssetPath;
			return false;
		}
	}

	return true;
}
