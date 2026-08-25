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

/**
 * One actor in one frame - the 3D counterpart to FReplayDot.
 *
 * FReplayDot carries a position and a colour and nothing else, which is all a minimap needs but
 * far too little to rebuild a scene: no identity (so a dot cannot be followed across frames), no
 * orientation, no height, no idea what was standing there. This struct adds exactly what a viewport
 * playback has to have, and stays small enough that a long match still fits in memory.
 *
 * Kept as a separate array next to Dots rather than replacing them - the minimap replay keeps
 * reading Dots and is unaffected by anything here.
 */
USTRUCT()
struct REPLAYMODULE_API FReplayActorState
{
	GENERATED_BODY()

	/** Stable across frames, so a unit can be matched to its proxy and followed. */
	UPROPERTY()
	uint32 ActorId = 0;

	/** Index into FReplayRecording::ClassTable. */
	UPROPERTY()
	uint16 ClassIndex = 0;

	/** World position. Full precision - quantizing this is what makes replays look jittery. */
	UPROPERTY()
	FVector3f Location = FVector3f::ZeroVector;

	/** Yaw only; RTS units do not meaningfully pitch or roll. Degrees * 100, so 0.01 deg resolution. */
	UPROPERTY()
	int16 YawCentiDegrees = 0;

	/** The unit's logical state at that moment, cast from UnitData::EState. */
	UPROPERTY()
	uint8 StateIndex = 0;

	/**
	 * What the animation blueprint was actually showing - which is NOT the same as StateIndex.
	 *
	 * The live anim instance starts from the unit state but then corrects it: a unit whose state
	 * says Run/Chase/Patrol while it is standing still gets reported as Idle, so it does not run on
	 * the spot. Replaying StateIndex therefore produces running units standing still - exactly the
	 * mismatch this field exists to avoid.
	 */
	UPROPERTY()
	uint8 AnimStateIndex = 0;

	/**
	 * Movement speed the anim blueprint saw, in tenths of a unit per second.
	 *
	 * Blend spaces for walking and running are driven by this, not by the state: a unit can sit in
	 * Run with a speed of zero and the blend space will still show it standing. Without this the
	 * proxies looked frozen even though their states were correct.
	 */
	UPROPERTY()
	uint16 SpeedDeciUnits = 0;

	/**
	 * The anim blueprint's two blend inputs.
	 *
	 * Full floats, not quantized bytes. They were int8 over -1..1 on the assumption that a blend
	 * point is a normalized value; measured against the live game they run up to 75, so every value
	 * above 1.27 was being clipped and the blend space never left its resting corner.
	 */
	UPROPERTY()
	float BlendPoint1 = 0.f;

	UPROPERTY()
	float BlendPoint2 = 0.f;

	/**
	 * Play rate and position of the continuous animation - what drives sustained fire on ranged
	 * units. The anim instance takes both from the Mass fragment; without them a ranged unit holds
	 * its idle pose while shooting.
	 */
	UPROPERTY()
	float ContinuousPlayRate = 1.f;

	UPROPERTY()
	float ContinuousAnimationPosition = 0.f;

	/** 0..255 mapped over 0..100 % so the health bar can be reproduced. */
	UPROPERTY()
	uint8 HealthPercent = 255;

	UPROPERTY()
	int8 TeamId = -1;

	/** Bit 0: is a building. Bit 1: was selectable by the recording player. Rest reserved. */
	UPROPERTY()
	uint8 Flags = 0;

	FReplayActorState() = default;

	float GetYawDegrees() const { return static_cast<float>(YawCentiDegrees) * 0.01f; }
	float GetBlendPoint1() const { return BlendPoint1; }
	float GetBlendPoint2() const { return BlendPoint2; }
	void SetBlendPoints(float B1, float B2);
	float GetSpeed() const { return static_cast<float>(SpeedDeciUnits) * 0.1f; }
	void SetSpeed(float Speed);
	void SetYawDegrees(float Yaw);

	bool IsBuilding() const { return (Flags & 0x1) != 0; }
};

/**
 * One projectile in flight, in one frame.
 *
 * Separate from FReplayActorState because projectiles are a different kind of thing to replay:
 * they have no health, no team-coloured selection, no animation - but they do need pitch, which
 * units do not, since a shot arcs and a unit does not.
 */
USTRUCT()
struct REPLAYMODULE_API FReplayProjectileState
{
	GENERATED_BODY()

	/** Stable across frames so a shot can be interpolated along its path instead of blinking. */
	UPROPERTY()
	uint32 ProjectileId = 0;

	/** Index into FReplayRecording::ClassTable, shared with the unit states. */
	UPROPERTY()
	uint16 ClassIndex = 0;

	UPROPERTY()
	FVector3f Location = FVector3f::ZeroVector;

	UPROPERTY()
	int16 YawCentiDegrees = 0;

	UPROPERTY()
	int16 PitchCentiDegrees = 0;

	FReplayProjectileState() = default;

	float GetYawDegrees() const { return static_cast<float>(YawCentiDegrees) * 0.01f; }
	float GetPitchDegrees() const { return static_cast<float>(PitchCentiDegrees) * 0.01f; }
	void SetRotation(float Yaw, float Pitch);
};

/**
 * One work area in one frame - resource nodes, build sites, base markers.
 *
 * Separate from FReplayActorState because a work area is not a unit: it has no team colour, no
 * animation and no health, but it does have a scale that changes over the match. Resource nodes
 * shrink as they are mined (AWorkArea::ShrinkResource), so replaying them at their original size
 * would show a full deposit where the match had a nearly exhausted one.
 */
USTRUCT()
struct REPLAYMODULE_API FReplayWorkAreaState
{
	GENERATED_BODY()

	UPROPERTY()
	uint32 AreaId = 0;

	/** Index into FReplayRecording::ClassTable, shared with units and projectiles. */
	UPROPERTY()
	uint16 ClassIndex = 0;

	UPROPERTY()
	FVector3f Location = FVector3f::ZeroVector;

	UPROPERTY()
	int16 YawCentiDegrees = 0;

	/** Uniform scale in percent of 1.0, so a mined-down node keeps its reduced size. */
	UPROPERTY()
	uint8 ScalePercent = 100;

	/** WorkAreaData::WorkAreaType, kept for tooling and future colouring. */
	UPROPERTY()
	uint8 AreaType = 0;

	FReplayWorkAreaState() = default;

	float GetYawDegrees() const { return static_cast<float>(YawCentiDegrees) * 0.01f; }
	float GetScale() const { return static_cast<float>(ScalePercent) * 0.01f; }
	void SetYawDegrees(float Yaw);
	void SetScale(float Scale);
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

	/** Empty on recordings made before the viewport playback existed, and on projects that disable it. */
	UPROPERTY()
	TArray<FReplayActorState> Actors;

	/** Projectiles in flight at this moment. */
	UPROPERTY()
	TArray<FReplayProjectileState> Projectiles;

	/** Resource nodes, build sites and base markers as they stood at this moment. */
	UPROPERTY()
	TArray<FReplayWorkAreaState> WorkAreas;

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

	/** False for older recordings that only hold minimap dots - the viewport playback is then unavailable. */
	UPROPERTY(BlueprintReadOnly, Category = "Replay")
	bool bHasViewportData = false;
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
	 * CPU-side terrain layer, BGRA, BackgroundSize x BackgroundSize.
	 *
	 * The RTS integration used to skip this and point the widget at the minimap's live topography
	 * texture instead. That texture is generated on a delay and did not exist yet when the recording
	 * bootstrapped, so the pointer was null and replays played on bare fog - and a transient texture
	 * could not have been written to a save game anyway. The recorder now copies the pixels in as soon
	 * as the minimap has them (see ReplayRTS::TryFetchBackgroundPixels). Still optional: a project
	 * without a minimap simply leaves it empty.
	 */
	UPROPERTY()
	TArray<FColor> BackgroundPixels;

	UPROPERTY()
	int32 BackgroundSize = 0;

	/**
	 * Classes referenced by FReplayActorState::ClassIndex.
	 *
	 * Soft paths, because a recording is written to a save game and must not keep hard references
	 * alive - and because a replay may well be watched in a session that never loaded these classes.
	 */
	UPROPERTY()
	TArray<FSoftClassPath> ClassTable;

	/** True when at least one frame carries actor states, i.e. a viewport replay is possible. */
	bool HasActorStates() const;

	/** Index of the class in ClassTable, appending it when new. INDEX_NONE when the table is full. */
	int32 FindOrAddClass(const FSoftClassPath& ClassPath);

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
