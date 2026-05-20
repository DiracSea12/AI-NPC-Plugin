using UnrealBuildTool;

public class VerifierHostTarget : TargetRules
{
	public VerifierHostTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("VerifierHost");
	}
}
