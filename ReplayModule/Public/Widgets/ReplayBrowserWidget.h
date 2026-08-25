// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Save/ReplaySaveGame.h"
#include "ReplayBrowserWidget.generated.h"

class UButton;
class UScrollBox;
class UTextBlock;
class UVerticalBox;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnReplayChosen, const FString&, SlotName);

/**
 * Picks a stored replay to watch.
 *
 * Like UReplayWidget this builds a plain layout when no Blueprint subclass supplies one, so the
 * module stays usable without any content work. A Blueprint subclass that names its widgets the
 * same way (ListBox, TitleText, CloseButton) keeps its own design and only inherits the behaviour.
 */
UCLASS()
class REPLAYMODULE_API UReplayBrowserWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Fires with the chosen slot name; the widget closes itself right after. */
	UPROPERTY(BlueprintAssignable, Category = "Replay|Browser")
	FOnReplayChosen OnReplayChosen;

	/** Reads the index and rebuilds the rows. Called on construct and after a deletion. */
	UFUNCTION(BlueprintCallable, Category = "Replay|Browser")
	void RefreshList();

	/**
	 * Loads the replay and opens the replay window on it. False when the slot is gone or holds a
	 * recording this build cannot read.
	 */
	UFUNCTION(BlueprintCallable, Category = "Replay|Browser")
	bool LoadAndPlay(const FString& SlotName);

	UFUNCTION(BlueprintCallable, Category = "Replay|Browser")
	void CloseBrowser();

	UFUNCTION(BlueprintPure, Category = "Replay|Browser")
	int32 GetEntryCount() const { return Entries.Num(); }

	/** Which save-game user index to read. Matches the one used when saving. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Browser")
	int32 UserIndex = 0;

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;

	/** Builds a usable window when no Blueprint subclass provided a widget tree. */
	void BuildFallbackLayout();

	/** One row per stored replay, with a button that loads it. */
	void BuildRows();

	UFUNCTION()
	void HandleCloseClicked();

	/** Row buttons cannot carry a payload, so the slot name is looked up by button. */
	UFUNCTION()
	void HandleRowClicked();

	UPROPERTY(BlueprintReadOnly, Category = "Replay|Widgets", meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UScrollBox> ListBox = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Replay|Widgets", meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> TitleText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Replay|Widgets", meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UButton> CloseButton = nullptr;

private:
	UPROPERTY()
	TArray<FReplaySlotInfo> Entries;

	/** Row button to slot name. UButton::OnClicked carries no payload, so the mapping lives here. */
	UPROPERTY()
	TMap<TObjectPtr<UButton>, FString> RowSlots;
};
