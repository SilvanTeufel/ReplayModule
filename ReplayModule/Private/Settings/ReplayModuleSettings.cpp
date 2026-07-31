// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Settings/ReplayModuleSettings.h"

UReplayModuleSettings::UReplayModuleSettings()
{
	PlaybackSpeedSteps = { 1.f, 2.f, 4.f, 8.f, 16.f };
}

FName UReplayModuleSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}
