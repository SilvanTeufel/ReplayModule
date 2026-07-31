// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ReplayFrameData.generated.h"

/**
 * Slot a recorded dot takes in FReplayStyle::Palette.
 *
 * The renderer only ever reads FReplayDot::ColorIndex, so a project is free to append its own
 * entries behind Custom - the enum just names the slots the built-in RTS integration fills.
 */
UENUM(BlueprintType)
enum class EReplayMarkerCategory : uint8
{
	FriendlyUnit		UMETA(DisplayName = "Friendly Unit"),
	AlliedUnit			UMETA(DisplayName = "Allied Unit"),
	EnemyUnit			UMETA(DisplayName = "Enemy Unit"),
	FriendlyBuilding	UMETA(DisplayName = "Friendly Building"),
	AlliedBuilding		UMETA(DisplayName = "Allied Building"),
	EnemyBuilding		UMETA(DisplayName = "Enemy Building"),
	NeutralUnit			UMETA(DisplayName = "Neutral Unit"),
	EffectArea			UMETA(DisplayName = "Effect Area"),
	PointOfInterest		UMETA(DisplayName = "Point Of Interest"),
	Custom				UMETA(DisplayName = "Custom"),

	Count				UMETA(Hidden)
};

/** Which units end up in a frame. */
UENUM(BlueprintType)
enum class EReplayVisibilityMode : uint8
{
	/** Only what the recording player could actually see (fog rules apply). Matches the live minimap. */
	AsSeen				UMETA(DisplayName = "As Seen (fog of war)"),

	/** Everything the recording machine knows about. Useful for a host-side "director" replay. */
	RevealAll			UMETA(DisplayName = "Reveal All")
};

/**
 * One marker in one frame - deliberately 8 bytes so a long match stays in the low megabytes.
 *
 * X/Y are the position normalized across the recording's world bounds; the two radii are pixel
 * radii valid at FReplayRecording::ReferenceTextureSize and are rescaled at render time.
 */
USTRUCT()
struct REPLAYMODULE_API FReplayDot
{
	GENERATED_BODY()

	UPROPERTY()
	uint16 X = 0;

	UPROPERTY()
	uint16 Y = 0;

	/** Radius of the drawn dot. */
	UPROPERTY()
	uint8 PixelRadius = 1;

	/** Radius this marker clears the fog with. 0 = the marker does not reveal anything. */
	UPROPERTY()
	uint8 SightPixelRadius = 0;

	/** Index into FReplayStyle::Palette. */
	UPROPERTY()
	uint8 ColorIndex = 0;

	/** Reserved for per-dot flags so the struct keeps its 8 byte alignment. */
	UPROPERTY()
	uint8 Flags = 0;

	FReplayDot() = default;
};

/** The player's camera footprint at the time of a frame, normalized to the map bounds. */
USTRUCT()
struct REPLAYMODULE_API FReplayViewportQuad
{
	GENERATED_BODY()

	/** Four corners in 0..1 map space, in order TL, TR, BR, BL. Empty when bValid is false. */
	UPROPERTY()
	TArray<FVector2f> Corners;

	UPROPERTY()
	bool bValid = false;
};

/** One recorded moment. */
USTRUCT()
struct REPLAYMODULE_API FReplayFrame
{
	GENERATED_BODY()

	/** Seconds since the recording started. */
	UPROPERTY()
	float TimeSeconds = 0.f;

	UPROPERTY()
	TArray<FReplayDot> Dots;

	UPROPERTY()
	FReplayViewportQuad Viewport;
};

/**
 * Everything the renderer needs to reproduce the look of the live minimap. Captured once when the
 * recording starts so a replay keeps the colors the match was played with.
 */
USTRUCT(BlueprintType)
struct REPLAYMODULE_API FReplayStyle
{
	GENERATED_BODY()

	/** Unexplored area. Its alpha is what makes the terrain underneath disappear. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Style")
	FColor FogColor = FColor(26, 26, 26, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Style", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FogOpacity = 0.8f;

	/** Drawn where a marker with a sight radius clears the fog. Alpha 0 lets the background show. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Style")
	FColor RevealedColor = FColor(50, 60, 50, 0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Style")
	bool bDrawDotOutline = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Style", meta = (EditCondition = "bDrawDotOutline"))
	FColor DotOutlineColor = FColor(0, 0, 0, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Style", meta = (EditCondition = "bDrawDotOutline", ClampMin = "1", ClampMax = "5"))
	int32 DotOutlineThickness = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Style")
	bool bDrawViewport = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Style", meta = (EditCondition = "bDrawViewport"))
	FColor ViewportColor = FColor(255, 255, 255, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Style", meta = (EditCondition = "bDrawViewport", ClampMin = "1", ClampMax = "5"))
	int32 ViewportThickness = 1;

	/** Multiplies every dot radius at render time. Bump it when replaying at a small widget size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Style", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float DotScale = 1.f;

	/** One color per EReplayMarkerCategory slot; extra entries are free for project use. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Style")
	TArray<FColor> Palette;

	FReplayStyle();

	/** Palette lookup that never goes out of range. */
	FColor GetColor(uint8 ColorIndex) const;

	/** Fog color with FogOpacity folded into its alpha. */
	FColor GetEffectiveFogColor() const;
};

/** Blueprint-facing summary of a recording; the frame data itself is far too large to copy around. */
USTRUCT(BlueprintType)
struct REPLAYMODULE_API FReplayInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Replay")
	FString MapName;

	UPROPERTY(BlueprintReadOnly, Category = "Replay")
	FDateTime RecordedAtUtc = FDateTime();

	UPROPERTY(BlueprintReadOnly, Category = "Replay")
	int32 FrameCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Replay")
	float DurationSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Replay")
	float IntervalSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Replay")
	int32 LocalTeamId = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Replay")
	int32 ApproxMemoryKB = 0;
};

/**
 * A complete recorded match. Held by UReplayStorageSubsystem (which lives on the GameInstance, so
 * it survives the ServerTravel a win/lose screen usually triggers) and consumed by
 * UReplayFrameRenderer.
 */
USTRUCT()
struct REPLAYMODULE_API FReplayRecording
{
	GENERATED_BODY()

	UPROPERTY()
	FString MapName;

	UPROPERTY()
	FDateTime RecordedAtUtc = FDateTime();

	/** Team the recording was taken from; -1 when the project does not use teams. */
	UPROPERTY()
	int32 LocalTeamId = -1;

	/** Seconds between two frames as configured while recording. */
	UPROPERTY()
	float IntervalSeconds = 1.f;

	/** Texture size the stored pixel radii refer to. */
	UPROPERTY()
	int32 ReferenceTextureSize = 256;

	UPROPERTY()
	FVector2D WorldMin = FVector2D::ZeroVector;

	UPROPERTY()
	FVector2D WorldMax = FVector2D::ZeroVector;

	UPROPERTY()
	FReplayStyle Style;

	UPROPERTY()
	TArray<FReplayFrame> Frames;

	/**
	 * Optional CPU-side terrain layer, BGRA, BackgroundSize x BackgroundSize. Only filled when the
	 * project hands one in - the RTS integration instead points the widget at the live topography
	 * texture, which costs nothing but cannot be written to a save game.
	 */
	UPROPERTY()
	TArray<FColor> BackgroundPixels;

	UPROPERTY()
	int32 BackgroundSize = 0;

	/** A recording is usable once it has non-degenerate bounds and at least one frame. */
	bool IsValid() const;

	float GetDurationSeconds() const;

	/** Index of the last frame at or before TimeSeconds. INDEX_NONE while there are no frames. */
	int32 FindFrameIndexForTime(float TimeSeconds) const;

	int64 GetApproxMemoryBytes() const;

	FReplayInfo GetInfo() const;

	/** Maps a world position onto the recording's 0..65535 grid. Returns false when off the map. */
	bool WorldToNormalized(const FVector& WorldLocation, uint16& OutX, uint16& OutY) const;

	void Reset();
};
