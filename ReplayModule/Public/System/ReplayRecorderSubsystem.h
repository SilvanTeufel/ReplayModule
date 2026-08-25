// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Data/ReplayFrameData.h"
#include "ReplayRecorderSubsystem.generated.h"

class UReplayMarkerComponent;
class UTexture2D;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnReplayRecordingStateChanged, bool, bIsRecording);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnReplayCaptureFrame);

/**
 * Samples the match into frames.
 *
 * Lives on the world (it needs actors and timers) but hands the finished recording to
 * UReplayStorageSubsystem on the GameInstance, so the result survives the level change that a
 * win/lose screen triggers.
 *
 * Three ways to feed it, all of which can be combined:
 *   - UReplayMarkerComponent on any actor (works in any project),
 *   - the RTSUnitTemplate integration, which discovers units and minimap bounds by itself,
 *   - PushDot from Blueprint for one-off markers.
 */
UCLASS()
class REPLAYMODULE_API UReplayRecorderSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UReplayRecorderSubsystem* Get(const UObject* WorldContextObject);

	// ~ USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	// ~ UWorldSubsystem
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	UPROPERTY(BlueprintAssignable, Category = "Replay")
	FOnReplayRecordingStateChanged OnRecordingStateChanged;

	/**
	 * Fires while a frame is being assembled. This is the one moment PushDot works, so a project
	 * that wants markers the built-in sources do not know about adds them from here.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Replay")
	FOnReplayCaptureFrame OnCaptureFrame;

	// --- Control ---

	/**
	 * Begins a recording over the given world bounds (the same rectangle the minimap covers).
	 * A recording already in progress is discarded.
	 */
	UFUNCTION(BlueprintCallable, Category = "Replay|Recording")
	void StartRecording(FVector2D WorldMin, FVector2D WorldMax);

	/** Ends the recording and publishes it to the storage subsystem. */
	UFUNCTION(BlueprintCallable, Category = "Replay|Recording")
	void StopRecording();

	UFUNCTION(BlueprintPure, Category = "Replay|Recording")
	bool IsRecording() const { return bRecording; }

	/** Takes effect immediately, including while recording. */
	UFUNCTION(BlueprintCallable, Category = "Replay|Recording")
	void SetRecordInterval(float Seconds);

	UFUNCTION(BlueprintPure, Category = "Replay|Recording")
	float GetRecordInterval() const { return RecordInterval; }

	/** Writes one frame out of band, e.g. right before a scripted event. */
	UFUNCTION(BlueprintCallable, Category = "Replay|Recording")
	void CaptureFrameNow();

	/**
	 * Team the recording is taken from, which decides friend/ally/enemy colors. The RTS integration
	 * fills this in by itself; set it before StartRecording in any other project that uses teams.
	 */
	UFUNCTION(BlueprintCallable, Category = "Replay|Recording")
	void SetLocalTeamId(int32 InTeamId);

	/** Style the frames are recorded with. Set it before StartRecording to take full effect. */
	UFUNCTION(BlueprintCallable, Category = "Replay|Recording")
	void SetStyle(const FReplayStyle& InStyle);

	UFUNCTION(BlueprintPure, Category = "Replay|Recording")
	FReplayStyle GetStyle() const { return WorkingRecording.IsValid() ? WorkingRecording->Style : PendingStyle; }

	/**
	 * Terrain layer for the replay window. Not copied - the texture has to outlive playback, which
	 * is the case for the RTSUnitTemplate topography texture.
	 */
	UFUNCTION(BlueprintCallable, Category = "Replay|Recording")
	void SetBackgroundTexture(UTexture2D* InTexture);

	/** CPU-side terrain layer. Unlike SetBackgroundTexture this one survives into a save game. */
	UFUNCTION(BlueprintCallable, Category = "Replay|Recording")
	void SetBackgroundPixels(const TArray<FColor>& Pixels, int32 Size);

	/**
	 * The match is over: stop recording, publish, and (per settings) put the replay button up.
	 * Safe to call more than once - only the first call does anything.
	 */
	UFUNCTION(BlueprintCallable, Category = "Replay")
	void NotifyGameEnded();

	UFUNCTION(BlueprintPure, Category = "Replay")
	bool HasGameEnded() const { return bGameEnded; }

	// --- Feeding ---

	void RegisterMarker(UReplayMarkerComponent* Marker);
	void UnregisterMarker(UReplayMarkerComponent* Marker);

	/**
	 * Adds a marker to the frame currently being assembled. Only meaningful from inside a
	 * CaptureFrame pass, so Blueprints normally use UReplayMarkerComponent instead.
	 */
	UFUNCTION(BlueprintCallable, Category = "Replay|Recording")
	bool PushDot(FVector WorldLocation, uint8 ColorIndex, float WorldRadius, float SightRadius = 0.f);

private:
	/** Retries resolving map bounds until they exist or BootstrapTimeoutSeconds has passed. */
	void TryBootstrap();

	void CaptureFrame();

	/** Fills Dots from every source that applies to this project. */
	void GatherDots(TArray<FReplayDot>& OutDots) const;

	void GatherViewport(FReplayViewportQuad& OutQuad) const;

	/** Turns a world radius into a pixel radius at the recording's reference resolution. */
	uint8 QuantizeRadius(float WorldRadius) const;

	void StartTimers();
	void StopTimers();

	/** Polls for the end of the match when the project has no explicit notification. */
	void PollGameEnd();

	void ShowLauncherWidget();

	/** The recording being assembled. Moved into the storage subsystem on StopRecording. */
	TSharedPtr<FReplayRecording, ESPMode::ThreadSafe> WorkingRecording;

	/** Style handed in before a recording exists to hold it in. */
	FReplayStyle PendingStyle;

	/** Team id handed in before a recording exists, applied by StartRecording. */
	int32 PendingLocalTeamId = -1;

	UPROPERTY()
	TArray<TWeakObjectPtr<UReplayMarkerComponent>> Markers;

	UPROPERTY()
	TObjectPtr<UTexture2D> PendingBackgroundTexture = nullptr;

	/**
	 * Only valid for the duration of one CaptureFrame call - PushDot appends here so integrations
	 * and Blueprints can contribute without knowing about the frame layout.
	 */
	TArray<FReplayDot>* ActiveDotSink = nullptr;

	FTimerHandle CaptureTimerHandle;
	FTimerHandle BootstrapTimerHandle;
	FTimerHandle GameEndTimerHandle;

	/**
	 * Replay id per recorded actor, and the counter they come from. Lives for the whole recording so
	 * a unit keeps its id across every frame it appears in.
	 */
	TMap<FObjectKey, uint32> ActorIdsByObject;
	uint32 NextActorId = 0;

	/** Same mapping for work areas, counted separately so the two id spaces cannot collide. */
	TMap<FObjectKey, uint32> WorkAreaIdsByObject;
	uint32 NextWorkAreaId = 0;

	float RecordInterval = 1.f;
	float RecordStartTime = 0.f;
	float BootstrapDeadline = 0.f;

	bool bRecording = false;
	bool bGameEnded = false;
	bool bBootstrapFinished = false;
};
