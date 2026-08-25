// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/ReplayFrameData.h"
#include "ReplayWidget.generated.h"

class UBorder;
class UButton;
class UImage;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UReplayFrameRenderer;
class USizeBox;
class USlider;
class UTextBlock;

/**
 * The replay window: a minimap-sized picture of the match plus a scrub slider and a speed control.
 *
 * Usable straight from C++ - if no Blueprint subclass supplies a widget tree, a plain layout is
 * built in Initialize. A Blueprint subclass that names its widgets the same way (MapImage,
 * TimeSlider, ...) takes over completely and can be styled however the project likes; the three
 * material slots below are the intended way to skin the picture, its frame and its backdrop.
 */
UCLASS(Blueprintable, BlueprintType)
class REPLAYMODULE_API UReplayWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// --- Styling ---

	/**
	 * Material for the replay picture. It receives the marker layer and the terrain layer as
	 * texture parameters, so the minimap material of an RTS project can be reused as is.
	 * Without a material the widget stacks the two textures as plain images, which already looks
	 * like the live minimap because revealed areas are transparent.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Style")
	TObjectPtr<UMaterialInterface> MapMaterial = nullptr;

	/** Optional decorative frame drawn over the picture. Never receives input. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Style")
	TObjectPtr<UMaterialInterface> FrameMaterial = nullptr;

	/** Optional material for the panel behind everything. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Style")
	TObjectPtr<UMaterialInterface> BackdropMaterial = nullptr;

	/** Used when no BackdropMaterial is set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Style")
	FLinearColor BackdropColor = FLinearColor(0.f, 0.f, 0.f, 0.75f);

	/** Parameter MapMaterial receives the marker layer through. Matches the RTS minimap material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Style")
	FName MarkerTextureParameterName = TEXT("MinimapTexture");

	/** Parameter MapMaterial receives the terrain layer through. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Style")
	FName BackgroundTextureParameterName = TEXT("TopographyTexture");

	/** Edge length of the picture in slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Style", meta = (ClampMin = "64.0"))
	float MapDisplaySize = 512.f;

	/**
	 * Rotation applied to the picture. -90 matches the orientation the RTS minimap is drawn with,
	 * so a replay lines up with what the player remembers.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Style")
	float MapRotationAngle = -90.f;

	// --- Playback ---

	/** Resolution the picture is rendered at. Falls back to the project setting when 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Playback", meta = (ClampMin = "0"))
	int32 RenderTextureSize = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Playback")
	bool bAutoPlayOnOpen = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Replay|Playback")
	float PlaybackSpeed = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Playback")
	bool bLoop = true;

	/** Speeds the speed button steps through. Falls back to the project setting when empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Playback")
	TArray<float> SpeedSteps;

	// --- API ---

	/** Pulls the recording from the storage subsystem. Called automatically on construct. */
	UFUNCTION(BlueprintCallable, Category = "Replay")
	bool LoadReplayFromStorage();

	UFUNCTION(BlueprintCallable, Category = "Replay")
	void PlayReplay();

	UFUNCTION(BlueprintCallable, Category = "Replay")
	void PauseReplay();

	UFUNCTION(BlueprintCallable, Category = "Replay")
	void ToggleReplayPlayback();

	UFUNCTION(BlueprintPure, Category = "Replay")
	bool IsReplayPlaying() const { return bPlaying; }

	UFUNCTION(BlueprintCallable, Category = "Replay")
	void SetReplaySpeed(float NewSpeed);

	/** Steps to the next entry in SpeedSteps and wraps around. */
	UFUNCTION(BlueprintCallable, Category = "Replay")
	void CycleReplaySpeed();

	/** Jumps to a position given as 0..1 of the whole recording. */
	UFUNCTION(BlueprintCallable, Category = "Replay")
	void SeekToNormalized(float Alpha);

	UFUNCTION(BlueprintCallable, Category = "Replay")
	void SeekToTime(float TimeSeconds);

	UFUNCTION(BlueprintPure, Category = "Replay")
	float GetPlaybackTime() const { return PlaybackTime; }

	UFUNCTION(BlueprintPure, Category = "Replay")
	FReplayInfo GetReplayInfo() const { return ReplayInfo; }

	/** Removes the window from the viewport. */
	UFUNCTION(BlueprintCallable, Category = "Replay")
	void CloseReplay();

	/** Saves the loaded recording under a generated slot name. Returns the name, empty on failure. */
	UFUNCTION(BlueprintCallable, Category = "Replay")
	FString SaveReplay();

	/** Hook for a Blueprint subclass that wants to refresh its own labels. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Replay")
	void OnReplayFrameChanged(int32 FrameIndex, float TimeSeconds);

protected:
	/**
	 * Shrinks the map and parks it in a corner, for when the viewport shows the actual replay and the
	 * minimap is a companion rather than the main picture. Position and size come from the settings.
	 */
	void ApplyMinimapOverlayLayout();

	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** Builds a usable layout when no Blueprint subclass provided one. */
	void BuildFallbackLayout();

	UFUNCTION()
	void HandlePlayPauseClicked();

	UFUNCTION()
	void HandleSpeedClicked();

	UFUNCTION()
	void HandleCloseClicked();

	UFUNCTION()
	void HandleSaveClicked();

	UFUNCTION()
	void HandleSliderValueChanged(float Value);

	UFUNCTION()
	void HandleSliderCaptureBegin();

	UFUNCTION()
	void HandleSliderCaptureEnd();

	/** Renders the frame for the current playback time when it is not the one on screen already. */
	void RefreshFrame(bool bForce = false);

	void RefreshLabels();

	void ApplyMaterials();

	// --- Bound widgets. All optional so a Blueprint subclass only names what it actually uses. ---

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Replay|Widgets")
	TObjectPtr<UBorder> RootBorder = nullptr;

	/** Terrain layer. Only used when no MapMaterial is set. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Replay|Widgets")
	TObjectPtr<UImage> BackgroundImage = nullptr;

	/** The replay picture itself. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Replay|Widgets")
	TObjectPtr<UImage> MapImage = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Replay|Widgets")
	TObjectPtr<UImage> FrameImage = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Replay|Widgets")
	TObjectPtr<USlider> TimeSlider = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Replay|Widgets")
	TObjectPtr<UButton> PlayPauseButton = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Replay|Widgets")
	TObjectPtr<UTextBlock> PlayPauseText = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Replay|Widgets")
	TObjectPtr<UButton> SpeedButton = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Replay|Widgets")
	TObjectPtr<UTextBlock> SpeedText = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Replay|Widgets")
	TObjectPtr<UButton> CloseButton = nullptr;

	/** Writes the current recording to a new slot so it survives the session. */
	UPROPERTY(BlueprintReadOnly, Category = "Replay|Widgets", meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> SaveButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Replay|Widgets", meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> SaveText = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Replay|Widgets")
	TObjectPtr<UTextBlock> TimeText = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Replay|Widgets")
	TObjectPtr<UTextBlock> TitleText = nullptr;

private:
	UPROPERTY()
	TObjectPtr<UReplayFrameRenderer> Renderer = nullptr;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> MapMaterialInstance = nullptr;

	TSharedPtr<const FReplayRecording, ESPMode::ThreadSafe> Recording;

	FReplayInfo ReplayInfo;

	float PlaybackTime = 0.f;
	int32 CurrentFrame = INDEX_NONE;

	bool bPlaying = false;

	/** True while the recording is also being played back in the viewport with unit proxies. */
	UPROPERTY(BlueprintReadOnly, Category = "Replay|Playback", meta = (AllowPrivateAccess = "true"))
	bool bViewportPlaybackActive = false;

	/** The box the map picture sits in, kept so the overlay layout can shrink it. */
	UPROPERTY()
	TObjectPtr<USizeBox> MapSizeBox = nullptr;

	/** Set while the slider writes into us, so its own callback is not mistaken for a user seek. */
	bool bUpdatingSlider = false;

	/** Playback state to restore once the user lets go of the slider handle. */
	bool bWasPlayingBeforeScrub = false;
};
