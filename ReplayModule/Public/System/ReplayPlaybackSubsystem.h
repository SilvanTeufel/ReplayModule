// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Data/ReplayFrameData.h"
#include "ReplayPlaybackSubsystem.generated.h"

class AReplayProxyActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnReplayPlaybackStateChanged, bool, bIsActive);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnReplayProxySelected, AReplayProxyActor*, Proxy);

/**
 * Plays a recording back in the viewport, as opposed to UReplayWidget which draws it on a minimap.
 *
 * Spawns one AReplayProxyActor per recorded unit and moves it along the frames, interpolating
 * between the two frames around the current time so a one-second recording interval still looks
 * continuous. The live units are hidden for the duration rather than destroyed, so leaving the
 * replay puts the level back exactly as it was.
 *
 * Time is owned here, not in the widget: seeking, speed and pause all have to move the proxies, and
 * having two clocks that can disagree is how a replay ends up with the minimap and the viewport
 * showing different moments.
 */
UCLASS()
class REPLAYMODULE_API UReplayPlaybackSubsystem : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	static UReplayPlaybackSubsystem* Get(const UObject* WorldContextObject);

	// ~ USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;

	// ~ FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bActive; }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual UWorld* GetTickableGameObjectWorld() const override { return GetWorld(); }

	UPROPERTY(BlueprintAssignable, Category = "Replay|Playback")
	FOnReplayPlaybackStateChanged OnPlaybackStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Replay|Playback")
	FOnReplayProxySelected OnProxySelected;

	// --- Control ---

	/**
	 * Takes the recording out of the storage subsystem, hides the live units and spawns the proxies.
	 * False when there is no recording or it holds no actor states (an older, minimap-only capture).
	 */
	UFUNCTION(BlueprintCallable, Category = "Replay|Playback")
	bool BeginPlayback();

	/** Removes every proxy and unhides the live units. */
	UFUNCTION(BlueprintCallable, Category = "Replay|Playback")
	void EndPlayback();

	UFUNCTION(BlueprintPure, Category = "Replay|Playback")
	bool IsPlaybackActive() const { return bActive; }

	UFUNCTION(BlueprintCallable, Category = "Replay|Playback")
	void SetPaused(bool bInPaused);

	UFUNCTION(BlueprintPure, Category = "Replay|Playback")
	bool IsPaused() const { return bPaused; }

	/** Clamped to MaxPlaybackSpeed (6x by default) - beyond that the interpolation stops keeping up. */
	UFUNCTION(BlueprintCallable, Category = "Replay|Playback")
	void SetSpeed(float NewSpeed);

	UFUNCTION(BlueprintPure, Category = "Replay|Playback")
	float GetSpeed() const { return Speed; }

	/** Jumps to a point in time, forwards or backwards, and re-seats every proxy. */
	UFUNCTION(BlueprintCallable, Category = "Replay|Playback")
	void SeekToTime(float TimeSeconds);

	UFUNCTION(BlueprintPure, Category = "Replay|Playback")
	float GetPlaybackTime() const { return PlaybackTime; }

	UFUNCTION(BlueprintPure, Category = "Replay|Playback")
	float GetDurationSeconds() const;

	UFUNCTION(BlueprintPure, Category = "Replay|Playback")
	bool ShouldLoop() const { return bLoop; }

	UFUNCTION(BlueprintCallable, Category = "Replay|Playback")
	void SetLoop(bool bInLoop) { bLoop = bInLoop; }

	// --- Selection ---

	/** Traces under the cursor and selects whatever proxy is there. Never issues an order. */
	UFUNCTION(BlueprintCallable, Category = "Replay|Playback")
	AReplayProxyActor* SelectProxyUnderCursor();

	UFUNCTION(BlueprintCallable, Category = "Replay|Playback")
	void ClearSelection();

	/**
	 * Logs what the current frame says the units should be doing next to what their anim instances
	 * actually show. The two differing is the whole question when animations look wrong.
	 */
	UFUNCTION(BlueprintCallable, Category = "Replay|Playback")
	void LogAnimStateBreakdown();

	UFUNCTION(BlueprintPure, Category = "Replay|Playback")
	AReplayProxyActor* GetSelectedProxy() const { return SelectedProxy.Get(); }

private:
	/** Places every proxy at PlaybackTime, spawning and retiring as the frame's cast changes. */
	void ApplyTime(float TimeSeconds);

	/** Frame at or before the time, plus the blend factor towards the next one. */
	void ResolveFrames(float TimeSeconds, int32& OutFrameA, int32& OutFrameB, float& OutAlpha) const;

	AReplayProxyActor* AcquireProxy(const FReplayActorState& State);

	/** Same idea as AcquireProxy, for shots in flight. */
	AReplayProxyActor* AcquireProjectileProxy(const FReplayProjectileState& State);

	/** Places the projectiles of the current moment and retires the ones that have landed. */
	void ApplyProjectiles(const FReplayFrame& A, const FReplayFrame& B, float Alpha);

	/** Places the work areas of the current moment, including their shrinking scale. */
	void ApplyWorkAreas(const FReplayFrame& Frame);

	AReplayProxyActor* AcquireWorkAreaProxy(const FReplayWorkAreaState& State);

	/** Resolves a class table index to a loaded class, caching the answer (including a failure). */
	UClass* ResolveClass(uint16 ClassIndex);

	void ReleaseAllProxies();

	/** Pushes the current playback rate onto every proxy's animation. */
	void RefreshAnimationRates();

	/** Hides the live units so the replay is not played on top of the finished match. */
	void SetLiveUnitsHidden(bool bHidden);

	/** Polls the mouse button so a click selects a proxy - without touching the project's controller. */
	void PollSelectionInput();

	TSharedPtr<FReplayRecording, ESPMode::ThreadSafe> Recording;

	UPROPERTY()
	TMap<uint32, TObjectPtr<AReplayProxyActor>> Proxies;

	UPROPERTY()
	TMap<uint32, TObjectPtr<AReplayProxyActor>> ProjectileProxies;

	UPROPERTY()
	TMap<uint32, TObjectPtr<AReplayProxyActor>> WorkAreaProxies;

	UPROPERTY()
	TWeakObjectPtr<AReplayProxyActor> SelectedProxy;

	/** Live actors we hid on BeginPlayback, so EndPlayback can restore exactly those. */
	UPROPERTY()
	TArray<TWeakObjectPtr<AActor>> HiddenLiveActors;

	/** Resolved once per class index; a soft path lookup per unit per frame would be far too slow. */
	UPROPERTY()
	TMap<uint16, TObjectPtr<UClass>> ResolvedClasses;

	float PlaybackTime = 0.f;
	float Speed = 1.f;

	bool bActive = false;
	bool bPaused = false;
	bool bLoop = true;

	/** Edge detection for the select click. */
	bool bSelectKeyWasDown = false;
};
