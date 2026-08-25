// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Actors/ReplayProxyActor.h"

#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Character.h"
#include "ReplayModule.h"

AReplayProxyActor::AReplayProxyActor()
{
	PrimaryActorTick.bCanEverTick = false;   // the playback subsystem drives us

	ClickCapsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("ClickCapsule"));
	SetRootComponent(ClickCapsule);
	ClickCapsule->InitCapsuleSize(42.f, 96.f);

	// Visibility only: clickable, but nothing can walk into it, shoot it, or path around it.
	ClickCapsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ClickCapsule->SetCollisionObjectType(ECC_WorldDynamic);
	ClickCapsule->SetCollisionResponseToAllChannels(ECR_Ignore);
	ClickCapsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	ClickCapsule->SetGenerateOverlapEvents(false);
	ClickCapsule->SetCanEverAffectNavigation(false);

	SkeletalMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("SkeletalMesh"));
	SkeletalMesh->SetupAttachment(ClickCapsule);
	SkeletalMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkeletalMesh->SetCanEverAffectNavigation(false);
	SkeletalMesh->bReceivesDecals = false;

	StaticMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMesh"));
	StaticMesh->SetupAttachment(ClickCapsule);
	StaticMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StaticMesh->SetCanEverAffectNavigation(false);
	StaticMesh->bReceivesDecals = false;
	StaticMesh->SetVisibility(false);
}

void AReplayProxyActor::BuildFromClass(UClass* RecordedClass)
{
	if (!RecordedClass)
	{
		return;
	}

	DisplayName = RecordedClass->GetName();
	DisplayName.RemoveFromEnd(TEXT("_C"));

	const AActor* CDO = RecordedClass->GetDefaultObject<AActor>();
	if (!CDO)
	{
		return;
	}

	// Walk the CDO's components rather than assuming ACharacter: a building may well be a plain
	// AActor with a static mesh, and a unit that was reparented would silently lose its mesh if we
	// only ever looked at ACharacter::GetMesh().
	bool bGotVisual = false;

	for (UActorComponent* Component : CDO->GetComponents())
	{
		if (const USkeletalMeshComponent* SourceSkeletal = Cast<USkeletalMeshComponent>(Component))
		{
			USkeletalMesh* Mesh = SourceSkeletal->GetSkeletalMeshAsset();
			if (!Mesh)
			{
				continue;
			}

			SkeletalMesh->SetSkeletalMeshAsset(Mesh);
			SkeletalMesh->SetRelativeTransform(SourceSkeletal->GetRelativeTransform());

			for (int32 i = 0; i < SourceSkeletal->GetNumMaterials(); ++i)
			{
				SkeletalMesh->SetMaterial(i, SourceSkeletal->GetMaterial(i));
			}

			// The animation blueprint is what makes a replay read as a match rather than a diorama of
			// sliding T-poses. It runs without any gameplay behind it; anything it cannot resolve just
			// leaves the unit in its idle pose.
			if (SourceSkeletal->AnimClass)
			{
				SkeletalMesh->SetAnimInstanceClass(SourceSkeletal->AnimClass);
			}

			SkeletalMesh->SetVisibility(true);
			bGotVisual = true;
			break;
		}
	}

	if (!bGotVisual)
	{
		for (UActorComponent* Component : CDO->GetComponents())
		{
			if (const UStaticMeshComponent* SourceStatic = Cast<UStaticMeshComponent>(Component))
			{
				UStaticMesh* Mesh = SourceStatic->GetStaticMesh();
				if (!Mesh)
				{
					continue;
				}

				StaticMesh->SetStaticMesh(Mesh);
				StaticMesh->SetRelativeTransform(SourceStatic->GetRelativeTransform());

				for (int32 i = 0; i < SourceStatic->GetNumMaterials(); ++i)
				{
					StaticMesh->SetMaterial(i, SourceStatic->GetMaterial(i));
				}

				StaticMesh->SetVisibility(true);
				SkeletalMesh->SetVisibility(false);
				bGotVisual = true;
				break;
			}
		}
	}

	if (!bGotVisual)
	{
		UE_LOG(LogReplayModule, Verbose, TEXT("No mesh found on %s - the proxy stays invisible but is still clickable."),
			*RecordedClass->GetName());
	}

	// Match the source capsule so clicks land where the unit looks like it is.
	if (const ACharacter* CharacterCDO = Cast<ACharacter>(CDO))
	{
		if (const UCapsuleComponent* SourceCapsule = CharacterCDO->GetCapsuleComponent())
		{
			ClickCapsule->SetCapsuleSize(
				SourceCapsule->GetUnscaledCapsuleRadius(),
				SourceCapsule->GetUnscaledCapsuleHalfHeight());
		}
	}
	else if (!bIsProjectile)
	{
		FitCapsuleToMesh();
	}

	SetActorScale3D(CDO->GetActorScale3D());
}

void AReplayProxyActor::FitCapsuleToMesh()
{
	FBoxSphereBounds Bounds(ForceInit);
	bool bHaveBounds = false;

	if (SkeletalMesh->GetSkeletalMeshAsset())
	{
		Bounds = SkeletalMesh->GetSkeletalMeshAsset()->GetBounds();
		bHaveBounds = true;
	}
	else if (StaticMesh->GetStaticMesh())
	{
		Bounds = StaticMesh->GetStaticMesh()->GetBounds();
		bHaveBounds = true;
	}

	if (!bHaveBounds)
	{
		return;
	}

	const float Radius = FMath::Clamp(FMath::Max(Bounds.BoxExtent.X, Bounds.BoxExtent.Y), 20.f, 2000.f);
	const float HalfHeight = FMath::Clamp(static_cast<float>(Bounds.BoxExtent.Z), 20.f, 2000.f);
	ClickCapsule->SetCapsuleSize(Radius, HalfHeight);
}

void AReplayProxyActor::ApplyState(const FVector& NewLocation, float YawDegrees)
{
	SetActorLocationAndRotation(NewLocation, FRotator(0.f, YawDegrees, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
}

void AReplayProxyActor::SetSelected(bool bInSelected)
{
	if (bSelected == bInSelected)
	{
		return;
	}

	bSelected = bInSelected;

	// Custom depth is the cheapest highlight that needs no material work and no extra assets; a
	// project with a post-process outline picks it up automatically.
	SkeletalMesh->SetRenderCustomDepth(bSelected);
	StaticMesh->SetRenderCustomDepth(bSelected);
	SkeletalMesh->SetCustomDepthStencilValue(bSelected ? 1 : 0);
	StaticMesh->SetCustomDepthStencilValue(bSelected ? 1 : 0);
}

void AReplayProxyActor::MakeProjectile()
{
	bIsProjectile = true;

	// A projectile must not swallow clicks meant for the unit behind it, and it has no selection
	// state worth having, so its collision goes away entirely.
	ClickCapsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AReplayProxyActor::ApplyProjectileState(const FVector& NewLocation, float YawDegrees, float PitchDegrees)
{
	SetActorLocationAndRotation(NewLocation, FRotator(PitchDegrees, YawDegrees, 0.f),
		false, nullptr, ETeleportType::TeleportPhysics);
}

void AReplayProxyActor::SetAnimationRate(float Rate)
{
	// GlobalAnimRateScale scales every montage and state machine on the component at once, which is
	// exactly the granularity wanted here - the whole proxy runs on replay time.
	SkeletalMesh->GlobalAnimRateScale = FMath::Max(0.f, Rate);
}

bool AReplayProxyActor::HasVisibleMesh() const
{
	if (SkeletalMesh && SkeletalMesh->IsVisible() && SkeletalMesh->GetSkeletalMeshAsset())
	{
		return true;
	}

	return StaticMesh && StaticMesh->IsVisible() && StaticMesh->GetStaticMesh() != nullptr;
}

