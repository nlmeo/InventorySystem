// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ProjectXHG : ModuleRules
{
	public ProjectXHG(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
            //"EnhancedInput",
        });

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"ProjectXHG",
			"ProjectXHG/Variant_Platforming",
			"ProjectXHG/Variant_Platforming/Animation",
			"ProjectXHG/Variant_Combat",
			"ProjectXHG/Variant_Combat/AI",
			"ProjectXHG/Variant_Combat/Animation",
			"ProjectXHG/Variant_Combat/Gameplay",
			"ProjectXHG/Variant_Combat/Interfaces",
			"ProjectXHG/Variant_Combat/UI",
			"ProjectXHG/Variant_SideScrolling",
			"ProjectXHG/Variant_SideScrolling/AI",
			"ProjectXHG/Variant_SideScrolling/Gameplay",
			"ProjectXHG/Variant_SideScrolling/Interfaces",
			"ProjectXHG/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
