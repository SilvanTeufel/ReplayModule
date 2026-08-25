// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Widgets/ReplayBrowserWidget.h"

#include "Blueprint/ReplayFunctionLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "ReplayModule.h"
#include "System/ReplayStorageSubsystem.h"

#define LOCTEXT_NAMESPACE "ReplayBrowserWidget"

namespace ReplayBrowserLayout
{
	static const FMargin PanelPadding(16.f);
	static const FMargin RowPadding(8.f, 6.f);
	static constexpr float RowSpacing = 4.f;
	static constexpr float ListHeight = 420.f;
}

bool UReplayBrowserWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	BuildFallbackLayout();
	return true;
}

void UReplayBrowserWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (CloseButton)
	{
		CloseButton->OnClicked.AddUniqueDynamic(this, &UReplayBrowserWidget::HandleCloseClicked);
	}

	RefreshList();
}

void UReplayBrowserWidget::RefreshList()
{
	Entries.Reset();

	if (const UReplayStorageSubsystem* Storage = UReplayStorageSubsystem::Get(this))
	{
		Entries = Storage->GetStoredReplays(UserIndex);
	}

	if (TitleText)
	{
		TitleText->SetText(FText::Format(
			LOCTEXT("ReplayBrowserTitle", "Replays ({0})"), FText::AsNumber(Entries.Num())));
	}

	BuildRows();

	UE_LOG(LogReplayModule, Log, TEXT("Replay browser: %d stored replays."), Entries.Num());
}

void UReplayBrowserWidget::BuildRows()
{
	if (!ListBox || !WidgetTree)
	{
		return;
	}

	ListBox->ClearChildren();
	RowSlots.Reset();

	if (Entries.Num() == 0)
	{
		if (UTextBlock* Empty = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("EmptyText")))
		{
			Empty->SetText(LOCTEXT("ReplayBrowserEmpty", "No saved replays yet - use Save in the replay window."));
			Empty->SetVisibility(ESlateVisibility::HitTestInvisible);
			ListBox->AddChild(Empty);
		}

		return;
	}

	for (const FReplaySlotInfo& Entry : Entries)
	{
		UButton* Row = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),
			FName(*FString::Printf(TEXT("Row_%s"), *Entry.SlotName)));

		if (!Row)
		{
			continue;
		}

		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("RowText_%s"), *Entry.SlotName)));

		if (Label)
		{
			// Local time on purpose: the recording stores UTC, but a list the player reads should
			// show the clock they played by.
			const FDateTime Local = Entry.RecordedAtUtc + (FDateTime::Now() - FDateTime::UtcNow());
			const int32 Minutes = FMath::FloorToInt(Entry.DurationSeconds / 60.f);
			const int32 Seconds = FMath::FloorToInt(Entry.DurationSeconds) % 60;

			FString Text = FString::Printf(TEXT("%s   %s   %02d:%02d"),
				*Entry.MapName, *Local.ToString(TEXT("%d.%m.%Y %H:%M")), Minutes, Seconds);

			// A recording without unit states plays on the minimap only - saying so here saves the
			// player from picking it and wondering why the viewport stays empty.
			if (!Entry.bHasViewportData)
			{
				Text += TEXT("   (minimap only)");
			}

			Label->SetText(FText::FromString(Text));
			Label->SetVisibility(ESlateVisibility::HitTestInvisible);
			Row->SetContent(Label);
		}

		Row->OnClicked.AddUniqueDynamic(this, &UReplayBrowserWidget::HandleRowClicked);
		RowSlots.Add(Row, Entry.SlotName);

		ListBox->AddChild(Row);
	}
}

void UReplayBrowserWidget::HandleRowClicked()
{
	// OnClicked carries no sender, so the pressed row is found by asking the buttons themselves.
	for (const TPair<TObjectPtr<UButton>, FString>& Pair : RowSlots)
	{
		if (Pair.Key && Pair.Key->IsPressed())
		{
			LoadAndPlay(Pair.Value);
			return;
		}
	}
}

bool UReplayBrowserWidget::LoadAndPlay(const FString& SlotName)
{
	UReplayStorageSubsystem* Storage = UReplayStorageSubsystem::Get(this);
	if (!Storage)
	{
		return false;
	}

	if (!Storage->LoadReplayFromSlot(SlotName, UserIndex))
	{
		UE_LOG(LogReplayModule, Warning, TEXT("Replay '%s' could not be loaded."), *SlotName);

		if (TitleText)
		{
			TitleText->SetText(LOCTEXT("ReplayBrowserLoadFailed", "Could not load that replay"));
		}

		return false;
	}

	UE_LOG(LogReplayModule, Log, TEXT("Replay '%s' loaded, opening the replay window."), *SlotName);

	OnReplayChosen.Broadcast(SlotName);

	// Close before opening, so the replay window is not covered by the browser it came from.
	RemoveFromParent();
	UReplayFunctionLibrary::OpenReplayWindow(this);

	return true;
}

void UReplayBrowserWidget::CloseBrowser()
{
	RemoveFromParent();
}

void UReplayBrowserWidget::HandleCloseClicked()
{
	CloseBrowser();
}

void UReplayBrowserWidget::BuildFallbackLayout()
{
	// A Blueprint subclass that authored its own hierarchy owns the layout - never fight it.
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RootBorder"));
	if (!Backdrop)
	{
		UE_LOG(LogReplayModule, Warning, TEXT("%s: could not build the fallback browser layout."), *GetName());
		return;
	}

	Backdrop->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.92f));
	Backdrop->SetPadding(ReplayBrowserLayout::PanelPadding);
	Backdrop->SetHorizontalAlignment(HAlign_Center);
	Backdrop->SetVerticalAlignment(VAlign_Center);
	WidgetTree->RootWidget = Backdrop;

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("BrowserColumn"));
	if (!Column)
	{
		return;
	}

	Backdrop->SetContent(Column);

	if (UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText")))
	{
		Title->SetText(LOCTEXT("ReplayBrowserTitleInitial", "Replays"));
		Title->SetJustification(ETextJustify::Center);
		Title->SetVisibility(ESlateVisibility::HitTestInvisible);

		if (UVerticalBoxSlot* BoxSlot = Column->AddChildToVerticalBox(Title))
		{
			BoxSlot->SetPadding(FMargin(0.f, 0.f, 0.f, ReplayBrowserLayout::RowSpacing * 2.f));
			BoxSlot->SetHorizontalAlignment(HAlign_Center);
			BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		}

		TitleText = Title;
	}

	if (UScrollBox* List = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("ListBox")))
	{
		if (UVerticalBoxSlot* BoxSlot = Column->AddChildToVerticalBox(List))
		{
			BoxSlot->SetPadding(ReplayBrowserLayout::RowPadding);
			BoxSlot->SetHorizontalAlignment(HAlign_Fill);
			BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		}

		List->SetVisibility(ESlateVisibility::Visible);
		ListBox = List;
	}

	if (UButton* Close = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CloseButton")))
	{
		if (UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CloseText")))
		{
			Label->SetText(LOCTEXT("ReplayBrowserClose", "Close"));
			Label->SetVisibility(ESlateVisibility::HitTestInvisible);
			Close->SetContent(Label);
		}

		if (UVerticalBoxSlot* BoxSlot = Column->AddChildToVerticalBox(Close))
		{
			BoxSlot->SetPadding(FMargin(0.f, ReplayBrowserLayout::RowSpacing * 2.f, 0.f, 0.f));
			BoxSlot->SetHorizontalAlignment(HAlign_Center);
			BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		}

		CloseButton = Close;
	}
}

#undef LOCTEXT_NAMESPACE
