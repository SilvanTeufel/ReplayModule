// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/ReplayFrameData.h"
#include "ReplayMarkerComponent.generated.h"

/**
 * Put this on any actor that should show up in the replay.
 *
 * This is the generic path - a project without RTSUnitTemplate needs nothing else to get a full
 * recording. The component registers itself with the world's recorder in BeginPlay and unregisters
 * in EndPlay, so units that die simply stop appearing in later frames.
 */
UCLASS(ClassGroup = (Replay), meta = (BlueprintSpawnableComponent), HideCategories = (ComponentTick, Collision, Activation, Cooking, AssetUserData))
class REPLAYMODULE_API UReplayMarkerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UReplayMarkerComponent();

	/** Palette slot this marker draws with. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
	EReplayMarkerCategory Category = EReplayMarkerCategory::NeutralUnit;

	/**
	 * Overrides Category when >= 0. Lets a project address palette entries it appended itself
	 * without extending the enum.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay", meta = (ClampMin = "-1", ClampMax = "255"))
	int32 CustomColorIndex = -1;

	/** Radius of the dot in world units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay", meta = (ClampMin = "1.0"))
	float WorldRadius = 150.f;

	/** Radius this marker clears the replay's fog with. 0 leaves the fog closed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay", meta = (ClampMin = "0.0"))
	float SightRadius = 0.f;

	/** Temporarily drop out of the recording without destroying the component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
	bool bRecordable = true;

	/** Resolved palette index, honouring CustomColorIndex. */
	uint8 GetColorIndex() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};
