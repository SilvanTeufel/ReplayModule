// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

REPLAYMODULE_API DECLARE_LOG_CATEGORY_EXTERN(LogReplayModule, Log, All);

class FReplayModuleModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
