// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/ReplayFrameData.h"

class UTexture2D;
class UWorld;

/**
 * Everything the recorder knows about RTSUnitTemplate lives behind these four functions.
 *
 * They are declared unconditionally and stubbed out when the plugin is not installed
 * (WITH_RTSUNITTEMPLATE == 0), which keeps the recorder itself free of preprocessor branches and
 * lets the exact same source tree build in a project that never heard of RTSUnitTemplate.
 *
 * Nothing here writes to RTSUnitTemplate - it only reads the minimap actor, the units and the
 * win/lose widget.
 */
namespace ReplayRTS
{
	/** True when this build was compiled against RTSUnitTemplate. */
	REPLAYMODULE_API bool IsAvailable();

	/**
	 * Reads the local player's minimap actor for the bounds, colors and terrain texture a recording
	 * should use. False while no minimap exists yet, which is normal during the first seconds of a
	 * match - the recorder keeps retrying.
	 */
	REPLAYMODULE_API bool ResolveRecordingSetup(
		const UWorld* World,
		FVector2D& OutWorldMin,
		FVector2D& OutWorldMax,
		FReplayStyle& OutStyle,
		int32& OutLocalTeamId,
		UTexture2D*& OutBackgroundTexture);

	/**
	 * Copies the minimap's terrain layer out as pixels, so the recording carries its own background.
	 *
	 * False until AMinimapActor has actually captured its topography - that runs on a delay (DelayTime,
	 * 2 s by default) and is therefore never ready at bootstrap time, which is why grabbing the live
	 * texture once when the recording starts left every replay without a background. Call it until it
	 * returns true. Pixels rather than the texture, because a transient texture cannot be saved.
	 */
	REPLAYMODULE_API bool TryFetchBackgroundPixels(
		const UWorld* World,
		TArray<FColor>& OutPixels,
		int32& OutSize);

	/** Appends one dot per visible unit, building and effect area to OutDots. */
	REPLAYMODULE_API void GatherUnitDots(
		const UWorld* World,
		const FReplayRecording& Recording,
		EReplayVisibilityMode VisibilityMode,
		int32 MaxDots,
		TArray<FReplayDot>& OutDots);

	/**
	 * Appends one state per living unit and building to OutStates - the 3D counterpart to GatherUnitDots.
	 *
	 * Records everything regardless of fog: a replay is watched after the match, and a viewport that
	 * pops units in and out as the recording player's vision changed is unwatchable. Fog filtering
	 * stays where it belongs, on the minimap dots.
	 *
	 * Recording is the frame's recording (for the class table, which this call may extend).
	 */
	REPLAYMODULE_API void GatherUnitStates(
		const UWorld* World,
		FReplayRecording& Recording,
		int32 MaxStates,
		TMap<FObjectKey, uint32>& ActorIds,
		uint32& NextActorId,
		TArray<FReplayActorState>& OutStates);

	/**
	 * Appends one state per work area - resource nodes, build sites, base markers.
	 *
	 * They are plain actors, not units, so GatherUnitStates never saw them and replays came out with
	 * the terrain but none of the things the workers were actually working on.
	 */
	REPLAYMODULE_API void GatherWorkAreas(
		const UWorld* World,
		FReplayRecording& Recording,
		int32 MaxAreas,
		TMap<FObjectKey, uint32>& AreaIds,
		uint32& NextAreaId,
		TArray<FReplayWorkAreaState>& OutStates);

	/**
	 * Appends one state per projectile currently in flight.
	 *
	 * Projectiles are Mass entities, not actors - iterating actors finds nothing, which is why a
	 * replay showed no shots at all. This runs a Mass query instead.
	 */
	REPLAYMODULE_API void GatherProjectiles(
		const UWorld* World,
		FReplayRecording& Recording,
		int32 MaxProjectiles,
		TArray<FReplayProjectileState>& OutStates);

	/**
	 * Pushes a recorded animation state onto a replay proxy's mesh.
	 *
	 * The anim blueprint reads UUnitBaseAnimInstance::CharAnimState. On a proxy its own update pass
	 * bails out early (the owner is not an AUnitBase), so a value written here survives the frame -
	 * which is exactly what makes the proxy animate like the unit it stands for.
	 *
	 * False when the mesh has no RTSUnitTemplate anim instance, which is not an error: a project can
	 * perfectly well use its own.
	 */
	REPLAYMODULE_API bool ApplyAnimState(
		class USkeletalMeshComponent* Mesh,
		uint8 AnimStateIndex,
		float BlendPoint1,
		float BlendPoint2,
		float Speed,
		float ContinuousPlayRate,
		float ContinuousPosition);

	/**
	 * Reads back what the anim instance is currently showing. Only used for diagnostics - to tell
	 * "the value never arrived" apart from "the value arrived and the blueprint ignores it".
	 * Returns 255 when the mesh has no RTSUnitTemplate anim instance.
	 */
	REPLAYMODULE_API uint8 ReadAnimState(class USkeletalMeshComponent* Mesh);

	/** Reads back the two blend inputs, for the same diagnostic reason as ReadAnimState. */
	REPLAYMODULE_API bool ReadBlendPoints(class USkeletalMeshComponent* Mesh, float& OutB1, float& OutB2,
		float& OutSpeed, bool& bOutSpeedValid);

	/** Human-readable name of a UnitData::EState value, for logs. */
	REPLAYMODULE_API FString GetStateName(uint8 StateIndex);

	/** True once RTSUnitTemplate's win/lose screen is up, whichever way the match ended. */
	REPLAYMODULE_API bool IsMatchOverScreenVisible(const UWorld* World);
}
