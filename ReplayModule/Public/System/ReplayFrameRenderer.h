// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Data/ReplayFrameData.h"
#include "ReplayFrameRenderer.generated.h"

class UTexture2D;

/**
 * Turns one recorded frame into a texture, using the same passes the live minimap draws with:
 * fog, then the areas friendly markers reveal, then the markers themselves, then the camera
 * rectangle.
 *
 * The result is an RGBA texture whose revealed areas are transparent, so it can simply be laid over
 * a terrain image - which is what UReplayWidget does when no material is configured.
 */
UCLASS(BlueprintType)
class REPLAYMODULE_API UReplayFrameRenderer : public UObject
{
	GENERATED_BODY()

public:
	/** (Re)creates the target texture. Safe to call again with a different size. */
	UFUNCTION(BlueprintCallable, Category = "Replay|Render")
	void Configure(int32 InTextureSize);

	/** Holds a shared reference, so the recording cannot go away underneath the renderer. */
	void SetRecording(TSharedPtr<const FReplayRecording, ESPMode::ThreadSafe> InRecording);

	/** Draws a frame. Out of range indices are ignored. */
	UFUNCTION(BlueprintCallable, Category = "Replay|Render")
	void RenderFrame(int32 FrameIndex);

	/** Marker/fog layer. Null until Configure has run. */
	UFUNCTION(BlueprintPure, Category = "Replay|Render")
	UTexture2D* GetReplayTexture() const { return ReplayTexture; }

	/**
	 * Terrain layer built from the recording's stored pixels. Null when the recording carries none,
	 * in which case the widget falls back to the live texture on the storage subsystem.
	 */
	UFUNCTION(BlueprintPure, Category = "Replay|Render")
	UTexture2D* GetBackgroundTexture() const { return BackgroundTexture; }

	UFUNCTION(BlueprintPure, Category = "Replay|Render")
	int32 GetFrameCount() const;

	UFUNCTION(BlueprintPure, Category = "Replay|Render")
	int32 GetLastRenderedFrame() const { return LastRenderedFrame; }

private:
	void EnsureTexture();
	void BuildBackgroundTexture();

	/** Pushes a buffer to the GPU. It is copied first, so the caller may reuse it right away. */
	void UploadPixels(UTexture2D* Texture, const TArray<FColor>& InPixels) const;

	void DrawFilledCircle(int32 CenterX, int32 CenterY, int32 Radius, const FColor& Color);
	void DrawCircleOutline(int32 CenterX, int32 CenterY, int32 Radius, int32 Thickness, const FColor& Color);
	void DrawLine(int32 X0, int32 Y0, int32 X1, int32 Y1, int32 Thickness, const FColor& Color);

	TSharedPtr<const FReplayRecording, ESPMode::ThreadSafe> Recording;

	UPROPERTY()
	TObjectPtr<UTexture2D> ReplayTexture = nullptr;

	UPROPERTY()
	TObjectPtr<UTexture2D> BackgroundTexture = nullptr;

	/** Scratch buffer reused between frames so scrubbing does not allocate. */
	TArray<FColor> Pixels;

	int32 TextureSize = 0;
	int32 LastRenderedFrame = INDEX_NONE;
};
