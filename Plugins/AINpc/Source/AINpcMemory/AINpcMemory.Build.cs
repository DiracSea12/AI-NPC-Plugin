using UnrealBuildTool;

public class AINpcMemory : ModuleRules
{
	public AINpcMemory(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AINpcCore"
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"Json",
			"JsonUtilities"
		});
	}
}
