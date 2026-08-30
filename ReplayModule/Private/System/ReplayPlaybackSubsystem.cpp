// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "System/ReplayPlaybackSubsystem.h"

#include "Actors/ReplayProxyActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Integration/ReplayRTSIntegration.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "ReplayModule.h"
#include "Settings/ReplayModuleSettings.h"
#include "System/ReplayStorageSubsystem.h"

UReplayPlaybackSubsystem* UReplayPlaybackSubsystem::Get(const UObject* WorldContextObject)
{
	if (const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr)
	{
		return World->GetSubsystem<UReplayPlaybackSubsystem>();
	}

	return nullptr;
}

bool UReplayPlaybackSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Game worlds only - an editor preview world has no player controller to trace from and no
	// business spawning proxies.
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}

	return false;
}

void UReplayPlaybackSubsystem::Deinitialize()
{
	if (bActive)
	{
		EndPlayback();
	}

	Super::Deinitialize();
}

TStatId UReplayPlaybackSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UReplayPlaybackSubsystem, STATGROUP_Tickables);
}

float UReplayPlaybackSubsystem::GetDurationSeconds() const
{
	return Recording.IsValid() ? Recording->GetDurationSeconds() : 0.f;
}

bool UReplayPlaybackSubsystem::BeginPlayback()
{
	if (bActive)
	{
		return true;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const UReplayModuleSettings* Settings = GetDefault<UReplayModuleSettings>();
	if (!Settings->bEnableViewportPlayback)
	{
		UE_LOG(LogReplayModule, Log, TEXT("Viewport playback is switched off in the settings."));
		return false;
	}

	UReplayStorageSubsystem* Storage = UReplayStorageSubsystem::Get(this);
	if (!Storage)
	{
		return false;
	}

	TSharedPtr<const FReplayRecording, ESPMode::ThreadSafe> Stored = Storage->GetRecording();
	if (!Stored.IsValid() || !Stored->IsValid())
	{
		UE_LOG(LogReplayModule, Warning, TEXT("No recording to play back."));
		return false;
	}

	if (!Stored->HasActorStates())
	{
		// A recording made before this existed, or with bRecordActorStates off. The minimap replay
		// still works - only the viewport cannot be filled.
		UE_LOG(LogReplayModule, Warning,
			TEXT("Recording holds no unit states - minimap replay only. Enable bRecordActorStates and record again."));
		return false;
	}

	Recording = ConstCastSharedPtr<FReplayRecording>(Stored);

	ResolvedClasses.Reset();
	PlaybackTime = 0.f;
	Speed = FMath::Clamp(Settings->DefaultPlaybackSpeed, 0.1f, Settings->MaxPlaybackSpeed);
	bLoop = Settings->bLoopPlayback;
	bPaused = false;
	bSelectKeyWasDown = false;

	SetLiveUnitsHidden(true);

	bActive = true;
	ApplyTime(0.f);

	// Counted per side: "the client sees nothing" can mean the proxies were never spawned there or
	// that they exist and are invisible - two completely different faults.
	int32 WithMesh = 0;
	for (const TPair<uint32, TObjectPtr<AReplayProxyActor>>& Pair : Proxies)
	{
		if (IsValid(Pair.Value) && Pair.Value->HasVisibleMesh())
		{
			++WithMesh;
		}
	}

	UE_LOG(LogReplayModule, Log,
		TEXT("Viewport playback started [%s]: %d frames over %.1f s, %d classes, %d proxies (%d with a visible mesh), %d work areas."),
		World->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"),
		Recording->Frames.Num(), Recording->GetDurationSeconds(), Recording->ClassTable.Num(),
		Proxies.Num(), WithMesh, WorkAreaProxies.Num());

	OnPlaybackStateChanged.Broadcast(true);
	return true;
}

void UReplayPlaybackSubsystem::EndPlayback()
{
	if (!bActive)
	{
		return;
	}

	ClearSelection();
	ReleaseAllProxies();
	SetLiveUnitsHidden(false);

	Recording.Reset();
	ResolvedClasses.Reset();
	bActive = false;
	bPaused = false;
	PlaybackTime = 0.f;

	UE_LOG(LogReplayModule, Log, TEXT("Viewport playback ended, level restored."));
	OnPlaybackStateChanged.Broadcast(false);
}

void UReplayPlaybackSubsystem::SetPaused(bool bInPaused)
{
	bPaused = bInPaused;
	RefreshAnimationRates();
}

void UReplayPlaybackSubsystem::RefreshAnimationRates()
{
	const float Rate = bPaused ? 0.f : Speed;
	for (TPair<uint32, TObjectPtr<AReplayProxyActor>>& Pair : Proxies)
	{
		if (IsValid(Pair.Value))
		{
			Pair.Value->SetAnimationRate(Rate);
		}
	}
}

void UReplayPlaybackSubsystem::SetSpeed(float NewSpeed)
{
	const UReplayModuleSettings* Settings = GetDefault<UReplayModuleSettings>();
	Speed = FMath::Clamp(NewSpeed, 0.05f, Settings->MaxPlaybackSpeed);
	RefreshAnimationRates();
}

void UReplayPlaybackSubsystem::SeekToTime(float TimeSeconds)
{
	if (!bActive || !Recording.IsValid())
	{
		return;
	}

	PlaybackTime = FMath::Clamp(TimeSeconds, 0.f, Recording->GetDurationSeconds());
	ApplyTime(PlaybackTime);
}

void UReplayPlaybackSubsystem::Tick(float DeltaTime)
{
	if (!bActive || !Recording.IsValid())
	{
		return;
	}

	const UReplayModuleSettings* Settings = GetDefault<UReplayModuleSettings>();
	if (Settings->bAllowUnitSelection)
	{
		PollSelectionInput();
	}

	if (bPaused)
	{
		return;
	}

	const float Duration = Recording->GetDurationSeconds();
	PlaybackTime += DeltaTime * Speed;

	if (PlaybackTime >= Duration)
	{
		if (bLoop && Duration > KINDA_SMALL_NUMBER)
		{
			PlaybackTime = FMath::Fmod(PlaybackTime, Duration);
		}
		else
		{
			PlaybackTime = Duration;
			bPaused = true;
		}
	}

	ApplyTime(PlaybackTime);
}

void UReplayPlaybackSubsystem::ResolveFrames(float TimeSeconds, int32& OutFrameA, int32& OutFrameB, float& OutAlpha) const
{
	OutFrameA = INDEX_NONE;
	OutFrameB = INDEX_NONE;
	OutAlpha = 0.f;

	if (!Recording.IsValid() || Recording->Frames.Num() == 0)
	{
		return;
	}

	OutFrameA = Recording->FindFrameIndexForTime(TimeSeconds);
	if (OutFrameA == INDEX_NONE)
	{
		OutFrameA = 0;
	}

	OutFrameB = FMath::Min(OutFrameA + 1, Recording->Frames.Num() - 1);

	const float TimeA = Recording->Frames[OutFrameA].TimeSeconds;
	const float TimeB = Recording->Frames[OutFrameB].TimeSeconds;
	const float Span = TimeB - TimeA;

	// Guard the division: two frames can share a timestamp when CaptureFrameNow lands in the same
	// tick as a scheduled capture, and dividing by that zero would put every unit at NaN - which
	// does not merely look wrong, it makes the proxies disappear entirely.
	OutAlpha = (Span > KINDA_SMALL_NUMBER) ? FMath::Clamp((TimeSeconds - TimeA) / Span, 0.f, 1.f) : 0.f;
}

void UReplayPlaybackSubsystem::ApplyTime(float TimeSeconds)
{
	if (!Recording.IsValid())
	{
		return;
	}

	int32 FrameA = INDEX_NONE;
	int32 FrameB = INDEX_NONE;
	float Alpha = 0.f;
	ResolveFrames(TimeSeconds, FrameA, FrameB, Alpha);

	if (FrameA == INDEX_NONE)
	{
		return;
	}

	const FReplayFrame& A = Recording->Frames[FrameA];
	const FReplayFrame& B = Recording->Frames[FrameB];

	// Index the next frame by actor id so each unit can be blended towards where it is going. A unit
	// missing from B died in between and simply holds its last position for this step.
	TMap<uint32, const FReplayActorState*> NextById;
	NextById.Reserve(B.Actors.Num());
	for (const FReplayActorState& State : B.Actors)
	{
		NextById.Add(State.ActorId, &State);
	}

	TSet<uint32> Seen;
	Seen.Reserve(A.Actors.Num());

	for (const FReplayActorState& State : A.Actors)
	{
		AReplayProxyActor* Proxy = AcquireProxy(State);
		if (!Proxy)
		{
			continue;
		}

		Seen.Add(State.ActorId);

		FVector Location = FVector(State.Location);
		float Yaw = State.GetYawDegrees();

		// Animation values are blended towards the next frame as well, not just the transform.
		// Setting them straight from the current frame made every state change a jump: at a one
		// second recording interval the blend point snaps from one value to the next in a single
		// tick, which reads as a pop. The game gets its smoothing from FInterpTo in the animation
		// processor; here the two surrounding frames provide it, which has the added advantage of
		// being deterministic - seeking to the same moment always produces the same pose.
		float BlendA = State.GetBlendPoint1();
		float BlendB = State.GetBlendPoint2();
		float UnitSpeed = State.GetSpeed();
		float PlayRate = State.ContinuousPlayRate;
		float AnimPos = State.ContinuousAnimationPosition;

		if (const FReplayActorState* const* Found = NextById.Find(State.ActorId))
		{
			const FReplayActorState* Next = *Found;
			Location = FMath::Lerp(FVector(State.Location), FVector(Next->Location), Alpha);

			// Shortest way round, so a unit turning past 180 degrees does not spin the long way.
			const float Delta = FMath::UnwindDegrees(Next->GetYawDegrees() - State.GetYawDegrees());
			Yaw = State.GetYawDegrees() + Delta * Alpha;

			BlendA = FMath::Lerp(BlendA, Next->GetBlendPoint1(), Alpha);
			BlendB = FMath::Lerp(BlendB, Next->GetBlendPoint2(), Alpha);
			UnitSpeed = FMath::Lerp(UnitSpeed, Next->GetSpeed(), Alpha);
			PlayRate = FMath::Lerp(PlayRate, Next->ContinuousPlayRate, Alpha);

			// The animation position runs forward and wraps; blending across a wrap would rewind
			// the animation, so it is only blended while it is still moving forward.
			AnimPos = (Next->ContinuousAnimationPosition >= AnimPos)
				? FMath::Lerp(AnimPos, Next->ContinuousAnimationPosition, Alpha)
				: AnimPos;
		}

		Proxy->TeamId = State.TeamId;
		Proxy->HealthFraction = State.HealthPercent / 255.f;
		Proxy->ApplyState(Location, Yaw);

		// The animation state comes from the frame we are standing on, not the blend towards the next
		// one: it is a discrete state, and interpolating between, say, Attack and Run would land on
		// whatever enum value sits between them.
		ReplayRTS::ApplyAnimState(Proxy->SkeletalMesh, State.AnimStateIndex,
			BlendA, BlendB, UnitSpeed, PlayRate, AnimPos);
	}

	ApplyProjectiles(A, B, Alpha);
	ApplyWorkAreas(A);

	// Retire proxies whose unit is not in this frame - it died, or we seeked back before it existed.
	TArray<uint32> Gone;
	for (const TPair<uint32, TObjectPtr<AReplayProxyActor>>& Pair : Proxies)
	{
		if (!Seen.Contains(Pair.Key))
		{
			Gone.Add(Pair.Key);
		}
	}

	for (uint32 Id : Gone)
	{
		if (TObjectPtr<AReplayProxyActor>* Proxy = Proxies.Find(Id))
		{
			if (IsValid(*Proxy))
			{
				if (SelectedProxy.Get() == *Proxy)
				{
					ClearSelection();
				}

				(*Proxy)->Destroy();
			}
		}

		Proxies.Remove(Id);
	}
}

AReplayProxyActor* UReplayPlaybackSubsystem::AcquireProxy(const FReplayActorState& State)
{
	if (TObjectPtr<AReplayProxyActor>* Existing = Proxies.Find(State.ActorId))
	{
		if (IsValid(*Existing))
		{
			// Belt and braces against an id ever standing for two different units: if the class
			// changed under an id, the proxy would keep showing the first unit's mesh at the second
			// unit's position. Rebuild instead of drawing something wrong.
			if ((*Existing)->BuiltForClassIndex == static_cast<int32>(State.ClassIndex))
			{
				return *Existing;
			}

			UE_LOG(LogReplayModule, Warning,
				TEXT("Replay id %u changed class (%d -> %d) - rebuilding its proxy."),
				State.ActorId, (*Existing)->BuiltForClassIndex, State.ClassIndex);

			(*Existing)->Destroy();
		}

		Proxies.Remove(State.ActorId);
	}

	UWorld* World = GetWorld();
	if (!World || !Recording.IsValid())
	{
		return nullptr;
	}

	UClass* RecordedClass = ResolveClass(State.ClassIndex);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;   // a proxy must never end up saved in the level

	AReplayProxyActor* Proxy = World->SpawnActor<AReplayProxyActor>(
		AReplayProxyActor::StaticClass(),
		FVector(State.Location),
		FRotator(0.f, State.GetYawDegrees(), 0.f),
		Params);

	if (!Proxy)
	{
		return nullptr;
	}

	Proxy->RecordedActorId = State.ActorId;
	Proxy->BuiltForClassIndex = static_cast<int32>(State.ClassIndex);
	Proxy->BuildFromClass(RecordedClass);

	// A proxy spawned mid-playback has to start at the current rate, not at 1x.
	Proxy->SetAnimationRate(bPaused ? 0.f : Speed);

	Proxies.Add(State.ActorId, Proxy);
	return Proxy;
}

UClass* UReplayPlaybackSubsystem::ResolveClass(uint16 ClassIndex)
{
	// Resolved once per index. TryLoadClass goes through the asset registry, which is far too
	// expensive to run per unit per frame.
	if (TObjectPtr<UClass>* Cached = ResolvedClasses.Find(ClassIndex))
	{
		return *Cached;
	}

	UClass* Loaded = nullptr;
	if (Recording.IsValid() && Recording->ClassTable.IsValidIndex(ClassIndex))
	{
		Loaded = Recording->ClassTable[ClassIndex].TryLoadClass<AActor>();
	}

	// Cached even when null, so a class that cannot be resolved is not retried every frame.
	ResolvedClasses.Add(ClassIndex, Loaded);

	if (!Loaded)
	{
		UE_LOG(LogReplayModule, Warning, TEXT("Recorded class %d could not be loaded - those actors stay invisible."),
			ClassIndex);
	}

	return Loaded;
}

AReplayProxyActor* UReplayPlaybackSubsystem::AcquireProjectileProxy(const FReplayProjectileState& State)
{
	if (TObjectPtr<AReplayProxyActor>* Existing = ProjectileProxies.Find(State.ProjectileId))
	{
		if (IsValid(*Existing))
		{
			return *Existing;
		}

		ProjectileProxies.Remove(State.ProjectileId);
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;

	AReplayProxyActor* Proxy = World->SpawnActor<AReplayProxyActor>(
		AReplayProxyActor::StaticClass(),
		FVector(State.Location),
		FRotator(State.GetPitchDegrees(), State.GetYawDegrees(), 0.f),
		Params);

	if (!Proxy)
	{
		return nullptr;
	}

	// Order matters: MakeProjectile first, so BuildFromClass skips the capsule fitting a shot does
	// not want.
	Proxy->MakeProjectile();
	Proxy->RecordedActorId = State.ProjectileId;
	Proxy->BuiltForClassIndex = static_cast<int32>(State.ClassIndex);
	Proxy->BuildFromClass(ResolveClass(State.ClassIndex));

	ProjectileProxies.Add(State.ProjectileId, Proxy);
	return Proxy;
}

void UReplayPlaybackSubsystem::ApplyProjectiles(const FReplayFrame& A, const FReplayFrame& B, float Alpha)
{
	TMap<uint32, const FReplayProjectileState*> NextById;
	NextById.Reserve(B.Projectiles.Num());
	for (const FReplayProjectileState& State : B.Projectiles)
	{
		NextById.Add(State.ProjectileId, &State);
	}

	TSet<uint32> Seen;
	Seen.Reserve(A.Projectiles.Num());

	for (const FReplayProjectileState& State : A.Projectiles)
	{
		AReplayProxyActor* Proxy = AcquireProjectileProxy(State);
		if (!Proxy)
		{
			continue;
		}

		Seen.Add(State.ProjectileId);

		FVector Location = FVector(State.Location);
		float Yaw = State.GetYawDegrees();
		float Pitch = State.GetPitchDegrees();

		// Interpolation matters far more here than for units: at a one-second recording interval a
		// shot covers thousands of units between two frames, so without this it would appear as a
		// row of stationary projectiles rather than something in flight.
		if (const FReplayProjectileState* const* Found = NextById.Find(State.ProjectileId))
		{
			const FReplayProjectileState* Next = *Found;
			Location = FMath::Lerp(FVector(State.Location), FVector(Next->Location), Alpha);
			Yaw += FMath::UnwindDegrees(Next->GetYawDegrees() - State.GetYawDegrees()) * Alpha;
			Pitch += FMath::UnwindDegrees(Next->GetPitchDegrees() - State.GetPitchDegrees()) * Alpha;
		}

		Proxy->ApplyProjectileState(Location, Yaw, Pitch);
	}

	// A projectile that is gone hit something or timed out - either way it stops existing.
	TArray<uint32> Gone;
	for (const TPair<uint32, TObjectPtr<AReplayProxyActor>>& Pair : ProjectileProxies)
	{
		if (!Seen.Contains(Pair.Key))
		{
			Gone.Add(Pair.Key);
		}
	}

	for (uint32 Id : Gone)
	{
		if (TObjectPtr<AReplayProxyActor>* Proxy = ProjectileProxies.Find(Id))
		{
			if (IsValid(*Proxy))
			{
				(*Proxy)->Destroy();
			}
		}

		ProjectileProxies.Remove(Id);
	}
}

AReplayProxyActor* UReplayPlaybackSubsystem::AcquireWorkAreaProxy(const FReplayWorkAreaState& State)
{
	if (TObjectPtr<AReplayProxyActor>* Existing = WorkAreaProxies.Find(State.AreaId))
	{
		if (IsValid(*Existing))
		{
			return *Existing;
		}

		WorkAreaProxies.Remove(State.AreaId);
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;

	AReplayProxyActor* Proxy = World->SpawnActor<AReplayProxyActor>(
		AReplayProxyActor::StaticClass(),
		FVector(State.Location),
		FRotator(0.f, State.GetYawDegrees(), 0.f),
		Params);

	if (!Proxy)
	{
		return nullptr;
	}

	// Work areas are scenery, not something the viewer selects - so no click collision, and no
	// capsule fitting that would fight the recorded scale.
	Proxy->MakeProjectile();
	Proxy->RecordedActorId = State.AreaId;
	Proxy->BuiltForClassIndex = static_cast<int32>(State.ClassIndex);
	Proxy->BuildFromClass(ResolveClass(State.ClassIndex));

	WorkAreaProxies.Add(State.AreaId, Proxy);
	return Proxy;
}

void UReplayPlaybackSubsystem::ApplyWorkAreas(const FReplayFrame& Frame)
{
	TSet<uint32> Seen;
	Seen.Reserve(Frame.WorkAreas.Num());

	for (const FReplayWorkAreaState& State : Frame.WorkAreas)
	{
		AReplayProxyActor* Proxy = AcquireWorkAreaProxy(State);
		if (!Proxy)
		{
			continue;
		}

		Seen.Add(State.AreaId);

		// No interpolation here: work areas do not move, and their scale changes in discrete steps
		// as resources are taken out. Blending would only smear those steps.
		Proxy->ApplyState(FVector(State.Location), State.GetYawDegrees());
		Proxy->SetActorScale3D(FVector(State.GetScale()));
	}

	// An area that is gone was mined out or built over.
	TArray<uint32> Gone;
	for (const TPair<uint32, TObjectPtr<AReplayProxyActor>>& Pair : WorkAreaProxies)
	{
		if (!Seen.Contains(Pair.Key))
		{
			Gone.Add(Pair.Key);
		}
	}

	for (uint32 Id : Gone)
	{
		if (TObjectPtr<AReplayProxyActor>* Proxy = WorkAreaProxies.Find(Id))
		{
			if (IsValid(*Proxy))
			{
				(*Proxy)->Destroy();
			}
		}

		WorkAreaProxies.Remove(Id);
	}
}

void UReplayPlaybackSubsystem::ReleaseAllProxies()
{
	for (TPair<uint32, TObjectPtr<AReplayProxyActor>>& Pair : Proxies)
	{
		if (IsValid(Pair.Value))
		{
			Pair.Value->Destroy();
		}
	}

	Proxies.Reset();

	for (TPair<uint32, TObjectPtr<AReplayProxyActor>>& Pair : ProjectileProxies)
	{
		if (IsValid(Pair.Value))
		{
			Pair.Value->Destroy();
		}
	}

	ProjectileProxies.Reset();

	for (TPair<uint32, TObjectPtr<AReplayProxyActor>>& Pair : WorkAreaProxies)
	{
		if (IsValid(Pair.Value))
		{
			Pair.Value->Destroy();
		}
	}

	WorkAreaProxies.Reset();
}

void UReplayPlaybackSubsystem::SetLiveUnitsHidden(bool bHidden)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (bHidden)
	{
		HiddenLiveActors.Reset();

		// Hidden by base type rather than by RTSUnitTemplate class: this file must not depend on
		// that plugin's headers - the integration is the only place allowed to know about them.
		// Every recorded unit is a Pawn, so that is the net to cast.
		const APlayerController* PC = World->GetFirstPlayerController();
		const APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;

		int32 CandidateCount = 0;
		int32 PawnCount = 0;
		int32 WorkAreaCount = 0;
		int32 AlreadyHiddenCount = 0;

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!IsValid(Actor) || Actor == PlayerPawn || Actor->IsA(AReplayProxyActor::StaticClass()))
			{
				continue;
			}

			// Pawns are the units; work areas are plain actors and are matched by class name so this
			// file stays free of RTSUnitTemplate headers. Without hiding them the replay would show the
			// live resource nodes on top of the recorded ones.
			const bool bIsUnit = Actor->IsA(APawn::StaticClass());
			const bool bIsWorkArea = Actor->GetClass()->GetName().Contains(TEXT("WorkArea"));
			if (!bIsUnit && !bIsWorkArea)
			{
				continue;
			}

			++CandidateCount;
			if (bIsUnit) { ++PawnCount; } else { ++WorkAreaCount; }

			// Something already invisible stays that way - and is counted, because on a client most
			// units are hidden by fog and that is what makes the client's number so much smaller.
			if (Actor->IsHidden())
			{
				++AlreadyHiddenCount;
				continue;
			}

			Actor->SetActorHiddenInGame(true);
			Actor->SetActorEnableCollision(false);
			HiddenLiveActors.Add(Actor);
		}

		// Broken down on purpose: "1 hidden" on a client next to "67" on the server says the client
		// barely has these actors at all, not that hiding failed - and those are very different problems.
		UE_LOG(LogReplayModule, Log,
			TEXT("Hidden for playback [%s]: %d of %d candidates (%d pawns, %d work areas, %d already hidden)."),
			World->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"),
			HiddenLiveActors.Num(), CandidateCount, PawnCount, WorkAreaCount, AlreadyHiddenCount);
	}
	else
	{
		for (const TWeakObjectPtr<AActor>& Weak : HiddenLiveActors)
		{
			if (AActor* Actor = Weak.Get())
			{
				Actor->SetActorHiddenInGame(false);
				Actor->SetActorEnableCollision(true);
			}
		}

		HiddenLiveActors.Reset();
	}
}

void UReplayPlaybackSubsystem::PollSelectionInput()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return;
	}

	// Polling instead of binding: an input binding would have to be installed on the project's own
	// controller, and this module is not allowed to reach into it. Edge-detected so holding the
	// button does not re-select every tick.
	const bool bDown = PC->IsInputKeyDown(EKeys::LeftMouseButton);
	const bool bJustPressed = bDown && !bSelectKeyWasDown;
	bSelectKeyWasDown = bDown;

	if (bJustPressed)
	{
		SelectProxyUnderCursor();
	}
}

AReplayProxyActor* UReplayPlaybackSubsystem::SelectProxyUnderCursor()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return nullptr;
	}

	FHitResult Hit;
	if (!PC->GetHitResultUnderCursor(ECC_Visibility, false, Hit))
	{
		ClearSelection();
		return nullptr;
	}

	AReplayProxyActor* Proxy = Cast<AReplayProxyActor>(Hit.GetActor());
	if (!Proxy)
	{
		ClearSelection();
		return nullptr;
	}

	if (SelectedProxy.Get() == Proxy)
	{
		return Proxy;
	}

	ClearSelection();

	Proxy->SetSelected(true);
	SelectedProxy = Proxy;

	UE_LOG(LogReplayModule, Verbose, TEXT("Replay selection: %s (team %d, %.0f %% health)."),
		*Proxy->DisplayName, Proxy->TeamId, Proxy->HealthFraction * 100.f);

	OnProxySelected.Broadcast(Proxy);
	return Proxy;
}

void UReplayPlaybackSubsystem::ClearSelection()
{
	if (AReplayProxyActor* Proxy = SelectedProxy.Get())
	{
		Proxy->SetSelected(false);
	}

	if (SelectedProxy.IsValid())
	{
		SelectedProxy.Reset();
		OnProxySelected.Broadcast(nullptr);
	}
}

void UReplayPlaybackSubsystem::LogAnimStateBreakdown()
{
	if (!bActive || !Recording.IsValid())
	{
		UE_LOG(LogReplayModule, Display, TEXT("Anim states: no playback running."));
		return;
	}

	int32 FrameA = INDEX_NONE;
	int32 FrameB = INDEX_NONE;
	float Alpha = 0.f;
	ResolveFrames(PlaybackTime, FrameA, FrameB, Alpha);

	if (FrameA == INDEX_NONE)
	{
		return;
	}

	// What the recording says.
	TMap<uint8, int32> Recorded;
	for (const FReplayActorState& State : Recording->Frames[FrameA].Actors)
	{
		Recorded.FindOrAdd(State.AnimStateIndex)++;
	}

	// What the proxies are actually showing.
	TMap<uint8, int32> Live;
	int32 WithoutAnimInstance = 0;
	for (const TPair<uint32, TObjectPtr<AReplayProxyActor>>& Pair : Proxies)
	{
		if (!IsValid(Pair.Value))
		{
			continue;
		}

		const uint8 Actual = ReplayRTS::ReadAnimState(Pair.Value->SkeletalMesh);
		if (Actual == 255)
		{
			++WithoutAnimInstance;
			continue;
		}

		Live.FindOrAdd(Actual)++;
	}

	UE_LOG(LogReplayModule, Display, TEXT("Anim states at %.1f s (frame %d):"), PlaybackTime, FrameA);

	for (const TPair<uint8, int32>& Pair : Recorded)
	{
		UE_LOG(LogReplayModule, Display, TEXT("  recorded %-24s %d"),
			*ReplayRTS::GetStateName(Pair.Key), Pair.Value);
	}

	for (const TPair<uint8, int32>& Pair : Live)
	{
		UE_LOG(LogReplayModule, Display, TEXT("  showing  %-24s %d"),
			*ReplayRTS::GetStateName(Pair.Key), Pair.Value);
	}

	UE_LOG(LogReplayModule, Display, TEXT("  proxies without an RTS anim instance: %d"), WithoutAnimInstance);

	// Which anim blueprint is actually running. A worker showing the wrong animation while its state
	// is right points at the blueprint, not at the data.
	TMap<FString, int32> AnimClasses;
	for (const TPair<uint32, TObjectPtr<AReplayProxyActor>>& Pair : Proxies)
	{
		if (!IsValid(Pair.Value) || !Pair.Value->SkeletalMesh)
		{
			continue;
		}

		const FString Name = GetNameSafe(Pair.Value->SkeletalMesh->AnimClass);
		AnimClasses.FindOrAdd(Name)++;
	}

	for (const TPair<FString, int32>& Pair : AnimClasses)
	{
		UE_LOG(LogReplayModule, Display, TEXT("  anim class %-40s %d"), *Pair.Key, Pair.Value);
	}

	// Blend points steer the blend spaces inside a state. A unit can sit in the right state and still
	// show a standing pose when these stay at zero, which looks exactly like a stuck animation.
	float RecMin = 0.f, RecMax = 0.f, LiveMin = 0.f, LiveMax = 0.f;
	bool bFirst = true;
	for (const FReplayActorState& State : Recording->Frames[FrameA].Actors)
	{
		const float B1 = State.GetBlendPoint1();
		if (bFirst) { RecMin = RecMax = B1; bFirst = false; }
		else { RecMin = FMath::Min(RecMin, B1); RecMax = FMath::Max(RecMax, B1); }
	}

	int32 LiveMoving = 0;
	int32 LiveValid = 0;
	bFirst = true;
	for (const TPair<uint32, TObjectPtr<AReplayProxyActor>>& Pair : Proxies)
	{
		if (!IsValid(Pair.Value)) { continue; }
		float B1 = 0.f, B2 = 0.f, Sp = 0.f;
		bool bValid = false;
		if (!ReplayRTS::ReadBlendPoints(Pair.Value->SkeletalMesh, B1, B2, Sp, bValid)) { continue; }
		if (Sp > 1.f) { ++LiveMoving; }
		if (bValid) { ++LiveValid; }
		if (bFirst) { LiveMin = LiveMax = B1; bFirst = false; }
		else { LiveMin = FMath::Min(LiveMin, B1); LiveMax = FMath::Max(LiveMax, B1); }
	}

	// Distribution, not range. The range said 0.00..1.27 while nearly every proxy sat at 0 - one
	// outlier is enough to make a broken spread look healthy.
	int32 AtZero = 0, NonZero = 0;
	for (const TPair<uint32, TObjectPtr<AReplayProxyActor>>& Pair : Proxies)
	{
		if (!IsValid(Pair.Value)) { continue; }
		float b1 = 0.f, b2 = 0.f, sp = 0.f;
		bool valid = false;
		if (!ReplayRTS::ReadBlendPoints(Pair.Value->SkeletalMesh, b1, b2, sp, valid)) { continue; }
		if (FMath::IsNearlyZero(b1)) { ++AtZero; } else { ++NonZero; }
	}

	UE_LOG(LogReplayModule, Display,
		TEXT("  blend point 1: recorded %.2f..%.2f, showing %.2f..%.2f | %d proxies at zero, %d non-zero"),
		RecMin, RecMax, LiveMin, LiveMax, AtZero, NonZero);

	// Speed range, because a blend space with everything at zero is what a frozen-looking replay
	// actually is - correct states, no motion inside them.
	float SpeedMin = 0.f, SpeedMax = 0.f;
	int32 Moving = 0;
	bFirst = true;
	for (const FReplayActorState& State : Recording->Frames[FrameA].Actors)
	{
		const float S = State.GetSpeed();
		if (S > 1.f) { ++Moving; }
		if (bFirst) { SpeedMin = SpeedMax = S; bFirst = false; }
		else { SpeedMin = FMath::Min(SpeedMin, S); SpeedMax = FMath::Max(SpeedMax, S); }
	}

	UE_LOG(LogReplayModule, Display, TEXT("  speed: recorded %.0f..%.0f uu/s, %d of %d units moving"),
		SpeedMin, SpeedMax, Moving, Recording->Frames[FrameA].Actors.Num());

	// The decisive pair: how many proxies actually carry a speed, and on how many the anim instance
	// still regards it as valid. NativeUpdateAnimation clears bMassSpeedValid on every tick before
	// it looks at the owner, so a value written from outside may not survive to be used.
	UE_LOG(LogReplayModule, Display, TEXT("  on the proxies: %d with speed > 1, %d with bMassSpeedValid"),
		LiveMoving, LiveValid);
}

