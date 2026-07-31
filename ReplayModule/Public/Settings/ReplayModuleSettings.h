// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Data/ReplayFrameData.h"
#include "ReplayModuleSettings.generated.h"

class UReplayWidget;
class UReplayLauncherWidget;

/**
 * Project Settings > Plugins > Replay Module.
 *
 * Everything a project normally wants to tune without touching Blueprints lives here; the recorder
 * reads these once when it starts, so a Blueprint may still override the interval per match through
 * UReplayRecorderSubsystem::SetRecordInterval.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Replay Module"))
class REPLAYMODULE_API UReplayModuleSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UReplayModuleSettings();

	virtual FName GetCategoryName() const override;

	// --- Recording ---

	/** Start recording by itself as soon as the map bounds can be resolved. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Recording")
	bool bAutoRecord = true;

	/** Seconds between two recorded frames. One frame per second reads well and costs very little. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Recording", meta = (ClampMin = "0.05", ClampMax = "60.0", UIMin = "0.1", UIMax = "10.0"))
	float RecordIntervalSeconds = 1.0f;

	/** Hard ceiling on frames. At the default interval this is two hours of match. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Recording", meta = (ClampMin = "10", ClampMax = "100000"))
	int32 MaxFrames = 7200;

	/** Markers per frame. Anything beyond this is dropped so a mass battle cannot blow up memory. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Recording", meta = (ClampMin = "16", ClampMax = "16384"))
	int32 MaxDotsPerFrame = 1024;

	/** Texture size the stored pixel radii are computed for. Playback rescales them. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Recording", meta = (ClampMin = "64", ClampMax = "2048"))
	int32 ReferenceTextureSize = 256;

	/** Record the camera footprint so the replay shows where the player was looking. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Recording")
	bool bRecordViewport = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Recording")
	EReplayVisibilityMode VisibilityMode = EReplayVisibilityMode::AsSeen;

	/** How long the recorder keeps looking for map bounds before it gives up on this match. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Recording", meta = (ClampMin = "1.0", ClampMax = "600.0"))
	float BootstrapTimeoutSeconds = 60.f;

	/** Interval of the retry/bootstrap timer while the recorder waits for a minimap to appear. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Recording", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float BootstrapRetryInterval = 1.0f;

	// --- Game end ---

	/**
	 * Watch for the end of the match by ourselves. With RTSUnitTemplate installed this notices the
	 * win/lose screen; without it, call UReplayFunctionLibrary::NotifyGameEnded from Blueprint.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Game End")
	bool bDetectGameEnd = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Game End", meta = (EditCondition = "bDetectGameEnd", ClampMin = "0.1", ClampMax = "5.0"))
	float GameEndPollInterval = 0.5f;

	/** Put the small "Replay" button on screen once the match is over. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Game End")
	bool bShowLauncherOnGameEnd = true;

	/** Z order of that button. Above the win/lose screen on purpose. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Game End")
	int32 LauncherZOrder = 500;

	/** Z order of the replay window itself. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Game End")
	int32 ReplayZOrder = 510;

	// --- Widgets ---

	/** Blueprint subclass used for the replay window. Empty falls back to the C++ layout. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Widgets", meta = (MetaClass = "/Script/ReplayModule.ReplayWidget"))
	TSoftClassPtr<UReplayWidget> ReplayWidgetClass;

	/** Blueprint subclass used for the end-of-match button. Empty falls back to the C++ layout. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Widgets", meta = (MetaClass = "/Script/ReplayModule.ReplayLauncherWidget"))
	TSoftClassPtr<UReplayLauncherWidget> LauncherWidgetClass;

	// --- Playback ---

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Playback", meta = (ClampMin = "0.1", ClampMax = "100.0"))
	float DefaultPlaybackSpeed = 4.f;

	/** Speeds the widget's speed button cycles through. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Playback")
	TArray<float> PlaybackSpeedSteps;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Playback")
	bool bLoopPlayback = true;

	/** Resolution the replay picture is rendered at. Independent of the recorded reference size. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Playback", meta = (ClampMin = "64", ClampMax = "2048"))
	int32 PlaybackTextureSize = 512;

	// --- Persistence ---

	/** Write the finished recording to a save game slot when the match ends. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Persistence")
	bool bAutoSaveOnGameEnd = false;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Persistence", meta = (EditCondition = "bAutoSaveOnGameEnd"))
	FString AutoSaveSlotName = TEXT("LastReplay");

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Persistence", meta = (EditCondition = "bAutoSaveOnGameEnd"))
	int32 AutoSaveUserIndex = 0;
};
