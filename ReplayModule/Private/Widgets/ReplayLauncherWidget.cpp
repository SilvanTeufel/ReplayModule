// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Widgets/ReplayLauncherWidget.h"

#include "Blueprint/ReplayFunctionLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "ReplayModule.h"

#define LOCTEXT_NAMESPACE "ReplayLauncherWidget"

bool UReplayLauncherWidget::Initialize()
{
	const bool bFirstInit = Super::Initialize();

	// Same reason as in UReplayWidget: after RebuildWidget a late widget tree is ignored.
	BuildFallbackLayout();

	return bFirstInit;
}

void UReplayLauncherWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (ButtonLabel.IsEmpty())
	{
		ButtonLabel = LOCTEXT("WatchReplay", "Watch Replay");
	}

	if (OpenReplayText)
	{
		OpenReplayText->SetText(ButtonLabel);
	}

	if (OpenReplayButton)
	{
		OpenReplayButton->OnClicked.AddUniqueDynamic(this, &UReplayLauncherWidget::HandleOpenClicked);
	}
}

void UReplayLauncherWidget::OpenReplay()
{
	if (UReplayFunctionLibrary::OpenReplayWindow(this) && bHideWhenReplayOpens)
	{
		RemoveFromParent();
	}
}

void UReplayLauncherWidget::HandleOpenClicked()
{
	OpenReplay();
}

void UReplayLauncherWidget::BuildFallbackLayout()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	// A canvas rather than a border, so the button can sit anywhere on screen without covering the
	// win/lose screen underneath it.
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("LauncherCanvas"));
	if (!Canvas)
	{
		UE_LOG(LogReplayModule, Warning, TEXT("%s: could not build the fallback launcher layout."), *GetName());
		return;
	}

	WidgetTree->RootWidget = Canvas;

	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LauncherBorder"));
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("OpenReplayButton"));
	UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("OpenReplayText"));

	if (!Backdrop || !Button || !Label)
	{
		return;
	}

	if (BackdropMaterial)
	{
		Backdrop->SetBrushFromMaterial(BackdropMaterial);
	}
	else
	{
		Backdrop->SetBrushColor(BackdropColor);
	}

	Backdrop->SetPadding(FMargin(10.f, 6.f));

	Label->SetText(ButtonLabel.IsEmpty() ? LOCTEXT("WatchReplay", "Watch Replay") : ButtonLabel);
	Label->SetJustification(ETextJustify::Center);

	Button->SetContent(Label);
	Backdrop->SetContent(Button);

	if (UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(Backdrop))
	{
		CanvasSlot->SetAnchors(FAnchors(ScreenAnchor.X, ScreenAnchor.Y));
		CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		CanvasSlot->SetAutoSize(true);
	}

	OpenReplayButton = Button;
	OpenReplayText = Label;
}

#undef LOCTEXT_NAMESPACE
