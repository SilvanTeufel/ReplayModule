// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "ReplayModule.h"

#include "Blueprint/ReplayFunctionLibrary.h"
#include "Data/ReplayFrameData.h"
#include "Save/ReplaySaveGame.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "System/ReplayPlaybackSubsystem.h"
#include "System/ReplayRecorderSubsystem.h"
#include "Integration/ReplayRTSIntegration.h"
#include "System/ReplaySessionActor.h"
#include "System/ReplayStorageSubsystem.h"

#define LOCTEXT_NAMESPACE "FReplayModuleModule"

DEFINE_LOG_CATEGORY(LogReplayModule);

namespace ReplayConsole
{
	/**
	 * The console commands below exist so a replay can be exercised without a win screen and
	 * without a Blueprint - during development, and for anyone verifying a build.
	 *
	 * All of them work on the first game world they can find, which is the PIE world while testing.
	 */
	static UWorld* FindGameWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}

		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && Context.World()->IsGameWorld())
			{
				return Context.World();
			}
		}

		return nullptr;
	}

	static void OpenReplay(const TArray<FString>& Args)
	{
		UWorld* World = FindGameWorld();
		if (!World)
		{
			UE_LOG(LogReplayModule, Warning, TEXT("Replay.Open: no running game world."));
			return;
		}

		// Stop the recording first, otherwise there is nothing published to play back yet.
		if (UReplayRecorderSubsystem* Recorder = UReplayRecorderSubsystem::Get(World))
		{
			if (Recorder->IsRecording())
			{
				Recorder->StopRecording();
				UE_LOG(LogReplayModule, Log, TEXT("Replay.Open: recording stopped and published."));
			}
		}

		if (!UReplayFunctionLibrary::OpenReplayWindow(World))
		{
			UE_LOG(LogReplayModule, Warning, TEXT("Replay.Open: the replay window could not be opened."));
		}
	}

	static void CloseReplay(const TArray<FString>& Args)
	{
		if (UWorld* World = FindGameWorld())
		{
			UReplayFunctionLibrary::CloseReplayWindow(World);
		}
	}

	static void ReplayStatus(const TArray<FString>& Args)
	{
		UWorld* World = FindGameWorld();
		if (!World)
		{
			UE_LOG(LogReplayModule, Display, TEXT("Replay.Status: no running game world."));
			return;
		}

		if (const UReplayRecorderSubsystem* Recorder = UReplayRecorderSubsystem::Get(World))
		{
			UE_LOG(LogReplayModule, Display, TEXT("Recording: %s (interval %.2f s)"),
				Recorder->IsRecording() ? TEXT("yes") : TEXT("no"), Recorder->GetRecordInterval());
		}

		if (const UReplayStorageSubsystem* Storage = UReplayStorageSubsystem::Get(World))
		{
			const FReplayInfo Info = Storage->GetReplayInfo();
			UE_LOG(LogReplayModule, Display,
				TEXT("Stored replay: %d frames, %.1f s, %d KB, viewport data: %s"),
				Info.FrameCount, Info.DurationSeconds, Info.ApproxMemoryKB,
				Info.bHasViewportData ? TEXT("yes") : TEXT("NO - minimap only"));
		}

		if (const UReplayPlaybackSubsystem* Playback = UReplayPlaybackSubsystem::Get(World))
		{
			UE_LOG(LogReplayModule, Display, TEXT("Viewport playback: %s, time %.1f s, speed %.1fx, paused: %s"),
				Playback->IsPlaybackActive() ? TEXT("running") : TEXT("stopped"),
				Playback->GetPlaybackTime(), Playback->GetSpeed(),
				Playback->IsPaused() ? TEXT("yes") : TEXT("no"));
		}
	}

	static void ReplaySeek(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogReplayModule, Display, TEXT("Usage: Replay.Seek <seconds>"));
			return;
		}

		if (UReplayPlaybackSubsystem* Playback = UReplayPlaybackSubsystem::Get(FindGameWorld()))
		{
			Playback->SeekToTime(FCString::Atof(*Args[0]));
			UE_LOG(LogReplayModule, Display, TEXT("Replay.Seek: now at %.1f s."), Playback->GetPlaybackTime());
		}
	}

	static void ReplaySpeed(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogReplayModule, Display, TEXT("Usage: Replay.Speed <factor>"));
			return;
		}

		if (UReplayPlaybackSubsystem* Playback = UReplayPlaybackSubsystem::Get(FindGameWorld()))
		{
			Playback->SetSpeed(FCString::Atof(*Args[0]));
			UE_LOG(LogReplayModule, Display, TEXT("Replay.Speed: now %.2fx (capped by MaxPlaybackSpeed)."),
				Playback->GetSpeed());
		}
	}

	static void ReplayPause(const TArray<FString>& Args)
	{
		if (UReplayPlaybackSubsystem* Playback = UReplayPlaybackSubsystem::Get(FindGameWorld()))
		{
			Playback->SetPaused(!Playback->IsPaused());
			UE_LOG(LogReplayModule, Display, TEXT("Replay.Pause: %s."),
				Playback->IsPaused() ? TEXT("paused") : TEXT("running"));
		}
	}

	static void CountProjectiles(const TArray<FString>& Args)
	{
		UWorld* World = FindGameWorld();
		if (!World)
		{
			UE_LOG(LogReplayModule, Display, TEXT("Replay.Projectiles: no running game world."));
			return;
		}

		// Runs the recorder's own query against the live world. That separates the two reasons a
		// replay can end up without shots: the query finds nothing, or nothing is shooting.
		FReplayRecording Scratch;
		TArray<FReplayProjectileState> States;
		ReplayRTS::GatherProjectiles(World, Scratch, 4096, States);

		UE_LOG(LogReplayModule, Display, TEXT("Replay.Projectiles: %d in flight right now (%d classes)."),
			States.Num(), Scratch.ClassTable.Num());
	}

	static void SaveReplay(const TArray<FString>& Args)
	{
		UWorld* World = FindGameWorld();
		UReplayStorageSubsystem* Storage = World ? UReplayStorageSubsystem::Get(World) : nullptr;
		if (!Storage)
		{
			UE_LOG(LogReplayModule, Display, TEXT("Replay.Save: no storage subsystem."));
			return;
		}

		// Save what is in memory - stopping the recording first, since a running one is not published.
		if (UReplayRecorderSubsystem* Recorder = UReplayRecorderSubsystem::Get(World))
		{
			if (Recorder->IsRecording())
			{
				Recorder->StopRecording();
			}
		}

		const FString Slot = Args.Num() > 0
			? (Storage->SaveReplayToSlot(Args[0]) ? Args[0] : FString())
			: Storage->SaveReplayToNewSlot();

		UE_LOG(LogReplayModule, Display, TEXT("Replay.Save: %s"),
			Slot.IsEmpty() ? TEXT("failed") : *FString::Printf(TEXT("saved as '%s'"), *Slot));
	}

	static void ListReplays(const TArray<FString>& Args)
	{
		UWorld* World = FindGameWorld();
		const UReplayStorageSubsystem* Storage = World ? UReplayStorageSubsystem::Get(World) : nullptr;
		if (!Storage)
		{
			UE_LOG(LogReplayModule, Display, TEXT("Replay.List: no storage subsystem."));
			return;
		}

		const TArray<FReplaySlotInfo> Slots = Storage->GetStoredReplays();
		UE_LOG(LogReplayModule, Display, TEXT("Replay.List: %d stored replay(s)."), Slots.Num());

		for (const FReplaySlotInfo& Slot : Slots)
		{
			UE_LOG(LogReplayModule, Display, TEXT("  %s | %s | %.1f s | %d frames | viewport: %s"),
				*Slot.SlotName, *Slot.MapName, Slot.DurationSeconds, Slot.FrameCount,
				Slot.bHasViewportData ? TEXT("yes") : TEXT("no"));
		}
	}

	static void LoadReplay(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogReplayModule, Display, TEXT("Usage: Replay.Load <slotname>"));
			return;
		}

		UWorld* World = FindGameWorld();
		UReplayStorageSubsystem* Storage = World ? UReplayStorageSubsystem::Get(World) : nullptr;
		if (!Storage)
		{
			return;
		}

		if (!Storage->LoadReplayFromSlot(Args[0]))
		{
			UE_LOG(LogReplayModule, Display, TEXT("Replay.Load: '%s' could not be loaded."), *Args[0]);
			return;
		}

		UE_LOG(LogReplayModule, Display, TEXT("Replay.Load: '%s' loaded."), *Args[0]);
		UReplayFunctionLibrary::OpenReplayWindow(World);
	}

	static void OpenBrowser(const TArray<FString>& Args)
	{
		if (UWorld* World = FindGameWorld())
		{
			UReplayFunctionLibrary::OpenReplayBrowser(World);
		}
	}

	static void ShareReplay(const TArray<FString>& Args)
	{
		UWorld* World = FindGameWorld();
		if (!World)
		{
			return;
		}

		if (UReplayRecorderSubsystem* Recorder = UReplayRecorderSubsystem::Get(World))
		{
			if (Recorder->IsRecording())
			{
				Recorder->StopRecording();
			}
		}

		AReplaySessionActor* Session = AReplaySessionActor::GetOrCreate(World);
		if (!Session)
		{
			UE_LOG(LogReplayModule, Display, TEXT("Replay.Share: only the server can share a replay."));
			return;
		}

		const bool bStarted = Session->BroadcastRecording();
		UE_LOG(LogReplayModule, Display, TEXT("Replay.Share: %s"),
			bStarted ? TEXT("transfer started") : TEXT("nothing to send"));
	}

	static void WatchTogether(const TArray<FString>& Args)
	{
		if (AReplaySessionActor* Session = AReplaySessionActor::GetOrCreate(FindGameWorld()))
		{
			Session->StartSharedPlayback();
			UE_LOG(LogReplayModule, Display, TEXT("Replay.WatchTogether: shared playback started."));
		}
		else
		{
			UE_LOG(LogReplayModule, Display, TEXT("Replay.WatchTogether: server only."));
		}
	}

	static void SessionStatus(const TArray<FString>& Args)
	{
		AReplaySessionActor* Session = AReplaySessionActor::Find(FindGameWorld());
		if (!Session)
		{
			UE_LOG(LogReplayModule, Display, TEXT("Replay.Session: no session actor in this world."));
			return;
		}

		UE_LOG(LogReplayModule, Display,
			TEXT("Replay.Session: %s, shared playback: %s, transfer: %s (%.0f %%)"),
			Session->HasAuthority() ? TEXT("server") : TEXT("client"),
			Session->IsSharedPlaybackActive() ? TEXT("on") : TEXT("off"),
			Session->IsTransferInProgress() ? TEXT("running") : TEXT("idle"),
			Session->GetTransferProgress() * 100.f);
	}

	static void AnimStates(const TArray<FString>& Args)
	{
		if (UReplayPlaybackSubsystem* Playback = UReplayPlaybackSubsystem::Get(FindGameWorld()))
		{
			Playback->LogAnimStateBreakdown();
		}
	}

	static FAutoConsoleCommand CmdAnimStates(
		TEXT("Replay.AnimStates"),
		TEXT("Lists the recorded animation states against what the proxies are showing."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&AnimStates));

	static FAutoConsoleCommand CmdShare(
		TEXT("Replay.Share"),
		TEXT("Server: sends the recording to every connected client."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ShareReplay));

	static FAutoConsoleCommand CmdWatchTogether(
		TEXT("Replay.WatchTogether"),
		TEXT("Server: starts a shared viewing - everyone watches the same moment."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&WatchTogether));

	static FAutoConsoleCommand CmdSession(
		TEXT("Replay.Session"),
		TEXT("Prints the state of the shared replay session."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&SessionStatus));

	static FAutoConsoleCommand CmdSave(
		TEXT("Replay.Save"),
		TEXT("Replay.Save [slotname] - saves the current replay; without a name one is generated."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&SaveReplay));

	static FAutoConsoleCommand CmdList(
		TEXT("Replay.List"),
		TEXT("Lists every stored replay."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ListReplays));

	static FAutoConsoleCommand CmdLoad(
		TEXT("Replay.Load"),
		TEXT("Replay.Load <slotname> - loads a stored replay and opens the replay window."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&LoadReplay));

	static FAutoConsoleCommand CmdBrowser(
		TEXT("Replay.Browser"),
		TEXT("Opens the replay browser to pick a stored replay."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&OpenBrowser));

	static FAutoConsoleCommand CmdProjectiles(
		TEXT("Replay.Projectiles"),
		TEXT("Counts the projectiles currently in flight, using the recorder's own query."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&CountProjectiles));

	static FAutoConsoleCommand CmdOpen(
		TEXT("Replay.Open"),
		TEXT("Stops the running recording and opens the replay window."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&OpenReplay));

	static FAutoConsoleCommand CmdClose(
		TEXT("Replay.Close"),
		TEXT("Closes the replay window and restores the level."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&CloseReplay));

	static FAutoConsoleCommand CmdStatus(
		TEXT("Replay.Status"),
		TEXT("Prints recording and playback state."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ReplayStatus));

	static FAutoConsoleCommand CmdSeek(
		TEXT("Replay.Seek"),
		TEXT("Replay.Seek <seconds> - jumps to a point in the replay."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ReplaySeek));

	static FAutoConsoleCommand CmdSpeed(
		TEXT("Replay.Speed"),
		TEXT("Replay.Speed <factor> - sets playback speed, capped at MaxPlaybackSpeed."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ReplaySpeed));

	static FAutoConsoleCommand CmdPause(
		TEXT("Replay.Pause"),
		TEXT("Toggles pause on the running replay."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ReplayPause));
}

void FReplayModuleModule::StartupModule()
{
}

void FReplayModuleModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FReplayModuleModule, ReplayModule)
