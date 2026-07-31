// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "ReplayModule.h"

#define LOCTEXT_NAMESPACE "FReplayModuleModule"

DEFINE_LOG_CATEGORY(LogReplayModule);

void FReplayModuleModule::StartupModule()
{
}

void FReplayModuleModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FReplayModuleModule, ReplayModule)
