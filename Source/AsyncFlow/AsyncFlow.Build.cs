// AsyncFlow.Build.cs — Core module, no GAS dependency

using UnrealBuildTool;

public class AsyncFlow : ModuleRules
{
	public AsyncFlow(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"HTTP"
		});

		// Required for awaiters guarded by __has_include. These are hard link-time
		// dependencies when the corresponding headers are present. If your project
		// does not enable one of these plugins, remove the line and the awaiter
		// header will compile as a no-op via its __has_include guard.
		PrivateDependencyModuleNames.Add("Niagara");
		PrivateDependencyModuleNames.Add("UMG");
		PrivateDependencyModuleNames.Add("SlateCore");
		PrivateDependencyModuleNames.Add("MovieScene");
		PrivateDependencyModuleNames.Add("LevelSequence");
	}
}

