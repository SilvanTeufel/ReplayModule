// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Integration/ReplayRTSIntegration.h"

#include "ReplayModule.h"

#if WITH_RTSUNITTEMPLATE

#include "Actors/MinimapActor.h"
#include "Characters/Unit/BuildingBase.h"
#include "Characters/Unit/UnitBase.h"
#include "Components/CapsuleComponent.h"
#include "Controller/PlayerController/ControllerBase.h"
#include "Core/UnitData.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Mass/MassActorBindingComponent.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/WinLoseWidget.h"

namespace
{
	/** Team of the player this machine is recording for. -1 when it cannot be determined yet. */
	int32 GetLocalTeamId(const UWorld* World)
	{
		if (!World)
		{
			return -1;
		}

		if (const AControllerBase* PC = Cast<AControllerBase>(World->GetFirstPlayerController()))
		{
			return PC->SelectableTeamId;
		}

		return -1;
	}

	/** Team mask of the local player, so allies get their own color like they do on the minimap. */
	int64 GetLocalAllianceMask(const UWorld* World, int32 LocalTeamId)
	{
		if (World)
		{
			if (const AControllerBase* PC = Cast<AControllerBase>(World->GetFirstPlayerController()))
			{
				if (PC->AlliedTeamsMask != 0)
				{
					return PC->AlliedTeamsMask;
				}
			}
		}

		return LocalTeamId >= 0 ? (1LL << LocalTeamId) : 0;
	}

	/** Picks the minimap actor belonging to the local team, or any as a fallback. */
	AMinimapActor* FindMinimapActor(const UWorld* World, int32 LocalTeamId)
	{
		AMinimapActor* Fallback = nullptr;

		for (TActorIterator<AMinimapActor> It(const_cast<UWorld*>(World)); It; ++It)
		{
			AMinimapActor* Actor = *It;
			if (!Actor)
			{
				continue;
			}

			if (Actor->TeamId == LocalTeamId)
			{
				return Actor;
			}

			if (!Fallback)
			{
				Fallback = Actor;
			}
		}

		return Fallback;
	}

	uint8 QuantizeRadius(float WorldRadius, double WorldExtentX, int32 ReferenceTextureSize)
	{
		if (WorldRadius <= 0.f || WorldExtentX <= 0.0)
		{
			return 0;
		}

		const double Pixels = (WorldRadius / WorldExtentX) * static_cast<double>(ReferenceTextureSize);
		return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Pixels), 1, 255));
	}
}

bool ReplayRTS::IsAvailable()
{
	return true;
}

bool ReplayRTS::ResolveRecordingSetup(
	const UWorld* World,
	FVector2D& OutWorldMin,
	FVector2D& OutWorldMax,
	FReplayStyle& OutStyle,
	int32& OutLocalTeamId,
	UTexture2D*& OutBackgroundTexture)
{
	if (!World)
	{
		return false;
	}

	const int32 LocalTeamId = GetLocalTeamId(World);
	AMinimapActor* Minimap = FindMinimapActor(World, LocalTeamId);
	if (!Minimap)
	{
		return false;
	}

	// Replicated bounds arrive a moment after the actor itself; recording against a zero-sized map
	// would quantize every unit onto the same pixel.
	if (FMath::IsNearlyEqual(Minimap->MinimapMaxBounds.X, Minimap->MinimapMinBounds.X)
		|| FMath::IsNearlyEqual(Minimap->MinimapMaxBounds.Y, Minimap->MinimapMinBounds.Y))
	{
		return false;
	}

	OutWorldMin = Minimap->MinimapMinBounds;
	OutWorldMax = Minimap->MinimapMaxBounds;
	OutLocalTeamId = LocalTeamId;

	// A style handed in from Blueprint may carry a shorter palette than the slots written below.
	const int32 RequiredPaletteSize = static_cast<int32>(EReplayMarkerCategory::Count);
	if (OutStyle.Palette.Num() < RequiredPaletteSize)
	{
		OutStyle.Palette.SetNum(RequiredPaletteSize);
	}

	// Take the look of the live minimap so the replay matches what the player stared at all match.
	OutStyle.FogColor = Minimap->FogColor;
	OutStyle.FogColor.A = 255;
	OutStyle.FogOpacity = Minimap->FogOpacity;
	OutStyle.RevealedColor = Minimap->BackgroundColor;
	OutStyle.bDrawDotOutline = Minimap->bDrawUnitOutline;
	OutStyle.DotOutlineColor = Minimap->UnitOutlineColor;
	OutStyle.DotOutlineThickness = Minimap->UnitOutlineThickness;
	OutStyle.bDrawViewport = Minimap->bDrawViewport;
	OutStyle.ViewportColor = Minimap->ViewportColor;
	OutStyle.ViewportThickness = Minimap->ViewportLineThickness;
	OutStyle.DotScale = FMath::Max(0.1f, Minimap->DotMultiplier);

	OutStyle.Palette[static_cast<int32>(EReplayMarkerCategory::FriendlyUnit)] = Minimap->FriendlyUnitColor;
	OutStyle.Palette[static_cast<int32>(EReplayMarkerCategory::AlliedUnit)] = Minimap->AlliedUnitColor;
	OutStyle.Palette[static_cast<int32>(EReplayMarkerCategory::EnemyUnit)] = Minimap->EnemyUnitColor;

	// Buildings share the team color but sit a step brighter so bases read as bases on playback.
	auto Brighten = [](const FColor& In)
	{
		return FColor(
			static_cast<uint8>(FMath::Min(255, In.R + 90)),
			static_cast<uint8>(FMath::Min(255, In.G + 90)),
			static_cast<uint8>(FMath::Min(255, In.B + 90)),
			In.A);
	};

	OutStyle.Palette[static_cast<int32>(EReplayMarkerCategory::FriendlyBuilding)] = Brighten(Minimap->FriendlyUnitColor);
	OutStyle.Palette[static_cast<int32>(EReplayMarkerCategory::AlliedBuilding)] = Brighten(Minimap->AlliedUnitColor);
	OutStyle.Palette[static_cast<int32>(EReplayMarkerCategory::EnemyBuilding)] = Brighten(Minimap->EnemyUnitColor);
	OutStyle.Palette[static_cast<int32>(EReplayMarkerCategory::PointOfInterest)] = Minimap->MapSwitcherColor;

	// The topography texture is generated on a delay, so it may still be missing here. That only
	// costs the replay its terrain layer, never the recording itself.
	OutBackgroundTexture = Minimap->GetTopographyTexture();

	return true;
}

void ReplayRTS::GatherUnitDots(
	const UWorld* World,
	const FReplayRecording& Recording,
	EReplayVisibilityMode VisibilityMode,
	int32 MaxDots,
	TArray<FReplayDot>& OutDots)
{
	if (!World)
	{
		return;
	}

	const double WorldExtentX = FMath::Max(1.0, Recording.WorldMax.X - Recording.WorldMin.X);
	const int32 LocalTeamId = Recording.LocalTeamId;
	const int64 AllianceMask = GetLocalAllianceMask(World, LocalTeamId);
	const bool bRevealAll = (VisibilityMode == EReplayVisibilityMode::RevealAll);

	for (TActorIterator<AUnitBase> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (OutDots.Num() >= MaxDots)
		{
			return;
		}

		AUnitBase* Unit = *It;
		if (!IsValid(Unit))
		{
			continue;
		}

		// A corpse still exists for its despawn time; a replay reads much better without them.
		if (Unit->GetUnitState() == UnitData::Dead)
		{
			continue;
		}

		const bool bIsFriendly = (Unit->TeamId == LocalTeamId);
		const bool bIsAllied = !bIsFriendly && LocalTeamId >= 0 && (AllianceMask & (1LL << Unit->TeamId)) != 0;

		if (!bIsFriendly && !bIsAllied && !bRevealAll && !Unit->IsVisibleEnemy)
		{
			continue;
		}

		const bool bIsBuilding = Unit->IsA(ABuildingBase::StaticClass());

		EReplayMarkerCategory Category;
		if (bIsFriendly)
		{
			Category = bIsBuilding ? EReplayMarkerCategory::FriendlyBuilding : EReplayMarkerCategory::FriendlyUnit;
		}
		else if (bIsAllied)
		{
			Category = bIsBuilding ? EReplayMarkerCategory::AlliedBuilding : EReplayMarkerCategory::AlliedUnit;
		}
		else
		{
			Category = bIsBuilding ? EReplayMarkerCategory::EnemyBuilding : EReplayMarkerCategory::EnemyUnit;
		}

		FReplayDot Dot;
		if (!Recording.WorldToNormalized(Unit->GetActorLocation(), Dot.X, Dot.Y))
		{
			continue;
		}

		float UnitWorldRadius = 150.f;
		if (const UCapsuleComponent* Capsule = Unit->GetCapsuleComponent())
		{
			UnitWorldRadius = Capsule->GetScaledCapsuleRadius() * 3.f;
		}

		Dot.ColorIndex = static_cast<uint8>(Category);
		Dot.PixelRadius = QuantizeRadius(UnitWorldRadius, WorldExtentX, Recording.ReferenceTextureSize);

		// Only what the recording player owns opens the fog, exactly like the live minimap.
		if (bIsFriendly || bIsAllied)
		{
			const float UnitSightRadius = Unit->MassActorBindingComponent
				? Unit->MassActorBindingComponent->SightRadius
				: 0.f;

			Dot.SightPixelRadius = QuantizeRadius(UnitSightRadius, WorldExtentX, Recording.ReferenceTextureSize);
		}

		OutDots.Add(Dot);
	}
}

bool ReplayRTS::IsMatchOverScreenVisible(const UWorld* World)
{
	if (!World)
	{
		return false;
	}

	// RTSUnitTemplate creates its win/lose widget from a Blueprint class the project configures, so
	// there is no delegate to bind to on the client. Spotting the widget in the viewport is the one
	// hook that works for both outcomes without touching that plugin.
	for (TObjectIterator<UWinLoseWidget> It; It; ++It)
	{
		UWinLoseWidget* Widget = *It;
		if (!IsValid(Widget) || Widget->HasAnyFlags(RF_ClassDefaultObject))
		{
			continue;
		}

		if (Widget->GetWorld() == World && Widget->IsInViewport())
		{
			return true;
		}
	}

	return false;
}

#else // WITH_RTSUNITTEMPLATE

bool ReplayRTS::IsAvailable()
{
	return false;
}

bool ReplayRTS::ResolveRecordingSetup(const UWorld*, FVector2D&, FVector2D&, FReplayStyle&, int32&, UTexture2D*&)
{
	return false;
}

void ReplayRTS::GatherUnitDots(const UWorld*, const FReplayRecording&, EReplayVisibilityMode, int32, TArray<FReplayDot>&)
{
}

bool ReplayRTS::IsMatchOverScreenVisible(const UWorld*)
{
	return false;
}

#endif // WITH_RTSUNITTEMPLATE
