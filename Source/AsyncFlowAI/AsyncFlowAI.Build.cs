// AsyncFlowAI.Build.cs — AI integration module

using UnrealBuildTool;

public class AsyncFlowAI : ModuleRules
{
	public AsyncFlowAI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AIModule",
			"NavigationSystem",
			"AsyncFlow"
		});
	}
}

