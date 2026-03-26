// AsyncFlowGAS.Build.cs — GAS integration module

using UnrealBuildTool;

public class AsyncFlowGAS : ModuleRules
{
	public AsyncFlowGAS(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",
			"AsyncFlow"
		});
	}
}

