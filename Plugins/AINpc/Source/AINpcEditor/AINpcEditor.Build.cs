using UnrealBuildTool;

public class AINpcEditor : ModuleRules
{
    public AINpcEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "UnrealEd",
            "AINpcCore",
            "AINpcUI",
            "AssetTools",
            "BlueprintGraph",
            "GraphEditor",
            "KismetCompiler",
            "Kismet",
            "Slate",
            "SlateCore"
        });
    }
}
