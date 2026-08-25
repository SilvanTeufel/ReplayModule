// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Data/ReplayFrameData.h"
#include "ReplaySaveGame.generated.h"

/** One entry in the replay index: enough to fill a list row without loading the recording itself. */
USTRUCT(BlueprintType)
struct REPLAYMODULE_API FReplaySlotInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Replay")
	FString SlotName;

	UPROPERTY(BlueprintReadOnly, Category = "Replay")
	FString MapName;

	UPROPERTY(BlueprintReadOnly, Category = "Replay")
	FDateTime RecordedAtUtc = FDateTime();

	UPROPERTY(BlueprintReadOnly, Category = "Replay")
	float DurationSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Replay")
	int32 FrameCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Replay")
	bool bHasViewportData = false;
};

/**
 * Index of every stored replay.
 *
 * Kept because UE offers no way to enumerate save slots: DoesSaveGameExist answers for one name at
 * a time, and walking the save directory only works where saves happen to be loose files. So the
 * module maintains this list itself, in its own well-known slot.
 */
UCLASS()
class REPLAYMODULE_API UReplayIndexSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	static const TCHAR* GetIndexSlotName() { return TEXT("ReplayIndex"); }

	UPROPERTY()
	TArray<FReplaySlotInfo> Slots;
};

/**
 * Carries a whole recording to disk. Only the CPU-side data goes along - a save game cannot keep a
 * live terrain texture, so a loaded replay falls back to fog plus markers unless the project also
 * stored background pixels while recording.
 */
UCLASS()
class REPLAYMODULE_API UReplaySaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** Bumped whenever FReplayRecording changes shape, so a stale slot can be rejected. */
	static constexpr int32 CurrentSaveVersion = 1;

	UPROPERTY()
	int32 SaveVersion = CurrentSaveVersion;

	UPROPERTY()
	FReplayRecording Recording;
};
