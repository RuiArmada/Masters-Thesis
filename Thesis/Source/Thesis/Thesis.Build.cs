// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Thesis : ModuleRules
{
    public Thesis(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] 
        { 
            "Core", 
            "CoreUObject", 
            "Engine", 
            "InputCore", 
            "AIModule", 
            "XmlParser",   // XML Parser module
            "NavigationSystem"  // Add NavigationSystem module
        });
    }
}
