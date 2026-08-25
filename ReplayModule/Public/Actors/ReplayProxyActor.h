// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ReplayProxyActor.generated.h"

class UCapsuleComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;

/**
 * Stands in for one recorded unit during viewport playback.
 *
 * Deliberately NOT the recorded class itself. Spawning a real AUnitBase would drag its whole
 * machinery along - BeginPlay, Mass registration, AI, movement, abilities - and a replay would then
 * be a second match running on top of the recording rather than a reproduction of it. The proxy
 * borrows only the appearance from the recorded class's default object (mesh, materials, scale,
 * animation blueprint) and takes its position straight from the frames.
 *
 * It carries collision on the visibility channel only, so it can be clicked but cannot be
 * commanded, walked into, or hit by anything.
 */
UCLASS()
class REPLAYMODULE_API AReplayProxyActor : public AActor
{
	GENERATED_BODY()

public:
	AReplayProxyActor();

	/**
	 * Copies mesh, materials, relative transform and (for skeletal meshes) the animation blueprint
	 * off the recorded class's CDO. Called once when the proxy is created.
	 */
	void BuildFromClass(UClass* RecordedClass);

	/** Places the proxy. Called every playback tick with the interpolated transform. */
	void ApplyState(const FVector& NewLocation, float YawDegrees);

	/** Places a projectile proxy, which unlike a unit also needs its pitch. */
	void ApplyProjectileState(const FVector& NewLocation, float YawDegrees, float PitchDegrees);

	/**
	 * Turns this proxy into a projectile: no click collision (a shot in flight is not something the
	 * viewer selects) and no capsule fitting, so the mesh keeps the size it was fired at.
	 */
	void MakeProjectile();

	/** Highlight when the viewer has this unit selected. */
	void SetSelected(bool bInSelected);

	/**
	 * Runs the animation at the playback rate: at 4x the unit has to walk four times as fast, or its
	 * legs crawl while it slides across the ground. 0 freezes the pose, which is what pause needs.
	 */
	void SetAnimationRate(float Rate);

	bool IsSelected() const { return bSelected; }

	/** True when this proxy actually got a mesh out of its recorded class. */
	bool HasVisibleMesh() const;

	/** The recording's ActorId this proxy stands for. */
	UPROPERTY()
	uint32 RecordedActorId = 0;

	/** Class index this proxy was built for, so a mismatch can be caught instead of drawn wrong. */
	UPROPERTY()
	int32 BuiltForClassIndex = INDEX_NONE;

	UPROPERTY()
	int32 TeamId = -1;

	/** 0..1, straight from the recorded state. */
	UPROPERTY()
	float HealthFraction = 1.f;

	/** Display name of the recorded class, for the selection readout. */
	UPROPERTY()
	FString DisplayName;

	UPROPERTY(VisibleAnywhere, Category = "Replay")
	TObjectPtr<UCapsuleComponent> ClickCapsule;

	UPROPERTY(VisibleAnywhere, Category = "Replay")
	TObjectPtr<USkeletalMeshComponent> SkeletalMesh;

	UPROPERTY(VisibleAnywhere, Category = "Replay")
	TObjectPtr<UStaticMeshComponent> StaticMesh;

private:
	/** Widens the click capsule to whatever the mesh actually occupies. */
	void FitCapsuleToMesh();

	bool bSelected = false;
	bool bIsProjectile = false;
};
