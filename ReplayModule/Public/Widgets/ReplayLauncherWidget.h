// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ReplayLauncherWidget.generated.h"

class UButton;
class UMaterialInterface;
class UTextBlock;

/**
 * The small "Watch Replay" button that appears once a match is over.
 *
 * It is a separate widget on purpose: the win/lose screen of an RTS project is a Blueprint that
 * belongs to that project, and dropping a button next to it needs no change there at all.
 */
UCLASS(Blueprintable, BlueprintType)
class REPLAYMODULE_API UReplayLauncherWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Style")
	FText ButtonLabel;

	/** Optional material for the button's panel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Style")
	TObjectPtr<UMaterialInterface> BackdropMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Style")
	FLinearColor BackdropColor = FLinearColor(0.f, 0.f, 0.f, 0.6f);

	/** Where on screen the fallback layout puts itself, in normalized screen space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay|Style")
	FVector2D ScreenAnchor = FVector2D(0.5f, 0.9f);

	/** Take the button away once the replay window is open. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
	bool bHideWhenReplayOpens = true;

	UFUNCTION(BlueprintCallable, Category = "Replay")
	void OpenReplay();

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;

	void BuildFallbackLayout();

	UFUNCTION()
	void HandleOpenClicked();

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Replay|Widgets")
	TObjectPtr<UButton> OpenReplayButton = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Replay|Widgets")
	TObjectPtr<UTextBlock> OpenReplayText = nullptr;
};
