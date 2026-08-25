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

	/**
	 * Also record full unit states, which is what the viewport replay plays back.
	 * Off means minimap-only recordings, as before - noticeably smaller, but no 3D playback.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Recording")
	bool bRecordActorStates = true;

	/** Upper bound per frame, mirroring MaxDotsPerFrame. A state is ~28 bytes. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Recording", meta = (EditCondition = "bRecordActorStates", ClampMin = "16", ClampMax = "16384"))
	int32 MaxActorStatesPerFrame = 2048;

	/** Also record projectiles in flight, so the replay shows the shots and not just the shooters. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Recording", meta = (EditCondition = "bRecordActorStates"))
	bool bRecordProjectiles = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Recording", meta = (EditCondition = "bRecordProjectiles", ClampMin = "16", ClampMax = "16384"))
	int32 MaxProjectilesPerFrame = 1024;

	/** Also record work areas - resource nodes, build sites and base markers. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Recording", meta = (EditCondition = "bRecordActorStates"))
	bool bRecordWorkAreas = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Recording", meta = (EditCondition = "bRecordWorkAreas", ClampMin = "16", ClampMax = "16384"))
	int32 MaxWorkAreasPerFrame = 1024;

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

	/** Blueprint subclass used for the replay list. Empty falls back to the C++ layout. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Widgets", meta = (MetaClass = "/Script/ReplayModule.ReplayBrowserWidget"))
	TSoftClassPtr<class UReplayBrowserWidget> BrowserWidgetClass;

	// --- Playback ---

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Playback", meta = (ClampMin = "0.1", ClampMax = "100.0"))
	float DefaultPlaybackSpeed = 4.f;

	/** Speeds the widget's speed button cycles through. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Playback")
	TArray<float> PlaybackSpeedSteps;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Playback")
	bool bLoopPlayback = true;

	/**
	 * Upper bound for the viewport playback. Past roughly this the proxies jump between recorded
	 * frames instead of gliding, because the recording interval and not the frame rate becomes the
	 * limiting factor.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Playback", meta = (ClampMin = "1.0", ClampMax = "20.0"))
	float MaxPlaybackSpeed = 6.f;

	/** Play the recording in the viewport with unit proxies, not just on the minimap. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Playback")
	bool bEnableViewportPlayback = true;

	/**
	 * When the server opens a replay, take every connected client along: the recording is sent over
	 * and their windows open on the same moment. Without this the host watches alone while the
	 * clients keep playing the live match.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Playback")
	bool bShareReplayWithClients = true;

	/** Let the viewer click units during playback. Selection only - no orders are ever issued. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Playback", meta = (EditCondition = "bEnableViewportPlayback"))
	bool bAllowUnitSelection = true;

	/** Where the minimap sits while the viewport replay runs. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Playback", meta = (EditCondition = "bEnableViewportPlayback"))
	FVector2D MinimapAnchor = FVector2D(1.f, 0.f);

	/** Minimap edge length during viewport playback, in pixels. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Playback", meta = (EditCondition = "bEnableViewportPlayback", ClampMin = "64.0", ClampMax = "1024.0"))
	float MinimapOverlaySize = 320.f;

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
