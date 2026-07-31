// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "System/ReplayRecorderSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Components/ReplayMarkerComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Integration/ReplayRTSIntegration.h"
#include "ReplayModule.h"
#include "Settings/ReplayModuleSettings.h"
#include "System/ReplayStorageSubsystem.h"
#include "TimerManager.h"
#include "Widgets/ReplayLauncherWidget.h"

UReplayRecorderSubsystem* UReplayRecorderSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UReplayRecorderSubsystem>() : nullptr;
}

bool UReplayRecorderSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	if (!World)
	{
		return false;
	}

	// A replay is a picture of what a player saw, so an editor preview or an inactive world has
	// nothing to record.
	return World->IsGameWorld();
}

void UReplayRecorderSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// A dedicated server has no local view to record; the clients each record their own.
	if (InWorld.GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const UReplayModuleSettings* Settings = GetDefault<UReplayModuleSettings>();
	RecordInterval = FMath::Max(0.05f, Settings->RecordIntervalSeconds);

	if (Settings->bAutoRecord)
	{
		BootstrapDeadline = InWorld.GetTimeSeconds() + FMath::Max(1.f, Settings->BootstrapTimeoutSeconds);

		InWorld.GetTimerManager().SetTimer(
			BootstrapTimerHandle,
			FTimerDelegate::CreateUObject(this, &UReplayRecorderSubsystem::TryBootstrap),
			FMath::Max(0.1f, Settings->BootstrapRetryInterval),
			/*bLoop=*/true,
			/*FirstDelay=*/FMath::Max(0.1f, Settings->BootstrapRetryInterval));
	}

	if (Settings->bDetectGameEnd)
	{
		InWorld.GetTimerManager().SetTimer(
			GameEndTimerHandle,
			FTimerDelegate::CreateUObject(this, &UReplayRecorderSubsystem::PollGameEnd),
			FMath::Max(0.1f, Settings->GameEndPollInterval),
			/*bLoop=*/true);
	}
}

void UReplayRecorderSubsystem::Deinitialize()
{
	StopTimers();

	// A match that ends through a level change rather than a win screen would otherwise lose
	// everything recorded so far.
	if (bRecording)
	{
		StopRecording();
	}

	Markers.Reset();
	WorkingRecording.Reset();
	ActiveDotSink = nullptr;

	Super::Deinitialize();
}

void UReplayRecorderSubsystem::TryBootstrap()
{
	UWorld* World = GetWorld();
	if (!World || bBootstrapFinished || bRecording)
	{
		return;
	}

	FVector2D WorldMin = FVector2D::ZeroVector;
	FVector2D WorldMax = FVector2D::ZeroVector;
	FReplayStyle Style = PendingStyle;
	int32 LocalTeamId = -1;
	UTexture2D* Background = nullptr;

	if (ReplayRTS::ResolveRecordingSetup(World, WorldMin, WorldMax, Style, LocalTeamId, Background))
	{
		PendingStyle = Style;

		if (Background)
		{
			PendingBackgroundTexture = Background;
		}

		bBootstrapFinished = true;
		World->GetTimerManager().ClearTimer(BootstrapTimerHandle);

		// Before StartRecording, not after: the very first frame is captured inside it and needs the
		// team to pick friend/enemy colors.
		PendingLocalTeamId = LocalTeamId;

		StartRecording(WorldMin, WorldMax);
		return;
	}

	if (World->GetTimeSeconds() >= BootstrapDeadline)
	{
		bBootstrapFinished = true;
		World->GetTimerManager().ClearTimer(BootstrapTimerHandle);

		UE_LOG(LogReplayModule, Log,
			TEXT("No map bounds could be resolved within the bootstrap window - auto recording is off for this match. ")
			TEXT("Call StartRecording with your own bounds to record anyway."));
	}
}

void UReplayRecorderSubsystem::StartRecording(FVector2D WorldMin, FVector2D WorldMax)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (FMath::IsNearlyEqual(WorldMin.X, WorldMax.X) || FMath::IsNearlyEqual(WorldMin.Y, WorldMax.Y))
	{
		UE_LOG(LogReplayModule, Warning, TEXT("StartRecording was given degenerate bounds (%s .. %s) - ignoring."),
			*WorldMin.ToString(), *WorldMax.ToString());
		return;
	}

	const UReplayModuleSettings* Settings = GetDefault<UReplayModuleSettings>();

	WorkingRecording = MakeShared<FReplayRecording, ESPMode::ThreadSafe>();
	WorkingRecording->MapName = World->GetMapName();
	WorkingRecording->RecordedAtUtc = FDateTime::UtcNow();
	WorkingRecording->IntervalSeconds = RecordInterval;
	WorkingRecording->ReferenceTextureSize = FMath::Clamp(Settings->ReferenceTextureSize, 64, 2048);
	WorkingRecording->WorldMin = FVector2D(FMath::Min(WorldMin.X, WorldMax.X), FMath::Min(WorldMin.Y, WorldMax.Y));
	WorkingRecording->WorldMax = FVector2D(FMath::Max(WorldMin.X, WorldMax.X), FMath::Max(WorldMin.Y, WorldMax.Y));
	WorkingRecording->Style = PendingStyle;
	WorkingRecording->LocalTeamId = PendingLocalTeamId;

	RecordStartTime = World->GetTimeSeconds();
	bRecording = true;

	StartTimers();

	// The first frame goes down immediately so a very short match still has something to show.
	CaptureFrame();

	UE_LOG(LogReplayModule, Log, TEXT("Recording started on '%s' (%.0f x %.0f uu, every %.2fs, RTS integration %s)."),
		*WorkingRecording->MapName,
		WorkingRecording->WorldMax.X - WorkingRecording->WorldMin.X,
		WorkingRecording->WorldMax.Y - WorkingRecording->WorldMin.Y,
		RecordInterval,
		ReplayRTS::IsAvailable() ? TEXT("on") : TEXT("off"));

	OnRecordingStateChanged.Broadcast(true);
}

void UReplayRecorderSubsystem::StopRecording()
{
	if (!bRecording)
	{
		return;
	}

	bRecording = false;
	StopTimers();

	if (WorkingRecording.IsValid() && WorkingRecording->IsValid())
	{
		if (UReplayStorageSubsystem* Storage = UReplayStorageSubsystem::Get(this))
		{
			if (PendingBackgroundTexture)
			{
				Storage->SetBackgroundTexture(PendingBackgroundTexture);
			}

			Storage->SetRecording(WorkingRecording);
		}
	}
	else
	{
		UE_LOG(LogReplayModule, Log, TEXT("Recording stopped without a usable frame - nothing was published."));
	}

	WorkingRecording.Reset();

	OnRecordingStateChanged.Broadcast(false);
}

void UReplayRecorderSubsystem::SetRecordInterval(float Seconds)
{
	RecordInterval = FMath::Clamp(Seconds, 0.05f, 60.f);

	if (WorkingRecording.IsValid())
	{
		WorkingRecording->IntervalSeconds = RecordInterval;
	}

	if (bRecording)
	{
		StartTimers();
	}
}

void UReplayRecorderSubsystem::CaptureFrameNow()
{
	if (bRecording)
	{
		CaptureFrame();
	}
}

void UReplayRecorderSubsystem::SetLocalTeamId(int32 InTeamId)
{
	PendingLocalTeamId = InTeamId;

	if (WorkingRecording.IsValid())
	{
		WorkingRecording->LocalTeamId = InTeamId;
	}
}

void UReplayRecorderSubsystem::SetStyle(const FReplayStyle& InStyle)
{
	PendingStyle = InStyle;

	if (WorkingRecording.IsValid())
	{
		WorkingRecording->Style = InStyle;
	}
}

void UReplayRecorderSubsystem::SetBackgroundTexture(UTexture2D* InTexture)
{
	PendingBackgroundTexture = InTexture;

	// Publishing right away keeps a replay that is opened mid-match on the same terrain layer.
	if (UReplayStorageSubsystem* Storage = UReplayStorageSubsystem::Get(this))
	{
		Storage->SetBackgroundTexture(InTexture);
	}
}

void UReplayRecorderSubsystem::SetBackgroundPixels(const TArray<FColor>& Pixels, int32 Size)
{
	if (!WorkingRecording.IsValid())
	{
		UE_LOG(LogReplayModule, Warning, TEXT("SetBackgroundPixels needs a running recording."));
		return;
	}

	if (Size <= 0 || Pixels.Num() != Size * Size)
	{
		UE_LOG(LogReplayModule, Warning, TEXT("SetBackgroundPixels expected %d pixels for size %d, got %d."),
			Size * Size, Size, Pixels.Num());
		return;
	}

	WorkingRecording->BackgroundPixels = Pixels;
	WorkingRecording->BackgroundSize = Size;
}

void UReplayRecorderSubsystem::NotifyGameEnded()
{
	if (bGameEnded)
	{
		return;
	}

	bGameEnded = true;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GameEndTimerHandle);
		World->GetTimerManager().ClearTimer(BootstrapTimerHandle);
	}

	StopRecording();

	const UReplayModuleSettings* Settings = GetDefault<UReplayModuleSettings>();

	if (Settings->bAutoSaveOnGameEnd)
	{
		if (UReplayStorageSubsystem* Storage = UReplayStorageSubsystem::Get(this))
		{
			Storage->SaveReplayToSlot(Settings->AutoSaveSlotName, Settings->AutoSaveUserIndex);
		}
	}

	if (Settings->bShowLauncherOnGameEnd)
	{
		ShowLauncherWidget();
	}
}

void UReplayRecorderSubsystem::RegisterMarker(UReplayMarkerComponent* Marker)
{
	if (Marker)
	{
		Markers.AddUnique(Marker);
	}
}

void UReplayRecorderSubsystem::UnregisterMarker(UReplayMarkerComponent* Marker)
{
	if (Marker)
	{
		Markers.RemoveSingleSwap(Marker);
	}
}

bool UReplayRecorderSubsystem::PushDot(FVector WorldLocation, uint8 ColorIndex, float WorldRadius, float SightRadius)
{
	if (!ActiveDotSink || !WorkingRecording.IsValid())
	{
		return false;
	}

	const UReplayModuleSettings* Settings = GetDefault<UReplayModuleSettings>();
	if (ActiveDotSink->Num() >= Settings->MaxDotsPerFrame)
	{
		return false;
	}

	FReplayDot Dot;
	if (!WorkingRecording->WorldToNormalized(WorldLocation, Dot.X, Dot.Y))
	{
		return false;
	}

	Dot.ColorIndex = ColorIndex;
	Dot.PixelRadius = QuantizeRadius(WorldRadius);
	Dot.SightPixelRadius = QuantizeRadius(SightRadius);

	ActiveDotSink->Add(Dot);
	return true;
}

void UReplayRecorderSubsystem::CaptureFrame()
{
	UWorld* World = GetWorld();
	if (!World || !WorkingRecording.IsValid())
	{
		return;
	}

	const UReplayModuleSettings* Settings = GetDefault<UReplayModuleSettings>();

	if (WorkingRecording->Frames.Num() >= Settings->MaxFrames)
	{
		UE_LOG(LogReplayModule, Warning, TEXT("Frame budget of %d reached - recording stops here."), Settings->MaxFrames);
		StopRecording();
		return;
	}

	FReplayFrame Frame;
	Frame.TimeSeconds = World->GetTimeSeconds() - RecordStartTime;

	// PushDot writes through this pointer, so it is only ever live for the length of one capture.
	ActiveDotSink = &Frame.Dots;
	GatherDots(Frame.Dots);
	OnCaptureFrame.Broadcast();
	ActiveDotSink = nullptr;

	if (Settings->bRecordViewport && WorkingRecording->Style.bDrawViewport)
	{
		GatherViewport(Frame.Viewport);
	}

	WorkingRecording->Frames.Add(MoveTemp(Frame));
}

void UReplayRecorderSubsystem::GatherDots(TArray<FReplayDot>& OutDots) const
{
	const UWorld* World = GetWorld();
	if (!World || !WorkingRecording.IsValid())
	{
		return;
	}

	const UReplayModuleSettings* Settings = GetDefault<UReplayModuleSettings>();
	const int32 MaxDots = Settings->MaxDotsPerFrame;

	OutDots.Reserve(FMath::Min(MaxDots, 256));

	// The RTS integration is a no-op in a project without RTSUnitTemplate.
	ReplayRTS::GatherUnitDots(World, *WorkingRecording, Settings->VisibilityMode, MaxDots, OutDots);

	// Marker components come on top, which is also the only source in a standalone project.
	for (const TWeakObjectPtr<UReplayMarkerComponent>& WeakMarker : Markers)
	{
		if (OutDots.Num() >= MaxDots)
		{
			break;
		}

		const UReplayMarkerComponent* Marker = WeakMarker.Get();
		if (!Marker || !Marker->bRecordable)
		{
			continue;
		}

		const AActor* Owner = Marker->GetOwner();
		if (!IsValid(Owner))
		{
			continue;
		}

		FReplayDot Dot;
		if (!WorkingRecording->WorldToNormalized(Owner->GetActorLocation(), Dot.X, Dot.Y))
		{
			continue;
		}

		Dot.ColorIndex = Marker->GetColorIndex();
		Dot.PixelRadius = QuantizeRadius(Marker->WorldRadius);
		Dot.SightPixelRadius = QuantizeRadius(Marker->SightRadius);

		OutDots.Add(Dot);
	}
}

void UReplayRecorderSubsystem::GatherViewport(FReplayViewportQuad& OutQuad) const
{
	const UWorld* World = GetWorld();
	if (!World || !WorkingRecording.IsValid())
	{
		return;
	}

	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}

	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	PC->GetViewportSize(ViewportWidth, ViewportHeight);

	if (ViewportWidth <= 0 || ViewportHeight <= 0)
	{
		return;
	}

	const FVector2D ScreenCorners[4] =
	{
		FVector2D(0.f, 0.f),
		FVector2D(static_cast<float>(ViewportWidth), 0.f),
		FVector2D(static_cast<float>(ViewportWidth), static_cast<float>(ViewportHeight)),
		FVector2D(0.f, static_cast<float>(ViewportHeight))
	};

	const double ExtentX = WorkingRecording->WorldMax.X - WorkingRecording->WorldMin.X;
	const double ExtentY = WorkingRecording->WorldMax.Y - WorkingRecording->WorldMin.Y;

	TArray<FVector2f> Corners;
	Corners.Reserve(4);

	for (const FVector2D& ScreenPos : ScreenCorners)
	{
		FVector RayOrigin;
		FVector RayDirection;
		if (!PC->DeprojectScreenPositionToWorld(ScreenPos.X, ScreenPos.Y, RayOrigin, RayDirection))
		{
			return;
		}

		// Same ground-plane intersection the live minimap uses for its camera rectangle.
		if (FMath::Abs(RayDirection.Z) <= KINDA_SMALL_NUMBER)
		{
			return;
		}

		const double T = -RayOrigin.Z / RayDirection.Z;
		if (T <= 0.0)
		{
			return;
		}

		const FVector Ground = RayOrigin + RayDirection * T;

		// Unlike unit dots the camera quad may hang off the map, so clamp instead of dropping it.
		Corners.Add(FVector2f(
			static_cast<float>(FMath::Clamp((Ground.X - WorkingRecording->WorldMin.X) / ExtentX, 0.0, 1.0)),
			static_cast<float>(FMath::Clamp((Ground.Y - WorkingRecording->WorldMin.Y) / ExtentY, 0.0, 1.0))));
	}

	OutQuad.Corners = MoveTemp(Corners);
	OutQuad.bValid = true;
}

uint8 UReplayRecorderSubsystem::QuantizeRadius(float WorldRadius) const
{
	if (WorldRadius <= 0.f || !WorkingRecording.IsValid())
	{
		return 0;
	}

	const double ExtentX = FMath::Max(1.0, WorkingRecording->WorldMax.X - WorkingRecording->WorldMin.X);
	const double Pixels = (WorldRadius / ExtentX) * static_cast<double>(WorkingRecording->ReferenceTextureSize);

	return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Pixels), 1, 255));
}

void UReplayRecorderSubsystem::StartTimers()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	World->GetTimerManager().SetTimer(
		CaptureTimerHandle,
		FTimerDelegate::CreateUObject(this, &UReplayRecorderSubsystem::CaptureFrame),
		RecordInterval,
		/*bLoop=*/true);
}

void UReplayRecorderSubsystem::StopTimers()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CaptureTimerHandle);
		World->GetTimerManager().ClearTimer(BootstrapTimerHandle);
	}
}

void UReplayRecorderSubsystem::PollGameEnd()
{
	if (bGameEnded)
	{
		return;
	}

	if (ReplayRTS::IsMatchOverScreenVisible(GetWorld()))
	{
		UE_LOG(LogReplayModule, Log, TEXT("Win/lose screen detected - closing the recording."));
		NotifyGameEnded();
	}
}

void UReplayRecorderSubsystem::ShowLauncherWidget()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}

	const UReplayStorageSubsystem* Storage = UReplayStorageSubsystem::Get(this);
	if (!Storage || !Storage->HasReplay())
	{
		// Nothing worth offering - a match that ended before the first frame, for instance.
		return;
	}

	const UReplayModuleSettings* Settings = GetDefault<UReplayModuleSettings>();

	UClass* LauncherClass = Settings->LauncherWidgetClass.IsNull()
		? UReplayLauncherWidget::StaticClass()
		: Settings->LauncherWidgetClass.LoadSynchronous();

	if (!LauncherClass)
	{
		LauncherClass = UReplayLauncherWidget::StaticClass();
	}

	if (UReplayLauncherWidget* Launcher = CreateWidget<UReplayLauncherWidget>(PC, LauncherClass))
	{
		Launcher->AddToViewport(Settings->LauncherZOrder);
	}
}
