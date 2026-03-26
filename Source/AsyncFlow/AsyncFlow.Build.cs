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
			"Engine"
		});

		PrivateDependencyModuleNames.Add("HTTP");

		// Optional modules — awaiters use __has_include guards, so these are
		// only needed at link time if the corresponding headers are found.
		AddOptionalDependency("Niagara");
		AddOptionalDependency("UMG");
		AddOptionalDependency("SlateCore");
		AddOptionalDependency("MovieScene");
		AddOptionalDependency("LevelSequence");
	}

	private void AddOptionalDependency(string ModuleName)
	{
		// ConditionalAddModuleDirectory is not available; use try-add pattern.
		// These are private so they only affect this module's compilation.
		PrivateDependencyModuleNames.Add(ModuleName);
	}
}

