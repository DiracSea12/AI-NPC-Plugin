// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AINpcExampleAssetGenerator.generated.h"

/**
 * Editor utility to generate example Blueprint assets for AI NPC Plugin.
 * Creates BP_ExampleNpc and DA_ExamplePersona at Content/AINpc/Examples/
 */
UCLASS()
class AINPCEDITOR_API UAINpcExampleAssetGenerator : public UObject
{
	GENERATED_BODY()

public:
	/** Generate example Blueprint and DataAsset files */
	static bool GenerateExampleAssets(FString& OutMessage);
	static bool GenerateExampleAssetsForStateTree(class UStateTree* DefaultStateTreeAsset, FString& OutMessage);

private:
	static bool CreateExamplePersonaDataAsset(const FString& AssetPath, class UNpcPersonaDataAsset*& OutPersonaAsset, FString& OutMessage);
	static bool CreateExampleNpcBlueprint(
		const FString& AssetPath,
		class UNpcPersonaDataAsset* PersonaAsset,
		class UStateTree* DefaultStateTreeAsset,
		FString& OutMessage);
};
