// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Data/ReplayFrameData.h"
#include "ReplaySaveGame.generated.h"

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
