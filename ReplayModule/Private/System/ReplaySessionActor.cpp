// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "System/ReplaySessionActor.h"

#include "Blueprint/ReplayFunctionLibrary.h"
#include "Data/ReplayFrameData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "ReplayModule.h"
// FSoftObjectPath cannot go through a plain FArchive - it asserts outright. The proxy archive
// below writes object references as strings and is what the engine's own save games use.
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/ArchiveLoadCompressedProxy.h"
#include "Serialization/ArchiveSaveCompressedProxy.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "System/ReplayPlaybackSubsystem.h"
#include "System/ReplayStorageSubsystem.h"

namespace ReplaySession
{
	/**
	 * Bytes per chunk.
	 *
	 * Well under the reliable-RPC ceiling: a single RPC cannot carry a whole recording, and going
	 * close to the limit makes delivery fragile on a busy connection. 16 KB is a compromise between
	 * per-RPC overhead and how much of one tick's bandwidth a single chunk claims.
	 */
	static constexpr int32 ChunkSize = 16 * 1024;

	/** Chunks pushed per tick. Sending them all at once stalls everything else on the channel. */
	static constexpr int32 ChunksPerTick = 4;
}

AReplaySessionActor::AReplaySessionActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.f;

	bReplicates = true;
	bAlwaysRelevant = true;          // everyone watching has to hear about it, wherever they are
	SetReplicateMovement(false);
	// Control state, not movement - no need for a fast channel.
	SetNetUpdateFrequency(10.f);
}

void AReplaySessionActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AReplaySessionActor, ControlTime);
	DOREPLIFETIME(AReplaySessionActor, ControlSpeed);
	DOREPLIFETIME(AReplaySessionActor, bControlPaused);
	DOREPLIFETIME(AReplaySessionActor, bSharedPlayback);
}

void AReplaySessionActor::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogReplayModule, Log, TEXT("Replay session actor up (%s)."),
		HasAuthority() ? TEXT("server") : TEXT("client"));
}

AReplaySessionActor* AReplaySessionActor::Find(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AReplaySessionActor> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (IsValid(*It))
		{
			return *It;
		}
	}

	return nullptr;
}

AReplaySessionActor* AReplaySessionActor::GetOrCreate(const UObject* WorldContextObject)
{
	if (AReplaySessionActor* Existing = Find(WorldContextObject))
	{
		return Existing;
	}

	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return nullptr;
	}

	// Only the server may create it - a client spawning its own would produce a second, unreplicated
	// actor that silently does nothing.
	if (World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	return World->SpawnActor<AReplaySessionActor>(AReplaySessionActor::StaticClass(), FTransform::Identity, Params);
}

float AReplaySessionActor::GetTransferProgress() const
{
	if (HasAuthority())
	{
		return (OutgoingChunkCount > 0)
			? FMath::Clamp(static_cast<float>(NextChunkToSend) / OutgoingChunkCount, 0.f, 1.f)
			: 0.f;
	}

	return (ExpectedChunkCount > 0)
		? FMath::Clamp(static_cast<float>(ReceivedChunkCount) / ExpectedChunkCount, 0.f, 1.f)
		: 0.f;
}

bool AReplaySessionActor::BroadcastRecording()
{
	if (!HasAuthority())
	{
		UE_LOG(LogReplayModule, Warning, TEXT("BroadcastRecording is server-only."));
		return false;
	}

	UReplayStorageSubsystem* Storage = UReplayStorageSubsystem::Get(this);
	if (!Storage || !Storage->HasReplay())
	{
		UE_LOG(LogReplayModule, Warning, TEXT("BroadcastRecording: the server has no recording."));
		return false;
	}

	TSharedPtr<const FReplayRecording, ESPMode::ThreadSafe> Recording = Storage->GetRecording();
	if (!Recording.IsValid())
	{
		return false;
	}

	OutgoingBytes.Reset();
	FMemoryWriter Writer(OutgoingBytes, /*bIsPersistent=*/true);

	// The class table holds FSoftClassPath entries, and a bare FArchive refuses those with a fatal
	// error. Wrapping the writer turns object references into strings - the same route the engine
	// takes for save games, which is why saving to a slot already worked while this did not.
	FObjectAndNameAsStringProxyArchive Archive(Writer, /*bInLoadIfFindFails=*/true);

	// NOT ArIsSaveGame: that flag limits serialisation to properties carrying CPF_SaveGame, and
	// FReplayRecording marks none - it produced a 9-byte "recording". ArNoDelta forces every property
	// out even when it matches its default, which matters because there is no default object to diff
	// against on the receiving side.
	Archive.ArNoDelta = true;

	// SerializeItem on the struct's reflection data, so the wire format follows FReplayRecording
	// automatically instead of a hand-written serialiser that drifts out of sync with it.
	FReplayRecording Copy = *Recording;
	FReplayRecording::StaticStruct()->SerializeItem(Archive, &Copy, nullptr);

	// Compress before sending. Tagged-property serialisation repeats every property name for every
	// element, so the raw stream is many times the size of the same recording in a save file - most of
	// it identical text, which is exactly what zlib is good at. Without this a few minutes of match
	// take megabytes off the connection.
	const int32 RawSize = OutgoingBytes.Num();
	{
		TArray<uint8> Compressed;
		FArchiveSaveCompressedProxy Compressor(Compressed, NAME_Zlib);
		Compressor << OutgoingBytes;
		Compressor.Flush();

		OutgoingBytes = MoveTemp(Compressed);
	}

	OutgoingChunkCount = FMath::DivideAndRoundUp(OutgoingBytes.Num(), ReplaySession::ChunkSize);
	NextChunkToSend = 0;
	bTransferActive = OutgoingChunkCount > 0;

	UE_LOG(LogReplayModule, Log, TEXT("Broadcasting the recording: %d bytes raw, %d compressed, %d chunks."),
		RawSize, OutgoingBytes.Num(), OutgoingChunkCount);

	return bTransferActive;
}

void AReplaySessionActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority())
	{
		return;
	}

	// Metered push of the outgoing recording.
	if (bTransferActive)
	{
		for (int32 i = 0; i < ReplaySession::ChunksPerTick && NextChunkToSend < OutgoingChunkCount; ++i)
		{
			const int32 Offset = NextChunkToSend * ReplaySession::ChunkSize;
			const int32 Size = FMath::Min(ReplaySession::ChunkSize, OutgoingBytes.Num() - Offset);

			TArray<uint8> Payload;
			Payload.Append(OutgoingBytes.GetData() + Offset, Size);

			MulticastRecordingChunk(NextChunkToSend, OutgoingChunkCount, Payload);
			++NextChunkToSend;
		}

		if (NextChunkToSend >= OutgoingChunkCount)
		{
			bTransferActive = false;
			UE_LOG(LogReplayModule, Log, TEXT("Recording transfer finished (%d chunks)."), OutgoingChunkCount);
		}
	}

	// Keep the shared playhead honest. Clients run their own clock between updates so playback stays
	// smooth; without a periodic correction they drift apart over a long replay.
	if (bSharedPlayback)
	{
		TimeSinceResync += DeltaSeconds;
		if (TimeSinceResync >= ResyncInterval)
		{
			TimeSinceResync = 0.f;

			if (const UReplayPlaybackSubsystem* Playback = UReplayPlaybackSubsystem::Get(this))
			{
				ControlTime = Playback->GetPlaybackTime();
			}
		}
	}
}

void AReplaySessionActor::MulticastRecordingChunk_Implementation(int32 ChunkIndex, int32 ChunkCount, const TArray<uint8>& Payload)
{
	// The server already has the recording; re-assembling its own broadcast would just overwrite it
	// with an identical copy.
	if (HasAuthority())
	{
		return;
	}

	if (ChunkIndex == 0)
	{
		IncomingBytes.Reset();
		ExpectedChunkCount = ChunkCount;
		ReceivedChunkCount = 0;
	}

	IncomingBytes.Append(Payload);
	++ReceivedChunkCount;

	if (ReceivedChunkCount >= ExpectedChunkCount && ExpectedChunkCount > 0)
	{
		FinishReceive();
	}
}

void AReplaySessionActor::FinishReceive()
{
	UReplayStorageSubsystem* Storage = UReplayStorageSubsystem::Get(this);
	if (!Storage)
	{
		return;
	}

	TSharedPtr<FReplayRecording, ESPMode::ThreadSafe> Received = MakeShared<FReplayRecording, ESPMode::ThreadSafe>();

	TArray<uint8> Raw;
	{
		FArchiveLoadCompressedProxy Decompressor(IncomingBytes, NAME_Zlib);
		if (Decompressor.GetError())
		{
			UE_LOG(LogReplayModule, Warning, TEXT("The received recording could not be decompressed (%d bytes)."),
				IncomingBytes.Num());
			IncomingBytes.Reset();
			return;
		}

		Decompressor << Raw;
	}

	FMemoryReader Reader(Raw, /*bIsPersistent=*/true);
	FObjectAndNameAsStringProxyArchive Archive(Reader, /*bInLoadIfFindFails=*/true);
	Archive.ArNoDelta = true;
	FReplayRecording::StaticStruct()->SerializeItem(Archive, Received.Get(), nullptr);

	if (!Received->IsValid())
	{
		UE_LOG(LogReplayModule, Warning, TEXT("The received recording is unusable (%d bytes)."), IncomingBytes.Num());
		IncomingBytes.Reset();
		return;
	}

	Storage->SetRecording(Received);

	UE_LOG(LogReplayModule, Log, TEXT("Recording received: %d frames, %.1f s (%d bytes compressed, %d raw)."),
		Received->Frames.Num(), Received->GetDurationSeconds(), IncomingBytes.Num(), Raw.Num());

	IncomingBytes.Reset();
	ExpectedChunkCount = 0;
	ReceivedChunkCount = 0;
}

void AReplaySessionActor::StartSharedPlayback()
{
	if (!HasAuthority())
	{
		return;
	}

	bSharedPlayback = true;
	bControlPaused = false;
	ControlTime = 0.f;

	MulticastSharedPlayback(true);
	ApplyControlLocally();
}

void AReplaySessionActor::StopSharedPlayback()
{
	if (!HasAuthority())
	{
		return;
	}

	bSharedPlayback = false;
	MulticastSharedPlayback(false);
	ApplyControlLocally();
}

void AReplaySessionActor::MulticastSharedPlayback_Implementation(bool bStart)
{
	const TCHAR* Side = HasAuthority() ? TEXT("server") : TEXT("client");

	UReplayPlaybackSubsystem* Playback = UReplayPlaybackSubsystem::Get(this);
	if (!Playback)
	{
		UE_LOG(LogReplayModule, Warning, TEXT("Shared playback (%s): no playback subsystem in this world."), Side);
		return;
	}

	const UReplayStorageSubsystem* Storage = UReplayStorageSubsystem::Get(this);
	UE_LOG(LogReplayModule, Log, TEXT("Shared playback (%s): %s, recording present: %s"),
		Side, bStart ? TEXT("start") : TEXT("stop"),
		(Storage && Storage->HasReplay()) ? TEXT("yes") : TEXT("NO"));

	if (bStart)
	{
		// Opening the window rather than only starting the subsystem, so every viewer also gets the
		// timeline and the minimap - otherwise the guests would watch without any controls.
		UReplayFunctionLibrary::OpenReplayWindow(this);
	}
	else
	{
		Playback->EndPlayback();
	}
}

void AReplaySessionActor::RequestSeek(float TimeSeconds)
{
	if (HasAuthority())
	{
		ControlTime = TimeSeconds;
		ApplyControlLocally();
	}
	else
	{
		ServerRequestSeek(TimeSeconds);
	}
}

void AReplaySessionActor::ServerRequestSeek_Implementation(float TimeSeconds)
{
	ControlTime = TimeSeconds;
	ApplyControlLocally();
}

void AReplaySessionActor::RequestSpeed(float NewSpeed)
{
	if (HasAuthority())
	{
		ControlSpeed = NewSpeed;
		ApplyControlLocally();
	}
	else
	{
		ServerRequestSpeed(NewSpeed);
	}
}

void AReplaySessionActor::ServerRequestSpeed_Implementation(float NewSpeed)
{
	ControlSpeed = NewSpeed;
	ApplyControlLocally();
}

void AReplaySessionActor::RequestPaused(bool bNewPaused)
{
	if (HasAuthority())
	{
		bControlPaused = bNewPaused;
		ApplyControlLocally();
	}
	else
	{
		ServerRequestPaused(bNewPaused);
	}
}

void AReplaySessionActor::ServerRequestPaused_Implementation(bool bNewPaused)
{
	bControlPaused = bNewPaused;
	ApplyControlLocally();
}

void AReplaySessionActor::OnRep_Control()
{
	// Starting playback from the replicated STATE rather than from the multicast event.
	//
	// The multicast reached the server but not the client, while the chunk multicasts sent moments
	// earlier arrived fine - so an event can be missed. Replicated state cannot: whatever the reason
	// for a dropped RPC, bSharedPlayback still arrives, and it also covers a client that joins after
	// the session has already started.
	UReplayPlaybackSubsystem* Playback = UReplayPlaybackSubsystem::Get(this);
	if (!Playback)
	{
		return;
	}

	// Only on a real change. Control state replicates a couple of times a second, and logging every
	// arrival buried everything else in the log.
	if (bSharedPlayback != bLastLoggedShared)
	{
		bLastLoggedShared = bSharedPlayback;
		UE_LOG(LogReplayModule, Log, TEXT("Shared state on the client: playback %s, local playback %s."),
			bSharedPlayback ? TEXT("on") : TEXT("off"),
			Playback->IsPlaybackActive() ? TEXT("running") : TEXT("stopped"));
	}

	if (bSharedPlayback && !Playback->IsPlaybackActive())
	{
		UReplayFunctionLibrary::OpenReplayWindow(this);
	}
	else if (!bSharedPlayback && Playback->IsPlaybackActive())
	{
		Playback->EndPlayback();
	}

	ApplyControlLocally();
}

void AReplaySessionActor::ApplyControlLocally()
{
	UReplayPlaybackSubsystem* Playback = UReplayPlaybackSubsystem::Get(this);
	if (!Playback || !bSharedPlayback)
	{
		return;
	}

	Playback->SetSpeed(ControlSpeed);
	Playback->SetPaused(bControlPaused);

	// Only correct the playhead when it has genuinely drifted. Seeking on every replication would
	// re-seat every proxy several times a second, which looks like stuttering and undoes the
	// interpolation the playback does between frames.
	const float Drift = FMath::Abs(Playback->GetPlaybackTime() - ControlTime);
	if (Drift > 0.75f)
	{
		Playback->SeekToTime(ControlTime);
	}
}
