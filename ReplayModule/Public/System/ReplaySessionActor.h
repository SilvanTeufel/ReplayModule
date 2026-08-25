// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ReplaySessionActor.generated.h"

/**
 * Shares one recording across a session, so several players can watch the same replay together.
 *
 * Two jobs, both of which have to exist for "watch together" to mean anything:
 *
 *  - Transfer. The server serialises its recording once and pushes it to a client in chunks. A
 *    recording is hundreds of kilobytes; a single RPC cannot carry that, and flooding the channel
 *    with all chunks in one tick stalls everything else on the connection. So chunks go out at a
 *    metered rate.
 *
 *  - Control. Time, speed and pause are replicated, so when the host seeks, everyone seeks. Clients
 *    ask the server rather than acting locally, which keeps a single source of truth - two viewers
 *    scrubbing independently is exactly what this is meant to prevent.
 *
 * A client that never receives anything is not broken: it recorded the match itself and can watch
 * its own copy. The transfer only matters when everyone should see the *same* recording.
 */
UCLASS()
class REPLAYMODULE_API AReplaySessionActor : public AActor
{
	GENERATED_BODY()

public:
	AReplaySessionActor();

	/** Finds the session actor, spawning it on the server if it does not exist yet. */
	static AReplaySessionActor* GetOrCreate(const UObject* WorldContextObject);

	/** Finds it without ever spawning - safe on clients. */
	static AReplaySessionActor* Find(const UObject* WorldContextObject);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	// --- Transfer (server side) ---

	/**
	 * Serialises the server's recording and starts pushing it to every connected client.
	 * False when there is nothing to send or this is not the server.
	 */
	UFUNCTION(BlueprintCallable, Category = "Replay|Session")
	bool BroadcastRecording();

	UFUNCTION(BlueprintPure, Category = "Replay|Session")
	bool IsTransferInProgress() const { return bTransferActive; }

	/** 0..1 over the outgoing transfer, for a progress readout. */
	UFUNCTION(BlueprintPure, Category = "Replay|Session")
	float GetTransferProgress() const;

	// --- Shared control ---

	/**
	 * Asks the server to move the shared playhead. On the server this applies straight away; on a
	 * client it goes through a server RPC, so everyone stays on the same moment.
	 */
	UFUNCTION(BlueprintCallable, Category = "Replay|Session")
	void RequestSeek(float TimeSeconds);

	UFUNCTION(BlueprintCallable, Category = "Replay|Session")
	void RequestSpeed(float NewSpeed);

	UFUNCTION(BlueprintCallable, Category = "Replay|Session")
	void RequestPaused(bool bNewPaused);

	/** True while this actor is driving playback, i.e. a shared session is running. */
	UFUNCTION(BlueprintPure, Category = "Replay|Session")
	bool IsSharedPlaybackActive() const { return bSharedPlayback; }

	/** Starts (server) a shared viewing: every client is told to begin playback. */
	UFUNCTION(BlueprintCallable, Category = "Replay|Session")
	void StartSharedPlayback();

	UFUNCTION(BlueprintCallable, Category = "Replay|Session")
	void StopSharedPlayback();

protected:
	virtual void BeginPlay() override;

	UFUNCTION(Server, Reliable)
	void ServerRequestSeek(float TimeSeconds);

	UFUNCTION(Server, Reliable)
	void ServerRequestSpeed(float NewSpeed);

	UFUNCTION(Server, Reliable)
	void ServerRequestPaused(bool bNewPaused);

	/** One slice of the serialised recording. Reliable, so the stream cannot lose a chunk. */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastRecordingChunk(int32 ChunkIndex, int32 ChunkCount, const TArray<uint8>& Payload);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastSharedPlayback(bool bStart);

	UFUNCTION()
	void OnRep_Control();

	/** Rebuilt on the client once every chunk has arrived. */
	void FinishReceive();

	/** Pushes the replicated control state onto the local playback subsystem. */
	void ApplyControlLocally();

	// --- Replicated control state ---

	/** Where the shared playhead is. Only meaningful while bSharedPlayback is true. */
	UPROPERTY(ReplicatedUsing = OnRep_Control)
	float ControlTime = 0.f;

	UPROPERTY(ReplicatedUsing = OnRep_Control)
	float ControlSpeed = 1.f;

	UPROPERTY(ReplicatedUsing = OnRep_Control)
	bool bControlPaused = false;

	UPROPERTY(ReplicatedUsing = OnRep_Control)
	bool bSharedPlayback = false;

private:
	/** Serialised recording waiting to go out, server side. */
	TArray<uint8> OutgoingBytes;

	int32 NextChunkToSend = 0;
	int32 OutgoingChunkCount = 0;
	bool bTransferActive = false;

	/** Chunks received so far, client side. */
	TArray<uint8> IncomingBytes;
	int32 ExpectedChunkCount = 0;
	int32 ReceivedChunkCount = 0;

	/** Last shared state that was logged, so a repeated replication does not spam the log. */
	bool bLastLoggedShared = false;

	/** How often the server resyncs the playhead while playing, in seconds. */
	float ResyncInterval = 0.5f;
	float TimeSinceResync = 0.f;
};
