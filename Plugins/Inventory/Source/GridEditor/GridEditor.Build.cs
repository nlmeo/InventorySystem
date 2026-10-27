// Copyright 2025, ProjectXHG. All right reserved.

using UnrealBuildTool;

public class GridEditor : ModuleRules
{
    public GridEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "Inventory"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "UnrealEd",
                "AssetTools",
                "AssetRegistry",
                "PropertyEditor",
                "Slate",
                "SlateCore",
                "InputCore",
                "ToolMenus",
                "EditorFramework",
                "UMG"
            }
        );
    }
}
