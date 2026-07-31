// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Data/ReplayFrameData.h"
#include "ReplayStorageSubsystem.generated.h"

class UTexture2D;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnReplayAvailable);

/**
 * Owns the finished recording.
 *
 * It sits on the GameInstance on purpose: a win/lose screen usually ends in a ServerTravel, and the
 * replay has to outlive that. The recording itself is held through a shared pointer so the renderer
 * can hold on to it without copying megabytes or risking a dangling reference.
 */
UCLASS()
class REPLAYMODULE_API UReplayStorageSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Convenience accessor that tolerates a null or partially torn down world. */
	static UReplayStorageSubsystem* Get(const UObject* WorldContextObject);

	virtual void Deinitialize() override;

	/** Fires once a recording becomes available - the launcher button listens on this. */
	UPROPERTY(BlueprintAssignable, Category = "Replay")
	FOnReplayAvailable OnReplayAvailable;

	UFUNCTION(BlueprintPure, Category = "Replay")
	bool HasReplay() const;

	UFUNCTION(BlueprintPure, Category = "Replay")
	FReplayInfo GetReplayInfo() const;

	/** Shared handle for the renderer. Null while nothing has been recorded. */
	TSharedPtr<const FReplayRecording, ESPMode::ThreadSafe> GetRecording() const;

	/** Takes ownership of a finished recording and notifies listeners. */
	void SetRecording(TSharedPtr<FReplayRecording, ESPMode::ThreadSafe> InRecording);

	UFUNCTION(BlueprintCallable, Category = "Replay")
	void ClearReplay();

	/**
	 * Terrain layer drawn underneath the markers. With RTSUnitTemplate this points straight at the
	 * minimap's topography texture, which costs nothing and stays valid because that plugin roots
	 * it. Cleared together with the recording.
	 */
	UFUNCTION(BlueprintCallable, Category = "Replay")
	void SetBackgroundTexture(UTexture2D* InTexture);

	UFUNCTION(BlueprintPure, Category = "Replay")
	UTexture2D* GetBackgroundTexture() const;

	UFUNCTION(BlueprintCallable, Category = "Replay|Persistence")
	bool SaveReplayToSlot(const FString& SlotName, int32 UserIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "Replay|Persistence")
	bool LoadReplayFromSlot(const FString& SlotName, int32 UserIndex = 0);

private:
	TSharedPtr<FReplayRecording, ESPMode::ThreadSafe> Recording;

	UPROPERTY()
	TObjectPtr<UTexture2D> BackgroundTexture = nullptr;
};
