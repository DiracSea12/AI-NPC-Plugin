using UnrealBuildTool;

public class AINpcCore : ModuleRules
{
    private bool bUseSmartObjects = true;

    public AINpcCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "DeveloperSettings",
            "Engine",
            "AIModule",
            "GameplayTags"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "GameplayStateTreeModule",
            "HTTP",
            "Json",
            "JsonUtilities",
            "StateTreeModule"
        });

        if (bUseSmartObjects)
        {
            PrivateDependencyModuleNames.Add("SmartObjectsModule");
            PublicDefinitions.Add("WITH_SMARTOBJECTS=1");
        }
        else
        {
            PublicDefinitions.Add("WITH_SMARTOBJECTS=0");
        }

        if (Target.Type == TargetType.Editor)
        {
            PrivateDependencyModuleNames.AddRange(new[]
            {
                "UnrealEd"
            });
        }

        // Automation tests need to be available in Development builds
        if (Target.Configuration != UnrealTargetConfiguration.Shipping)
        {
            PrivateDependencyModuleNames.Add("AutomationTest");

            // Gauntlet-based gameplay test controller for NPC behavior scenarios
            if (Target.Type != TargetType.Server)
            {
                PrivateDependencyModuleNames.Add("Gauntlet");
            }
        }
    }
}
