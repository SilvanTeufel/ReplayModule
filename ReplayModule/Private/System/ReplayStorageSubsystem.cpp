// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "System/ReplayStorageSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "ReplayModule.h"
#include "Save/ReplaySaveGame.h"

UReplayStorageSubsystem* UReplayStorageSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return nullptr;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UReplayStorageSubsystem>() : nullptr;
}

void UReplayStorageSubsystem::Deinitialize()
{
	Recording.Reset();
	BackgroundTexture = nullptr;

	Super::Deinitialize();
}

bool UReplayStorageSubsystem::HasReplay() const
{
	return Recording.IsValid() && Recording->IsValid();
}

FReplayInfo UReplayStorageSubsystem::GetReplayInfo() const
{
	return Recording.IsValid() ? Recording->GetInfo() : FReplayInfo();
}

TSharedPtr<const FReplayRecording, ESPMode::ThreadSafe> UReplayStorageSubsystem::GetRecording() const
{
	return Recording;
}

void UReplayStorageSubsystem::SetRecording(TSharedPtr<FReplayRecording, ESPMode::ThreadSafe> InRecording)
{
	Recording = MoveTemp(InRecording);

	if (HasReplay())
	{
		const FReplayInfo Info = Recording->GetInfo();
		UE_LOG(LogReplayModule, Log, TEXT("Replay stored: %d frames, %.1fs, ~%d KB."),
			Info.FrameCount, Info.DurationSeconds, Info.ApproxMemoryKB);

		OnReplayAvailable.Broadcast();
	}
}

void UReplayStorageSubsystem::ClearReplay()
{
	Recording.Reset();
	BackgroundTexture = nullptr;
}

void UReplayStorageSubsystem::SetBackgroundTexture(UTexture2D* InTexture)
{
	BackgroundTexture = InTexture;
}

UTexture2D* UReplayStorageSubsystem::GetBackgroundTexture() const
{
	return BackgroundTexture;
}

bool UReplayStorageSubsystem::SaveReplayToSlot(const FString& SlotName, int32 UserIndex)
{
	if (!HasReplay() || SlotName.IsEmpty())
	{
		return false;
	}

	UReplaySaveGame* SaveGame = Cast<UReplaySaveGame>(UGameplayStatics::CreateSaveGameObject(UReplaySaveGame::StaticClass()));
	if (!SaveGame)
	{
		return false;
	}

	SaveGame->SaveVersion = UReplaySaveGame::CurrentSaveVersion;
	SaveGame->Recording = *Recording;

	const bool bSaved = UGameplayStatics::SaveGameToSlot(SaveGame, SlotName, UserIndex);
	UE_LOG(LogReplayModule, Log, TEXT("Saving replay to slot '%s': %s"), *SlotName, bSaved ? TEXT("ok") : TEXT("failed"));
	return bSaved;
}

bool UReplayStorageSubsystem::LoadReplayFromSlot(const FString& SlotName, int32 UserIndex)
{
	if (SlotName.IsEmpty() || !UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex))
	{
		return false;
	}

	UReplaySaveGame* SaveGame = Cast<UReplaySaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex));
	if (!SaveGame)
	{
		return false;
	}

	if (SaveGame->SaveVersion != UReplaySaveGame::CurrentSaveVersion)
	{
		UE_LOG(LogReplayModule, Warning, TEXT("Replay slot '%s' was written by version %d, this build expects %d - ignoring it."),
			*SlotName, SaveGame->SaveVersion, UReplaySaveGame::CurrentSaveVersion);
		return false;
	}

	if (!SaveGame->Recording.IsValid())
	{
		return false;
	}

	// A loaded replay brings no live terrain texture with it; the markers still render on fog.
	BackgroundTexture = nullptr;
	SetRecording(MakeShared<FReplayRecording, ESPMode::ThreadSafe>(SaveGame->Recording));
	return true;
}
