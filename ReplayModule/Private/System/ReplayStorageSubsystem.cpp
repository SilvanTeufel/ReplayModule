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

	if (bSaved)
	{
		RememberSlot(SlotName, UserIndex);
	}

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

FString UReplayStorageSubsystem::SaveReplayToNewSlot(int32 UserIndex)
{
	if (!HasReplay())
	{
		UE_LOG(LogReplayModule, Warning, TEXT("Nothing to save - there is no recording."));
		return FString();
	}

	const FReplayInfo Info = GetReplayInfo();

	// Map plus local timestamp. Local rather than UTC because this name is shown to the player, and
	// a replay stamped two hours off is confusing when picking one from a list.
	FString Map = Info.MapName;
	Map.RemoveFromStart(TEXT("UEDPIE_0_"));
	Map.RemoveFromStart(TEXT("UEDPIE_1_"));
	if (Map.IsEmpty())
	{
		Map = TEXT("Replay");
	}

	const FString SlotName = FString::Printf(TEXT("Replay_%s_%s"), *Map, *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));

	return SaveReplayToSlot(SlotName, UserIndex) ? SlotName : FString();
}

void UReplayStorageSubsystem::RememberSlot(const FString& SlotName, int32 UserIndex)
{
	const TCHAR* IndexSlot = UReplayIndexSaveGame::GetIndexSlotName();

	UReplayIndexSaveGame* Index = Cast<UReplayIndexSaveGame>(
		UGameplayStatics::LoadGameFromSlot(IndexSlot, UserIndex));

	if (!Index)
	{
		Index = Cast<UReplayIndexSaveGame>(UGameplayStatics::CreateSaveGameObject(UReplayIndexSaveGame::StaticClass()));
		if (!Index)
		{
			return;
		}
	}

	const FReplayInfo Info = GetReplayInfo();

	FReplaySlotInfo Entry;
	Entry.SlotName = SlotName;
	Entry.MapName = Info.MapName;
	Entry.RecordedAtUtc = Info.RecordedAtUtc;
	Entry.DurationSeconds = Info.DurationSeconds;
	Entry.FrameCount = Info.FrameCount;
	Entry.bHasViewportData = Info.bHasViewportData;

	// Overwriting a slot must not leave two entries pointing at the same name.
	Index->Slots.RemoveAll([&SlotName](const FReplaySlotInfo& Existing)
	{
		return Existing.SlotName == SlotName;
	});

	Index->Slots.Add(Entry);

	UGameplayStatics::SaveGameToSlot(Index, IndexSlot, UserIndex);
}

TArray<FReplaySlotInfo> UReplayStorageSubsystem::GetStoredReplays(int32 UserIndex) const
{
	TArray<FReplaySlotInfo> Result;

	const UReplayIndexSaveGame* Index = Cast<UReplayIndexSaveGame>(
		UGameplayStatics::LoadGameFromSlot(UReplayIndexSaveGame::GetIndexSlotName(), UserIndex));

	if (!Index)
	{
		return Result;
	}

	Result = Index->Slots;

	// Entries whose save file is gone (deleted by hand, or a failed write) would just produce a row
	// that cannot be loaded, so they are filtered out here rather than surfacing as a broken click.
	Result.RemoveAll([UserIndex](const FReplaySlotInfo& Entry)
	{
		return !UGameplayStatics::DoesSaveGameExist(Entry.SlotName, UserIndex);
	});

	Result.Sort([](const FReplaySlotInfo& A, const FReplaySlotInfo& B)
	{
		return A.RecordedAtUtc > B.RecordedAtUtc;
	});

	return Result;
}

bool UReplayStorageSubsystem::DeleteStoredReplay(const FString& SlotName, int32 UserIndex)
{
	const bool bDeleted = UGameplayStatics::DeleteGameInSlot(SlotName, UserIndex);

	if (UReplayIndexSaveGame* Index = Cast<UReplayIndexSaveGame>(
		UGameplayStatics::LoadGameFromSlot(UReplayIndexSaveGame::GetIndexSlotName(), UserIndex)))
	{
		const int32 Removed = Index->Slots.RemoveAll([&SlotName](const FReplaySlotInfo& Entry)
		{
			return Entry.SlotName == SlotName;
		});

		if (Removed > 0)
		{
			UGameplayStatics::SaveGameToSlot(Index, UReplayIndexSaveGame::GetIndexSlotName(), UserIndex);
		}
	}

	UE_LOG(LogReplayModule, Log, TEXT("Deleting replay '%s': %s"), *SlotName, bDeleted ? TEXT("ok") : TEXT("failed"));
	return bDeleted;
}

