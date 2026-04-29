using UnrealBuildTool;

public class AINpcImmersion : ModuleRules
{
	public AINpcImmersion(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AINpcCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AINpcMemory"
		});
	}
}
