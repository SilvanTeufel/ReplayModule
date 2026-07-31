// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Blueprint/ReplayFunctionLibrary.h"

#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "ReplayModule.h"
#include "Settings/ReplayModuleSettings.h"
#include "System/ReplayRecorderSubsystem.h"
#include "System/ReplayStorageSubsystem.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/ReplayLauncherWidget.h"
#include "Widgets/ReplayWidget.h"

namespace
{
	UWorld* GetWorldSafe(const UObject* WorldContextObject)
	{
		return (GEngine && WorldContextObject)
			? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
			: nullptr;
	}

	/** The replay window already on screen for this world, if there is one. */
	UReplayWidget* FindOpenReplayWidget(const UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}

		for (TObjectIterator<UReplayWidget> It; It; ++It)
		{
			UReplayWidget* Widget = *It;
			if (!IsValid(Widget) || Widget->HasAnyFlags(RF_ClassDefaultObject))
			{
				continue;
			}

			if (Widget->GetWorld() == World && Widget->IsInViewport())
			{
				return Widget;
			}
		}

		return nullptr;
	}
}

UReplayWidget* UReplayFunctionLibrary::OpenReplayWindow(const UObject* WorldContextObject)
{
	UWorld* World = GetWorldSafe(WorldContextObject);
	if (!World)
	{
		return nullptr;
	}

	if (UReplayWidget* Existing = FindOpenReplayWidget(World))
	{
		return Existing;
	}

	const UReplayStorageSubsystem* Storage = UReplayStorageSubsystem::Get(WorldContextObject);
	if (!Storage || !Storage->HasReplay())
	{
		UE_LOG(LogReplayModule, Log, TEXT("OpenReplayWindow was called without a recording to show."));
		return nullptr;
	}

	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
	{
		return nullptr;
	}

	const UReplayModuleSettings* Settings = GetDefault<UReplayModuleSettings>();

	UClass* WidgetClass = Settings->ReplayWidgetClass.IsNull()
		? UReplayWidget::StaticClass()
		: Settings->ReplayWidgetClass.LoadSynchronous();

	if (!WidgetClass)
	{
		WidgetClass = UReplayWidget::StaticClass();
	}

	UReplayWidget* Widget = CreateWidget<UReplayWidget>(PC, WidgetClass);
	if (!Widget)
	{
		return nullptr;
	}

	Widget->AddToViewport(Settings->ReplayZOrder);
	return Widget;
}

void UReplayFunctionLibrary::CloseReplayWindow(const UObject* WorldContextObject)
{
	if (UReplayWidget* Widget = FindOpenReplayWidget(GetWorldSafe(WorldContextObject)))
	{
		Widget->CloseReplay();
	}
}

bool UReplayFunctionLibrary::HasReplay(const UObject* WorldContextObject)
{
	const UReplayStorageSubsystem* Storage = UReplayStorageSubsystem::Get(WorldContextObject);
	return Storage && Storage->HasReplay();
}

FReplayInfo UReplayFunctionLibrary::GetReplayInfo(const UObject* WorldContextObject)
{
	const UReplayStorageSubsystem* Storage = UReplayStorageSubsystem::Get(WorldContextObject);
	return Storage ? Storage->GetReplayInfo() : FReplayInfo();
}

void UReplayFunctionLibrary::NotifyGameEnded(const UObject* WorldContextObject)
{
	if (UReplayRecorderSubsystem* Recorder = UReplayRecorderSubsystem::Get(WorldContextObject))
	{
		Recorder->NotifyGameEnded();
	}
}

void UReplayFunctionLibrary::ShowReplayLauncher(const UObject* WorldContextObject)
{
	UWorld* World = GetWorldSafe(WorldContextObject);
	if (!World)
	{
		return;
	}

	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}

	const UReplayModuleSettings* Settings = GetDefault<UReplayModuleSettings>();

	UClass* LauncherClass = Settings->LauncherWidgetClass.IsNull()
		? UReplayLauncherWidget::StaticClass()
		: Settings->LauncherWidgetClass.LoadSynchronous();

	if (!LauncherClass)
	{
		LauncherClass = UReplayLauncherWidget::StaticClass();
	}

	if (UReplayLauncherWidget* Launcher = CreateWidget<UReplayLauncherWidget>(PC, LauncherClass))
	{
		Launcher->AddToViewport(Settings->LauncherZOrder);
	}
}

void UReplayFunctionLibrary::StartReplayRecording(const UObject* WorldContextObject, FVector2D WorldMin, FVector2D WorldMax)
{
	if (UReplayRecorderSubsystem* Recorder = UReplayRecorderSubsystem::Get(WorldContextObject))
	{
		Recorder->StartRecording(WorldMin, WorldMax);
	}
}

void UReplayFunctionLibrary::StopReplayRecording(const UObject* WorldContextObject)
{
	if (UReplayRecorderSubsystem* Recorder = UReplayRecorderSubsystem::Get(WorldContextObject))
	{
		Recorder->StopRecording();
	}
}

bool UReplayFunctionLibrary::IsReplayRecording(const UObject* WorldContextObject)
{
	const UReplayRecorderSubsystem* Recorder = UReplayRecorderSubsystem::Get(WorldContextObject);
	return Recorder && Recorder->IsRecording();
}

void UReplayFunctionLibrary::SetReplayRecordInterval(const UObject* WorldContextObject, float Seconds)
{
	if (UReplayRecorderSubsystem* Recorder = UReplayRecorderSubsystem::Get(WorldContextObject))
	{
		Recorder->SetRecordInterval(Seconds);
	}
}

bool UReplayFunctionLibrary::SaveReplayToSlot(const UObject* WorldContextObject, const FString& SlotName, int32 UserIndex)
{
	UReplayStorageSubsystem* Storage = UReplayStorageSubsystem::Get(WorldContextObject);
	return Storage && Storage->SaveReplayToSlot(SlotName, UserIndex);
}

bool UReplayFunctionLibrary::LoadReplayFromSlot(const UObject* WorldContextObject, const FString& SlotName, int32 UserIndex)
{
	UReplayStorageSubsystem* Storage = UReplayStorageSubsystem::Get(WorldContextObject);
	return Storage && Storage->LoadReplayFromSlot(SlotName, UserIndex);
}
