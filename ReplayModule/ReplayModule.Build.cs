// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;

public class ReplayModule : ModuleRules
{
	public ReplayModule(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"UMG",
				"DeveloperSettings"
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
				"InputCore",
				"RenderCore",
				"RHI"
			}
			);

		// The plugin is deliberately usable on its own: everything above is engine-only, and a
		// project without RTSUnitTemplate feeds the recorder through UReplayMarkerComponent or the
		// PushDot API. RTSUnitTemplate is only linked when it is actually installed next to us, so
		// the same source tree compiles in both kinds of project.
		bool bHasRTSUnitTemplate = IsPluginPresent("RTSUnitTemplate");
		PublicDefinitions.Add("WITH_RTSUNITTEMPLATE=" + (bHasRTSUnitTemplate ? "1" : "0"));

		if (bHasRTSUnitTemplate)
		{
			// Public because RTSUnitTemplate publishes the Mass/GAS modules our integration headers
			// pull in transitively through UnitBase.h.
			PublicDependencyModuleNames.Add("RTSUnitTemplate");

			// Unit health for the replay states is read off UAttributeSetBase. Its ATTRIBUTE_ACCESSORS
			// call through FGameplayAttributeData, which lives in GameplayAbilities - including the
			// header compiles fine without this, and then fails at link time.
			PublicDependencyModuleNames.Add("GameplayAbilities");

			// Projectiles are Mass entities, so recording them means running a Mass query.
			// MassCommon carries FTransformFragment, MassEntity the query and execution context.
			PublicDependencyModuleNames.Add("MassCore");
			PublicDependencyModuleNames.Add("MassEntity");
			PublicDependencyModuleNames.Add("MassCommon");
		}
	}

	/// <summary>
	/// Looks for &lt;PluginName&gt;.uplugin next to this plugin and in the engine's Marketplace folder.
	/// Only two directory levels are scanned so a project with a deep Plugins tree does not pay for
	/// a full recursive walk on every build.
	/// </summary>
	private bool IsPluginPresent(string PluginName)
	{
		string FileName = PluginName + ".uplugin";

		string[] SearchRoots =
		{
			Path.GetFullPath(Path.Combine(PluginDirectory, "..")),
			Path.GetFullPath(Path.Combine(EngineDirectory, "Plugins", "Marketplace")),
			Path.GetFullPath(Path.Combine(EngineDirectory, "Plugins"))
		};

		foreach (string Root in SearchRoots)
		{
			if (ContainsPluginFile(Root, FileName))
			{
				return true;
			}
		}

		return false;
	}

	private bool ContainsPluginFile(string Root, string FileName)
	{
		try
		{
			if (!Directory.Exists(Root))
			{
				return false;
			}

			foreach (string FirstLevel in Directory.GetDirectories(Root))
			{
				if (File.Exists(Path.Combine(FirstLevel, FileName)))
				{
					return true;
				}

				foreach (string SecondLevel in Directory.GetDirectories(FirstLevel))
				{
					if (File.Exists(Path.Combine(SecondLevel, FileName)))
					{
						return true;
					}
				}
			}
		}
		catch (Exception)
		{
			// An unreadable directory just means "not found here" - never fail the build over it.
		}

		return false;
	}
}
