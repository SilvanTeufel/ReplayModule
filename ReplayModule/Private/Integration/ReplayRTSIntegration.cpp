// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Integration/ReplayRTSIntegration.h"

#include "ReplayModule.h"

#if WITH_RTSUNITTEMPLATE

#include "Actors/EffectArea.h"
#include "Actors/WorkArea.h"
#include "Actors/MinimapActor.h"
#include "Characters/Unit/BuildingBase.h"
#include "Characters/Unit/UnitBase.h"
#include "Components/CapsuleComponent.h"
#include "Controller/PlayerController/ControllerBase.h"
#include "Core/UnitData.h"
#include "GAS/AttributeSetBase.h"
#include "Animations/UnitBaseAnimInstance.h"
#include "Animations/UnitAnimationProcessor.h"
#include "Engine/DataTable.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Mass/MassActorBindingComponent.h"
// Must be pulled in before the projectile query below: the engine static_asserts that a fragment
// is trivially copyable, and FMassProjectileFragment opts out of that through the
// TMassFragmentTraits specialisation declared in this header. Without it the query fails to
// compile even though the fragment is perfectly usable.
#include "Mass/MassFragmentTraitsOverrides.h"
#include "MassCommonFragments.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassExecutionContext.h"
#include "Mass/UnitMassTag.h"
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

	// Buildings used to be brightened by +90 here so bases would "read as bases". The live minimap does
	// no such thing - AMinimapActor picks one color per relation and draws units and buildings with it -
	// so the replay came out in a palette the player had never seen. Same color, no exception.
	OutStyle.Palette[static_cast<int32>(EReplayMarkerCategory::FriendlyBuilding)] = Minimap->FriendlyUnitColor;
	OutStyle.Palette[static_cast<int32>(EReplayMarkerCategory::AlliedBuilding)] = Minimap->AlliedUnitColor;
	OutStyle.Palette[static_cast<int32>(EReplayMarkerCategory::EnemyBuilding)] = Minimap->EnemyUnitColor;

	// The recorder files effect areas under the friendly/allied/enemy slots, exactly as the minimap
	// colors them. This slot is only left for projects pushing their own dots through PushDot.
	OutStyle.Palette[static_cast<int32>(EReplayMarkerCategory::EffectArea)] = Minimap->EnemyUnitColor;
	OutStyle.Palette[static_cast<int32>(EReplayMarkerCategory::PointOfInterest)] = Minimap->MapSwitcherColor;

	// The topography texture is generated on a delay, so it may still be missing here. That only
	// costs the replay its terrain layer, never the recording itself.
	OutBackgroundTexture = Minimap->GetTopographyTexture();

	return true;
}

bool ReplayRTS::TryFetchBackgroundPixels(const UWorld* World, TArray<FColor>& OutPixels, int32& OutSize)
{
	OutSize = 0;

	if (!World)
	{
		return false;
	}

	AMinimapActor* Minimap = FindMinimapActor(World, GetLocalTeamId(World));
	if (!Minimap)
	{
		return false;
	}

	const int32 Size = Minimap->GetTopographyPixelSize();
	if (Size <= 0)
	{
		return false;   // topography not captured yet - normal for the first seconds
	}

	const TArray<FColor>& Terrain = Minimap->GetTopographyPixels();
	if (Terrain.Num() != Size * Size)
	{
		return false;
	}

	OutPixels = Terrain;
	OutSize = Size;
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

	// Effect areas. The live minimap draws them alongside the units, in the same relation color and
	// with no sight radius of their own; leaving them out was the other half of "the replay does not
	// look like the minimap". They never open the fog, so no SightPixelRadius here.
	for (TActorIterator<AEffectArea> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (OutDots.Num() >= MaxDots)
		{
			return;
		}

		AEffectArea* Area = *It;
		if (!IsValid(Area))
		{
			continue;
		}

		const bool bIsFriendly = (Area->TeamId == LocalTeamId);
		const bool bIsAllied = !bIsFriendly && LocalTeamId >= 0 && (AllianceMask & (1LL << Area->TeamId)) != 0;

		if (!bIsFriendly && !bIsAllied && !bRevealAll && !Area->bIsVisibleByFog)
		{
			continue;
		}

		FReplayDot Dot;
		if (!Recording.WorldToNormalized(Area->GetActorLocation(), Dot.X, Dot.Y))
		{
			continue;
		}

		const EReplayMarkerCategory Category = bIsFriendly
			? EReplayMarkerCategory::FriendlyUnit
			: (bIsAllied ? EReplayMarkerCategory::AlliedUnit : EReplayMarkerCategory::EnemyUnit);

		Dot.ColorIndex = static_cast<uint8>(Category);
		Dot.PixelRadius = QuantizeRadius(Area->BaseRadius, WorldExtentX, Recording.ReferenceTextureSize);

		OutDots.Add(Dot);
	}
}

void ReplayRTS::GatherUnitStates(
	const UWorld* World,
	FReplayRecording& Recording,
	int32 MaxStates,
	TMap<FObjectKey, uint32>& ActorIds,
	uint32& NextActorId,
	TArray<FReplayActorState>& OutStates)
{
	if (!World)
	{
		return;
	}

	for (TActorIterator<AUnitBase> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (OutStates.Num() >= MaxStates)
		{
			return;
		}

		AUnitBase* Unit = *It;
		if (!IsValid(Unit))
		{
			continue;
		}

		// Dying units ARE recorded, unlike on the minimap.
		//
		// Skipping them made every death an instant vanishing: a unit fighting in one frame was
		// simply absent in the next. The game plays a death animation, and the anim blueprint has a
		// Dead state for exactly that, so the state is recorded and the proxy plays it out. The
		// corpse disappears when the game destroys the actor, which is when it stops appearing in
		// the frames - the same moment it goes away in the match.
		//
		// Cost is small: a dead unit is recorded for its despawn time only.

		const int32 ClassIndex = Recording.FindOrAddClass(FSoftClassPath(Unit->GetClass()));
		if (ClassIndex == INDEX_NONE)
		{
			continue;
		}

		FReplayActorState State;

		// Ids come from our own counter, keyed by FObjectKey.
		//
		// GetUniqueID looks like the obvious choice and is wrong: it is the actor's slot in the global
		// object array, and that slot is recycled once the actor is destroyed and collected. In an RTS
		// units die constantly, so a later unit inherits a dead one's id - and the replay then hands
		// that unit the dead one's proxy, complete with the wrong mesh, teleporting across the map.
		// FObjectKey carries the serial number alongside the index, so a recycled slot is a new key.
		const FObjectKey Key(Unit);
		if (const uint32* Known = ActorIds.Find(Key))
		{
			State.ActorId = *Known;
		}
		else
		{
			State.ActorId = ++NextActorId;
			ActorIds.Add(Key, State.ActorId);
		}
		State.ClassIndex = static_cast<uint16>(ClassIndex);
		State.Location = FVector3f(Unit->GetActorLocation());
		State.SetYawDegrees(Unit->GetActorRotation().Yaw);
		State.StateIndex = static_cast<uint8>(Unit->GetUnitState());

		// The animation state is read off the live anim instance rather than derived from the unit
		// state, because the two genuinely differ: the anim instance downgrades Run/Chase/Patrol to
		// Idle whenever the unit is not actually moving. Recomputing that here would mean duplicating
		// its rules and drifting out of sync with them.
		State.AnimStateIndex = State.StateIndex;
		if (const USkeletalMeshComponent* Mesh = Unit->GetMesh())
		{
			if (const UUnitBaseAnimInstance* Anim = Cast<UUnitBaseAnimInstance>(Mesh->GetAnimInstance()))
			{
				State.AnimStateIndex = static_cast<uint8>(Anim->CharAnimState.GetValue());
				State.SetSpeed(Anim->MassSpeed);
			}
		}

		// Animation values come from the Mass fragment, not from the anim instance.
		//
		// The instance only receives them while UnitBase->IsOnViewport holds, so anything off-camera
		// carries stale numbers - that is what made a whole army replay standing still. The fragment is
		// updated for every unit regardless of where the camera points.
		if (Unit->MassActorBindingComponent)
		{
			const FMassEntityHandle Entity = Unit->MassActorBindingComponent->GetEntityHandle();
			if (Entity.IsValid())
			{
				if (UMassEntitySubsystem* EntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>())
				{
					const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
					if (EntityManager.IsEntityValid(Entity))
					{
						if (const FUnitAnimationFragment* AnimFrag = EntityManager.GetFragmentDataPtr<FUnitAnimationFragment>(Entity))
						{
							State.SetBlendPoints(AnimFrag->CurrentBlendPoint_1, AnimFrag->CurrentBlendPoint_2);
							State.ContinuousPlayRate = AnimFrag->PlayRate;
							State.ContinuousAnimationPosition = AnimFrag->AnimationPosition;
						}
					}
				}
			}
		}
		State.TeamId = static_cast<int8>(FMath::Clamp(Unit->TeamId, -128, 127));

		// Health lives on the GAS attribute set, not on the unit - and Attributes is null for a short
		// window after spawn, so it has to be checked rather than assumed.
		if (Unit->Attributes)
		{
			const float MaxHealth = Unit->Attributes->GetMaxHealth();
			const float Health = Unit->Attributes->GetHealth();
			State.HealthPercent = (MaxHealth > KINDA_SMALL_NUMBER)
				? static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Health / MaxHealth * 255.f), 0, 255))
				: 255;
		}

		if (Unit->IsA(ABuildingBase::StaticClass()))
		{
			State.Flags |= 0x1;
		}

		OutStates.Add(State);
	}
}

void ReplayRTS::GatherWorkAreas(
	const UWorld* World,
	FReplayRecording& Recording,
	int32 MaxAreas,
	TMap<FObjectKey, uint32>& AreaIds,
	uint32& NextAreaId,
	TArray<FReplayWorkAreaState>& OutStates)
{
	if (!World)
	{
		return;
	}

	for (TActorIterator<AWorkArea> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (OutStates.Num() >= MaxAreas)
		{
			return;
		}

		AWorkArea* Area = *It;
		if (!IsValid(Area) || Area->IsHidden())
		{
			continue;
		}

		const int32 ClassIndex = Recording.FindOrAddClass(FSoftClassPath(Area->GetClass()));
		if (ClassIndex == INDEX_NONE)
		{
			continue;
		}

		FReplayWorkAreaState State;

		// Same reasoning as for units: FObjectKey rather than GetUniqueID, because an object index is
		// recycled once the actor is gone and a later area would inherit an earlier one's identity.
		const FObjectKey Key(Area);
		if (const uint32* Known = AreaIds.Find(Key))
		{
			State.AreaId = *Known;
		}
		else
		{
			State.AreaId = ++NextAreaId;
			AreaIds.Add(Key, State.AreaId);
		}

		State.ClassIndex = static_cast<uint16>(ClassIndex);
		State.Location = FVector3f(Area->GetActorLocation());
		State.SetYawDegrees(Area->GetActorRotation().Yaw);
		State.AreaType = static_cast<uint8>(Area->Type.GetValue());

		// Scale is recorded per frame rather than taken from the class: resource nodes shrink as they
		// are mined, so using the default would show full deposits all the way through the replay.
		State.SetScale(Area->GetActorScale3D().X);

		OutStates.Add(State);
	}
}

void ReplayRTS::GatherProjectiles(
	const UWorld* World,
	FReplayRecording& Recording,
	int32 MaxProjectiles,
	TArray<FReplayProjectileState>& OutStates)
{
	if (!World)
	{
		return;
	}

	UMassEntitySubsystem* EntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	if (!EntitySubsystem)
	{
		return;
	}

	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();

	FMassEntityQuery Query(EntityManager.AsShared());
	Query.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FMassProjectileFragment>(EMassFragmentAccess::ReadOnly);

	FMassExecutionContext Context(EntityManager);
	Query.ForEachEntityChunk(Context, [&Recording, &OutStates, MaxProjectiles](FMassExecutionContext& ChunkContext)
	{
		const TConstArrayView<FTransformFragment> Transforms = ChunkContext.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FMassProjectileFragment> Projectiles = ChunkContext.GetFragmentView<FMassProjectileFragment>();

		for (int32 i = 0; i < ChunkContext.GetNumEntities(); ++i)
		{
			if (OutStates.Num() >= MaxProjectiles)
			{
				return;
			}

			const FMassProjectileFragment& Projectile = Projectiles[i];
			if (!Projectile.ProjectileClass)
			{
				continue;
			}

			const int32 ClassIndex = Recording.FindOrAddClass(FSoftClassPath(Projectile.ProjectileClass));
			if (ClassIndex == INDEX_NONE)
			{
				continue;
			}

			const FTransform& Transform = Transforms[i].GetTransform();

			FReplayProjectileState State;

			// A Mass entity handle is index plus serial number, and unlike an actor's object index the
			// serial number is bumped on reuse - so this is stable for the shot's lifetime and never
			// silently inherited by the next one.
			const FMassEntityHandle Entity = ChunkContext.GetEntity(i);
			State.ProjectileId = static_cast<uint32>(Entity.Index) ^ (static_cast<uint32>(Entity.SerialNumber) << 16);

			State.ClassIndex = static_cast<uint16>(ClassIndex);
			State.Location = FVector3f(Transform.GetLocation());

			const FRotator Rotation = Transform.Rotator();
			State.SetRotation(Rotation.Yaw, Rotation.Pitch);

			OutStates.Add(State);
		}
	});
}

bool ReplayRTS::ApplyAnimState(USkeletalMeshComponent* Mesh, uint8 AnimStateIndex, float BlendPoint1, float BlendPoint2, float Speed,
	float ContinuousPlayRate, float ContinuousPosition)
{
	if (!Mesh)
	{
		return false;
	}

	UUnitBaseAnimInstance* Anim = Cast<UUnitBaseAnimInstance>(Mesh->GetAnimInstance());
	if (!Anim)
	{
		return false;
	}

	const TEnumAsByte<UnitData::EState> NewState = static_cast<UnitData::EState>(AnimStateIndex);

	// LastAnimState is what the anim instance uses to notice a change (and retrigger its sound), so
	// it has to move with CharAnimState - otherwise the first transition after playback starts is
	// swallowed and the unit keeps the pose it was spawned in.
	if (Anim->CharAnimState != NewState)
	{
		Anim->LastAnimState = Anim->CharAnimState;
	}

	Anim->CharAnimState = NewState;

	// Blend points are looked up from the anim data table by state, not taken from the recording.
	//
	// Recording them off the live anim instance looked right and was not: NativeUpdateAnimation only
	// copies those values while UnitBase->IsOnViewport is true, so every unit off-camera carried a
	// stale value - almost always 0. Zero is the resting corner of the blend space, which is why the
	// whole army replayed standing still no matter what its states said.
	//
	// This table is the same source the animation processor reads, so the pose matches the game by
	// construction rather than by luck.
	// Recorded values win - they come straight from the Mass fragment and are what the unit really
	// showed. The table is only consulted when the recording has nothing (older captures).
	float B1 = BlendPoint1;
	float B2 = BlendPoint2;

	if (FMath::IsNearlyZero(B1) && FMath::IsNearlyZero(B2))
	{
		if (const UDataTable* Table = Anim->AnimDataTable)
		{
			// Exact row, else the Idle row - mirroring FindAnimRowForStateOrIdle in the animation
			// processor. States like Rooted and ContinousAttack have no row of their own, and the game
			// falls back to Idle for them rather than keeping the previous state's values.
			const FUnitAnimData* Exact = nullptr;
			const FUnitAnimData* IdleRow = nullptr;

			for (const TPair<FName, uint8*>& Row : Table->GetRowMap())
			{
				const FUnitAnimData* Data = reinterpret_cast<const FUnitAnimData*>(Row.Value);
				if (!Data)
				{
					continue;
				}

				if (Data->AnimState == NewState)
				{
					Exact = Data;
					break;
				}

				if (Data->AnimState == UnitData::Idle)
				{
					IdleRow = Data;
				}
			}

			if (const FUnitAnimData* Use = Exact ? Exact : IdleRow)
			{
				B1 = Use->BlendPoint_1;
				B2 = Use->BlendPoint_2;
				Anim->TransitionRate_1 = Use->TransitionRate_1;
				Anim->TransitionRate_2 = Use->TransitionRate_2;
				Anim->Resolution_1 = Use->Resolution_1;
				Anim->Resolution_2 = Use->Resolution_2;
			}
		}
	}

	Anim->CurrentBlendPoint_1 = B1;
	Anim->CurrentBlendPoint_2 = B2;
	Anim->BlendPoint_1 = B1;
	Anim->BlendPoint_2 = B2;

	// Sustained fire: ranged units drive their shooting animation off these two, and without them
	// they hold an idle pose while firing.
	Anim->ContinuousPlayRate = ContinuousPlayRate;
	Anim->ContinuousAnimationPosition = ContinuousPosition;

	// bMassSpeedValid has to come along: the anim blueprint treats the speed as meaningless without
	// it, and would fall back to a standing pose no matter what the state says.
	Anim->MassSpeed = Speed;
	Anim->bMassSpeedValid = true;

	return true;
}

uint8 ReplayRTS::ReadAnimState(USkeletalMeshComponent* Mesh)
{
	if (!Mesh)
	{
		return 255;
	}

	if (const UUnitBaseAnimInstance* Anim = Cast<UUnitBaseAnimInstance>(Mesh->GetAnimInstance()))
	{
		return static_cast<uint8>(Anim->CharAnimState.GetValue());
	}

	return 255;
}

bool ReplayRTS::ReadBlendPoints(USkeletalMeshComponent* Mesh, float& OutB1, float& OutB2,
	float& OutSpeed, bool& bOutSpeedValid)
{
	OutB1 = 0.f;
	OutB2 = 0.f;
	OutSpeed = 0.f;
	bOutSpeedValid = false;

	if (!Mesh)
	{
		return false;
	}

	if (const UUnitBaseAnimInstance* Anim = Cast<UUnitBaseAnimInstance>(Mesh->GetAnimInstance()))
	{
		OutB1 = Anim->CurrentBlendPoint_1;
		OutB2 = Anim->CurrentBlendPoint_2;
		OutSpeed = Anim->MassSpeed;
		bOutSpeedValid = Anim->bMassSpeedValid;
		return true;
	}

	return false;
}

FString ReplayRTS::GetStateName(uint8 StateIndex)
{
	if (const UEnum* Enum = StaticEnum<UnitData::EState>())
	{
		return Enum->GetNameStringByValue(static_cast<int64>(StateIndex));
	}

	return FString::FromInt(StateIndex);
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

bool ReplayRTS::TryFetchBackgroundPixels(const UWorld*, TArray<FColor>&, int32& OutSize)
{
	OutSize = 0;
	return false;
}

void ReplayRTS::GatherUnitDots(const UWorld*, const FReplayRecording&, EReplayVisibilityMode, int32, TArray<FReplayDot>&)
{
}

void ReplayRTS::GatherUnitStates(const UWorld*, FReplayRecording&, int32, TMap<FObjectKey, uint32>&, uint32&, TArray<FReplayActorState>&)
{
}

void ReplayRTS::GatherWorkAreas(const UWorld*, FReplayRecording&, int32, TMap<FObjectKey, uint32>&, uint32&, TArray<FReplayWorkAreaState>&)
{
}

void ReplayRTS::GatherProjectiles(const UWorld*, FReplayRecording&, int32, TArray<FReplayProjectileState>&)
{
}

bool ReplayRTS::ApplyAnimState(USkeletalMeshComponent*, uint8, float, float, float, float, float)
{
	return false;
}

uint8 ReplayRTS::ReadAnimState(USkeletalMeshComponent*)
{
	return 255;
}

bool ReplayRTS::ReadBlendPoints(USkeletalMeshComponent*, float& OutB1, float& OutB2, float& OutSpeed, bool& bOutSpeedValid)
{
	OutB1 = 0.f;
	OutB2 = 0.f;
	OutSpeed = 0.f;
	bOutSpeedValid = false;
	return false;
}

FString ReplayRTS::GetStateName(uint8 StateIndex)
{
	return FString::FromInt(StateIndex);
}

bool ReplayRTS::IsMatchOverScreenVisible(const UWorld*)
{
	return false;
}

#endif // WITH_RTSUNITTEMPLATE
