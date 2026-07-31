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

	/** Appends one dot per visible unit, building and effect area to OutDots. */
	REPLAYMODULE_API void GatherUnitDots(
		const UWorld* World,
		const FReplayRecording& Recording,
		EReplayVisibilityMode VisibilityMode,
		int32 MaxDots,
		TArray<FReplayDot>& OutDots);

	/** True once RTSUnitTemplate's win/lose screen is up, whichever way the match ended. */
	REPLAYMODULE_API bool IsMatchOverScreenVisible(const UWorld* World);
}
