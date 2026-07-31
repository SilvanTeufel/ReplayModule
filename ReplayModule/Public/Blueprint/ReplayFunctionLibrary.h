// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Data/ReplayFrameData.h"
#include "ReplayFunctionLibrary.generated.h"

class UReplayWidget;

/**
 * The whole plugin in a handful of Blueprint nodes, for projects that would rather not reach into
 * the subsystems directly.
 */
UCLASS()
class REPLAYMODULE_API UReplayFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Opens the replay window on the local player. Returns the widget, or null when there is
	 * nothing recorded. Opening it twice brings the existing window forward instead of stacking.
	 */
	UFUNCTION(BlueprintCallable, Category = "Replay", meta = (WorldContext = "WorldContextObject"))
	static UReplayWidget* OpenReplayWindow(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "Replay", meta = (WorldContext = "WorldContextObject"))
	static void CloseReplayWindow(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "Replay", meta = (WorldContext = "WorldContextObject"))
	static bool HasReplay(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "Replay", meta = (WorldContext = "WorldContextObject"))
	static FReplayInfo GetReplayInfo(const UObject* WorldContextObject);

	/**
	 * Tell the plugin the match is over. Only needed when automatic detection is off or the project
	 * does not use RTSUnitTemplate's win/lose screen.
	 */
	UFUNCTION(BlueprintCallable, Category = "Replay", meta = (WorldContext = "WorldContextObject"))
	static void NotifyGameEnded(const UObject* WorldContextObject);

	/** Puts the "Watch Replay" button on screen by hand. */
	UFUNCTION(BlueprintCallable, Category = "Replay", meta = (WorldContext = "WorldContextObject"))
	static void ShowReplayLauncher(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "Replay|Recording", meta = (WorldContext = "WorldContextObject"))
	static void StartReplayRecording(const UObject* WorldContextObject, FVector2D WorldMin, FVector2D WorldMax);

	UFUNCTION(BlueprintCallable, Category = "Replay|Recording", meta = (WorldContext = "WorldContextObject"))
	static void StopReplayRecording(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "Replay|Recording", meta = (WorldContext = "WorldContextObject"))
	static bool IsReplayRecording(const UObject* WorldContextObject);

	/** Seconds between recorded frames. Takes effect immediately. */
	UFUNCTION(BlueprintCallable, Category = "Replay|Recording", meta = (WorldContext = "WorldContextObject"))
	static void SetReplayRecordInterval(const UObject* WorldContextObject, float Seconds);

	UFUNCTION(BlueprintCallable, Category = "Replay|Persistence", meta = (WorldContext = "WorldContextObject"))
	static bool SaveReplayToSlot(const UObject* WorldContextObject, const FString& SlotName, int32 UserIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "Replay|Persistence", meta = (WorldContext = "WorldContextObject"))
	static bool LoadReplayFromSlot(const UObject* WorldContextObject, const FString& SlotName, int32 UserIndex = 0);
};
