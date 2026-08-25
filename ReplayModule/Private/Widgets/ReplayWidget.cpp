// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Widgets/ReplayWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ReplayModule.h"
#include "Settings/ReplayModuleSettings.h"
#include "System/ReplayFrameRenderer.h"
#include "System/ReplayStorageSubsystem.h"
#include "System/ReplayPlaybackSubsystem.h"
#include "System/ReplaySessionActor.h"

#define LOCTEXT_NAMESPACE "ReplayWidget"

namespace ReplayWidgetLayout
{
	static const FLinearColor ButtonTint(0.10f, 0.12f, 0.15f, 0.95f);
	static const FMargin PanelPadding(16.f, 14.f);
	static constexpr float RowSpacing = 8.f;
}

bool UReplayWidget::Initialize()
{
	const bool bFirstInit = Super::Initialize();

	// Has to happen here, not in NativeConstruct: once RebuildWidget has run, a widget tree added
	// afterwards never takes over the Slate hierarchy.
	BuildFallbackLayout();

	return bFirstInit;
}

void UReplayWidget::NativeConstruct()
{
	Super::NativeConstruct();

	const UReplayModuleSettings* Settings = GetDefault<UReplayModuleSettings>();

	if (SpeedSteps.Num() == 0)
	{
		SpeedSteps = Settings->PlaybackSpeedSteps;
	}

	if (SpeedSteps.Num() == 0)
	{
		SpeedSteps = { 1.f, 2.f, 4.f, 8.f, 16.f };
	}

	if (PlaybackSpeed <= 0.f)
	{
		PlaybackSpeed = FMath::Max(0.1f, Settings->DefaultPlaybackSpeed);
	}

	bLoop = Settings->bLoopPlayback;

	if (PlayPauseButton)
	{
		PlayPauseButton->OnClicked.AddUniqueDynamic(this, &UReplayWidget::HandlePlayPauseClicked);
	}

	if (SpeedButton)
	{
		SpeedButton->OnClicked.AddUniqueDynamic(this, &UReplayWidget::HandleSpeedClicked);
	}

	if (CloseButton)
	{
		CloseButton->OnClicked.AddUniqueDynamic(this, &UReplayWidget::HandleCloseClicked);
	}

	if (SaveButton)
	{
		SaveButton->OnClicked.AddUniqueDynamic(this, &UReplayWidget::HandleSaveClicked);
	}

	if (TimeSlider)
	{
		TimeSlider->OnValueChanged.AddUniqueDynamic(this, &UReplayWidget::HandleSliderValueChanged);
		TimeSlider->OnMouseCaptureBegin.AddUniqueDynamic(this, &UReplayWidget::HandleSliderCaptureBegin);
		TimeSlider->OnMouseCaptureEnd.AddUniqueDynamic(this, &UReplayWidget::HandleSliderCaptureEnd);
	}

	ApplyMaterials();
	LoadReplayFromStorage();

	// Try the viewport playback. It declines by itself when the recording predates unit states or the
	// setting is off, and the widget then simply stays the minimap replay it has always been.
	if (UReplayPlaybackSubsystem* Playback = UReplayPlaybackSubsystem::Get(this))
	{
		bViewportPlaybackActive = Playback->BeginPlayback();

		if (bViewportPlaybackActive)
		{
			// The viewport is the main picture now, so the map shrinks into a corner overlay.
			ApplyMinimapOverlayLayout();

			// One clock for both pictures: the subsystem owns the time and this widget follows it.
			// Two independent clocks drift apart within seconds at 6x and then show different moments.
			PlaybackSpeed = FMath::Min(PlaybackSpeed, Settings->MaxPlaybackSpeed);
			Playback->SetSpeed(PlaybackSpeed);
		}
	}

	if (bAutoPlayOnOpen)
	{
		PlayReplay();
	}
	else
	{
		RefreshLabels();
	}
}

void UReplayWidget::NativeDestruct()
{
	bPlaying = false;

	// Closing the window by any route (including being torn down with the HUD) has to end the
	// playback - otherwise the proxies stay behind and the live units stay hidden.
	if (bViewportPlaybackActive)
	{
		// Same rule as CloseReplay: a shared viewing is ended by the server, not by one guest's window
		// going away.
		bool bMayEnd = true;
		if (const AReplaySessionActor* Session = AReplaySessionActor::Find(this))
		{
			bMayEnd = !Session->IsSharedPlaybackActive();
		}

		if (bMayEnd)
		{
			if (UReplayPlaybackSubsystem* Playback = UReplayPlaybackSubsystem::Get(this))
			{
				Playback->EndPlayback();
			}
		}

		bViewportPlaybackActive = false;
	}

	Recording.Reset();

	Super::NativeDestruct();
}

void UReplayWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bPlaying || !Recording.IsValid())
	{
		return;
	}

	const float Duration = ReplayInfo.DurationSeconds;
	if (Duration <= 0.f)
	{
		return;
	}

	if (UReplayPlaybackSubsystem* Playback = bViewportPlaybackActive ? UReplayPlaybackSubsystem::Get(this) : nullptr)
	{
		// The subsystem advances the time because it also has to move the proxies; the widget just
		// reads it, so the minimap always shows the same moment as the viewport.
		PlaybackTime = Playback->GetPlaybackTime();
		bPlaying = !Playback->IsPaused();
	}
	else
	{
		PlaybackTime += InDeltaTime * PlaybackSpeed;

		if (PlaybackTime >= Duration)
		{
			if (bLoop)
			{
				PlaybackTime = FMath::Fmod(PlaybackTime, Duration);
			}
			else
			{
				PlaybackTime = Duration;
				bPlaying = false;
			}
		}
	}

	RefreshFrame();
	RefreshLabels();
}

bool UReplayWidget::LoadReplayFromStorage()
{
	UReplayStorageSubsystem* Storage = UReplayStorageSubsystem::Get(this);
	if (!Storage || !Storage->HasReplay())
	{
		UE_LOG(LogReplayModule, Log, TEXT("The replay window opened without a recording to show."));
		return false;
	}

	Recording = Storage->GetRecording();
	ReplayInfo = Storage->GetReplayInfo();

	const UReplayModuleSettings* Settings = GetDefault<UReplayModuleSettings>();
	const int32 Size = RenderTextureSize > 0 ? RenderTextureSize : Settings->PlaybackTextureSize;

	if (!Renderer)
	{
		Renderer = NewObject<UReplayFrameRenderer>(this);
	}

	Renderer->Configure(Size);
	Renderer->SetRecording(Recording);

	// A recording carries its own terrain pixels only when the project stored them; otherwise the
	// live texture on the storage subsystem is the terrain layer.
	UTexture2D* Background = Renderer->GetBackgroundTexture();
	if (!Background)
	{
		Background = Storage->GetBackgroundTexture();
	}

	if (MapMaterialInstance)
	{
		MapMaterialInstance->SetTextureParameterValue(MarkerTextureParameterName, Renderer->GetReplayTexture());

		if (Background)
		{
			MapMaterialInstance->SetTextureParameterValue(BackgroundTextureParameterName, Background);
		}
	}
	else if (MapImage)
	{
		MapImage->SetBrushFromTexture(Renderer->GetReplayTexture(), false);
	}

	if (BackgroundImage)
	{
		if (Background)
		{
			BackgroundImage->SetBrushFromTexture(Background, false);
			BackgroundImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			BackgroundImage->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	if (TitleText)
	{
		TitleText->SetText(FText::Format(
			LOCTEXT("ReplayTitleFormat", "Replay - {0}"),
			FText::FromString(ReplayInfo.MapName)));
	}

	PlaybackTime = 0.f;
	CurrentFrame = INDEX_NONE;
	RefreshFrame(/*bForce=*/true);
	RefreshLabels();

	return true;
}

void UReplayWidget::PlayReplay()
{
	if (!Recording.IsValid())
	{
		return;
	}

	// Restarting from the end feels less broken than a play button that does nothing.
	if (!bLoop && PlaybackTime >= ReplayInfo.DurationSeconds)
	{
		PlaybackTime = 0.f;
	}

	bPlaying = true;

	// In a shared session the request goes through the server, so pressing play here starts it for
	// everyone rather than desyncing this viewer from the rest.
	if (AReplaySessionActor* Session = AReplaySessionActor::Find(this))
	{
		if (Session->IsSharedPlaybackActive())
		{
			Session->RequestPaused(false);
			RefreshLabels();
			return;
		}
	}

	if (UReplayPlaybackSubsystem* Playback = UReplayPlaybackSubsystem::Get(this))
	{
		Playback->SetPaused(false);
	}

	RefreshLabels();
}

void UReplayWidget::PauseReplay()
{
	bPlaying = false;

	if (AReplaySessionActor* Session = AReplaySessionActor::Find(this))
	{
		if (Session->IsSharedPlaybackActive())
		{
			Session->RequestPaused(true);
			RefreshLabels();
			return;
		}
	}

	if (UReplayPlaybackSubsystem* Playback = UReplayPlaybackSubsystem::Get(this))
	{
		Playback->SetPaused(true);
	}

	RefreshLabels();
}

void UReplayWidget::ToggleReplayPlayback()
{
	if (bPlaying)
	{
		PauseReplay();
	}
	else
	{
		PlayReplay();
	}
}

void UReplayWidget::SetReplaySpeed(float NewSpeed)
{
	// Capped at the setting (6x) while the viewport plays: past that the proxies jump from recorded
	// frame to recorded frame instead of gliding, because the recording interval becomes the limit.
	const UReplayModuleSettings* Settings = GetDefault<UReplayModuleSettings>();
	const float Ceiling = bViewportPlaybackActive ? Settings->MaxPlaybackSpeed : 100.f;
	PlaybackSpeed = FMath::Clamp(NewSpeed, 0.1f, Ceiling);

	if (AReplaySessionActor* Session = AReplaySessionActor::Find(this))
	{
		if (Session->IsSharedPlaybackActive())
		{
			Session->RequestSpeed(PlaybackSpeed);
			RefreshLabels();
			return;
		}
	}

	if (UReplayPlaybackSubsystem* Playback = UReplayPlaybackSubsystem::Get(this))
	{
		Playback->SetSpeed(PlaybackSpeed);
	}

	RefreshLabels();
}

void UReplayWidget::CycleReplaySpeed()
{
	if (SpeedSteps.Num() == 0)
	{
		return;
	}

	int32 NextIndex = 0;
	for (int32 Index = 0; Index < SpeedSteps.Num(); ++Index)
	{
		if (FMath::IsNearlyEqual(SpeedSteps[Index], PlaybackSpeed, 0.01f))
		{
			NextIndex = (Index + 1) % SpeedSteps.Num();
			break;
		}
	}

	SetReplaySpeed(SpeedSteps[NextIndex]);
}

void UReplayWidget::SeekToNormalized(float Alpha)
{
	SeekToTime(FMath::Clamp(Alpha, 0.f, 1.f) * ReplayInfo.DurationSeconds);
}

void UReplayWidget::SeekToTime(float TimeSeconds)
{
	PlaybackTime = FMath::Clamp(TimeSeconds, 0.f, FMath::Max(0.f, ReplayInfo.DurationSeconds));

	// Seeking has to re-seat every proxy, forwards and backwards alike - and in a shared session it
	// must move everyone, which is why it goes through the server there.
	bool bHandled = false;
	if (AReplaySessionActor* Session = AReplaySessionActor::Find(this))
	{
		if (Session->IsSharedPlaybackActive())
		{
			Session->RequestSeek(PlaybackTime);
			bHandled = true;
		}
	}

	if (!bHandled)
	{
		if (UReplayPlaybackSubsystem* Playback = UReplayPlaybackSubsystem::Get(this))
		{
			Playback->SeekToTime(PlaybackTime);
		}
	}

	RefreshFrame();
	RefreshLabels();
}

void UReplayWidget::CloseReplay()
{
	bPlaying = false;

	// Puts the proxies away and unhides the live units, so closing the window leaves the level
	// exactly as it was before the replay started.
	//
	// In a shared session only the server may end it - a guest closing their window should leave the
	// viewing running for everyone else, and the server's StopSharedPlayback is the way out.
	bool bMayEnd = true;
	if (const AReplaySessionActor* Session = AReplaySessionActor::Find(this))
	{
		bMayEnd = !Session->IsSharedPlaybackActive();
	}

	if (bMayEnd)
	{
		if (UReplayPlaybackSubsystem* Playback = UReplayPlaybackSubsystem::Get(this))
		{
			Playback->EndPlayback();
		}
	}

	bViewportPlaybackActive = false;
	RemoveFromParent();
}

void UReplayWidget::HandlePlayPauseClicked()
{
	ToggleReplayPlayback();
}

void UReplayWidget::HandleSpeedClicked()
{
	CycleReplaySpeed();
}

void UReplayWidget::HandleCloseClicked()
{
	CloseReplay();
}

void UReplayWidget::HandleSliderValueChanged(float Value)
{
	// RefreshLabels writes the slider itself while playing; without this guard that write would
	// come straight back here and fight the playhead.
	if (bUpdatingSlider)
	{
		return;
	}

	SeekToNormalized(Value);
}

void UReplayWidget::HandleSliderCaptureBegin()
{
	bWasPlayingBeforeScrub = bPlaying;
	bPlaying = false;
}

void UReplayWidget::HandleSliderCaptureEnd()
{
	if (bWasPlayingBeforeScrub)
	{
		PlayReplay();
	}
}

void UReplayWidget::RefreshFrame(bool bForce)
{
	if (!Renderer || !Recording.IsValid())
	{
		return;
	}

	const int32 FrameIndex = Recording->FindFrameIndexForTime(PlaybackTime);
	if (FrameIndex == INDEX_NONE)
	{
		return;
	}

	// Rendering is a full CPU pass over the texture, so it only runs when the frame actually moved.
	if (!bForce && FrameIndex == CurrentFrame)
	{
		return;
	}

	CurrentFrame = FrameIndex;
	Renderer->RenderFrame(FrameIndex);

	// The brush caches the texture pointer, so it has to be rebound whenever the texture is new.
	if (!MapMaterialInstance && MapImage)
	{
		MapImage->SetBrushFromTexture(Renderer->GetReplayTexture(), false);
	}

	OnReplayFrameChanged(FrameIndex, Recording->Frames[FrameIndex].TimeSeconds);
}

void UReplayWidget::RefreshLabels()
{
	if (TimeSlider)
	{
		const float Duration = ReplayInfo.DurationSeconds;
		const float Alpha = Duration > 0.f ? FMath::Clamp(PlaybackTime / Duration, 0.f, 1.f) : 0.f;

		bUpdatingSlider = true;
		TimeSlider->SetValue(Alpha);
		bUpdatingSlider = false;
	}

	if (TimeText)
	{
		const int32 CurrentSeconds = FMath::FloorToInt(PlaybackTime);
		const int32 TotalSeconds = FMath::FloorToInt(ReplayInfo.DurationSeconds);

		TimeText->SetText(FText::FromString(FString::Printf(
			TEXT("%02d:%02d / %02d:%02d"),
			CurrentSeconds / 60, CurrentSeconds % 60,
			TotalSeconds / 60, TotalSeconds % 60)));
	}

	if (SpeedText)
	{
		SpeedText->SetText(FText::FromString(FString::Printf(TEXT("%gx"), PlaybackSpeed)));
	}

	if (PlayPauseText)
	{
		PlayPauseText->SetText(bPlaying
			? LOCTEXT("ReplayPause", "Pause")
			: LOCTEXT("ReplayPlay", "Play"));
	}
}

void UReplayWidget::ApplyMaterials()
{
	if (MapMaterial && MapImage)
	{
		MapImage->SetBrushFromMaterial(MapMaterial);
		MapMaterialInstance = MapImage->GetDynamicMaterial();
	}

	if (FrameMaterial && FrameImage)
	{
		FrameImage->SetBrushFromMaterial(FrameMaterial);
		FrameImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else if (FrameImage)
	{
		FrameImage->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (RootBorder)
	{
		if (BackdropMaterial)
		{
			RootBorder->SetBrushFromMaterial(BackdropMaterial);
		}
		else
		{
			RootBorder->SetBrushColor(BackdropColor);
		}
	}

	if (MapImage)
	{
		MapImage->SetRenderTransformAngle(MapRotationAngle);
	}

	if (BackgroundImage)
	{
		BackgroundImage->SetRenderTransformAngle(MapRotationAngle);
	}
}

void UReplayWidget::BuildFallbackLayout()
{
	// A Blueprint subclass that authored its own hierarchy owns the layout - never fight it.
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RootBorder"));
	if (!Backdrop)
	{
		UE_LOG(LogReplayModule, Warning, TEXT("%s: could not build the fallback replay layout."), *GetName());
		return;
	}

	Backdrop->SetBrushColor(BackdropColor);
	Backdrop->SetPadding(ReplayWidgetLayout::PanelPadding);
	Backdrop->SetHorizontalAlignment(HAlign_Center);
	Backdrop->SetVerticalAlignment(VAlign_Center);
	WidgetTree->RootWidget = Backdrop;
	RootBorder = Backdrop;

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ReplayColumn"));
	if (!Column)
	{
		return;
	}
	Backdrop->SetContent(Column);

	// --- Title ---
	{
		UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
		if (Title)
		{
			Title->SetText(LOCTEXT("ReplayTitle", "Replay"));
			Title->SetJustification(ETextJustify::Center);
			Title->SetVisibility(ESlateVisibility::HitTestInvisible);

			if (UVerticalBoxSlot* TitleSlot = Column->AddChildToVerticalBox(Title))
			{
				TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, ReplayWidgetLayout::RowSpacing));
				TitleSlot->SetHorizontalAlignment(HAlign_Center);
				TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			}

			TitleText = Title;
		}
	}

	// --- Picture: terrain, markers and an optional frame stacked on top of each other ---
	{
		USizeBox* MapBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("MapBox"));
		UOverlay* MapOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("MapOverlay"));

		if (MapBox && MapOverlay)
		{
			MapBox->SetWidthOverride(MapDisplaySize);
			MapBox->SetHeightOverride(MapDisplaySize);
			MapBox->SetContent(MapOverlay);
			MapSizeBox = MapBox;

			UImage* Background = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("BackgroundImage"));
			if (Background)
			{
				Background->SetVisibility(ESlateVisibility::HitTestInvisible);
				if (UOverlaySlot* BackgroundSlot = MapOverlay->AddChildToOverlay(Background))
				{
					BackgroundSlot->SetHorizontalAlignment(HAlign_Fill);
					BackgroundSlot->SetVerticalAlignment(VAlign_Fill);
				}
				BackgroundImage = Background;
			}

			UImage* Map = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("MapImage"));
			if (Map)
			{
				Map->SetVisibility(ESlateVisibility::HitTestInvisible);
				if (UOverlaySlot* MapSlot = MapOverlay->AddChildToOverlay(Map))
				{
					MapSlot->SetHorizontalAlignment(HAlign_Fill);
					MapSlot->SetVerticalAlignment(VAlign_Fill);
				}
				MapImage = Map;
			}

			UImage* Frame = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("FrameImage"));
			if (Frame)
			{
				Frame->SetVisibility(ESlateVisibility::Collapsed);
				if (UOverlaySlot* FrameSlot = MapOverlay->AddChildToOverlay(Frame))
				{
					FrameSlot->SetHorizontalAlignment(HAlign_Fill);
					FrameSlot->SetVerticalAlignment(VAlign_Fill);
				}
				FrameImage = Frame;
			}

			if (UVerticalBoxSlot* MapBoxSlot = Column->AddChildToVerticalBox(MapBox))
			{
				MapBoxSlot->SetHorizontalAlignment(HAlign_Center);
				MapBoxSlot->SetPadding(FMargin(0.f, 0.f, 0.f, ReplayWidgetLayout::RowSpacing));
				MapBoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			}
		}
	}

	// --- Scrub slider ---
	{
		USlider* Slider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(), TEXT("TimeSlider"));
		if (Slider)
		{
			Slider->SetMinValue(0.f);
			Slider->SetMaxValue(1.f);
			Slider->SetValue(0.f);

			if (UVerticalBoxSlot* SliderSlot = Column->AddChildToVerticalBox(Slider))
			{
				SliderSlot->SetHorizontalAlignment(HAlign_Fill);
				SliderSlot->SetPadding(FMargin(0.f, 0.f, 0.f, ReplayWidgetLayout::RowSpacing));
				SliderSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			}

			TimeSlider = Slider;
		}
	}

	// --- Controls ---
	{
		UHorizontalBox* Controls = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ControlsRow"));
		if (!Controls)
		{
			return;
		}

		if (UVerticalBoxSlot* ControlsSlot = Column->AddChildToVerticalBox(Controls))
		{
			ControlsSlot->SetHorizontalAlignment(HAlign_Fill);
			ControlsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		}

		// Small helper so the three buttons below stay identical without repeating eight lines.
		auto MakeButton = [&](const FName& ButtonName, const FName& LabelName, const FText& Label,
			TObjectPtr<UButton>& OutButton, TObjectPtr<UTextBlock>& OutLabel)
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), ButtonName);
			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), LabelName);
			if (!Button || !Text)
			{
				return;
			}

			Text->SetText(Label);
			Text->SetJustification(ETextJustify::Center);
			Button->SetContent(Text);
			Button->SetBackgroundColor(ReplayWidgetLayout::ButtonTint);

			if (UHorizontalBoxSlot* ButtonSlot = Controls->AddChildToHorizontalBox(Button))
			{
				ButtonSlot->SetPadding(FMargin(0.f, 0.f, ReplayWidgetLayout::RowSpacing, 0.f));
				ButtonSlot->SetVerticalAlignment(VAlign_Center);
			}

			OutButton = Button;
			OutLabel = Text;
		};

		MakeButton(TEXT("PlayPauseButton"), TEXT("PlayPauseText"), LOCTEXT("ReplayPlay", "Play"), PlayPauseButton, PlayPauseText);
		MakeButton(TEXT("SpeedButton"), TEXT("SpeedText"), LOCTEXT("ReplaySpeed", "4x"), SpeedButton, SpeedText);

		UTextBlock* Time = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TimeText"));
		if (Time)
		{
			Time->SetText(LOCTEXT("ReplayTimeZero", "00:00 / 00:00"));
			Time->SetVisibility(ESlateVisibility::HitTestInvisible);

			if (UHorizontalBoxSlot* TimeSlot = Controls->AddChildToHorizontalBox(Time))
			{
				TimeSlot->SetPadding(FMargin(ReplayWidgetLayout::RowSpacing, 0.f));
				TimeSlot->SetVerticalAlignment(VAlign_Center);
				TimeSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			}

			TimeText = Time;
		}

		TObjectPtr<UTextBlock> CloseLabel = nullptr;
		MakeButton(TEXT("SaveButton"), TEXT("SaveText"), LOCTEXT("ReplaySave", "Save"), SaveButton, SaveText);
		MakeButton(TEXT("CloseButton"), TEXT("CloseText"), LOCTEXT("ReplayClose", "Close"), CloseButton, CloseLabel);
	}
}

void UReplayWidget::ApplyMinimapOverlayLayout()
{
	const UReplayModuleSettings* Settings = GetDefault<UReplayModuleSettings>();

	MapDisplaySize = Settings->MinimapOverlaySize;

	if (MapSizeBox)
	{
		MapSizeBox->SetWidthOverride(MapDisplaySize);
		MapSizeBox->SetHeightOverride(MapDisplaySize);
	}

	// Anchor and alignment both take the same 0..1 pair, so (1,0) pins the top-right corner of the
	// widget to the top-right corner of the screen. Setting only the anchor would hang the widget off
	// the edge, which is the usual way this ends up half off-screen.
	const FVector2D Anchor = Settings->MinimapAnchor;
	SetAnchorsInViewport(FAnchors(Anchor.X, Anchor.Y, Anchor.X, Anchor.Y));
	SetAlignmentInViewport(Anchor);

	// Backdrop out of the way: a full-screen dark panel behind a corner minimap would grey out the
	// replay we are here to watch.
	if (RootBorder)
	{
		RootBorder->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.55f));
	}

	UE_LOG(LogReplayModule, Log, TEXT("Minimap moved to the corner overlay (anchor %.1f/%.1f, %.0f px)."),
		Anchor.X, Anchor.Y, MapDisplaySize);
}

FString UReplayWidget::SaveReplay()
{
	UReplayStorageSubsystem* Storage = UReplayStorageSubsystem::Get(this);
	if (!Storage)
	{
		return FString();
	}

	const FString SlotName = Storage->SaveReplayToNewSlot();

	// The button doubles as the status readout - a save that silently did nothing is the one thing
	// a save button must never do.
	if (SaveText)
	{
		SaveText->SetText(SlotName.IsEmpty()
			? LOCTEXT("ReplaySaveFailed", "Save failed")
			: LOCTEXT("ReplaySaved", "Saved"));
	}

	if (!SlotName.IsEmpty())
	{
		UE_LOG(LogReplayModule, Log, TEXT("Replay saved as '%s'."), *SlotName);
	}

	return SlotName;
}

void UReplayWidget::HandleSaveClicked()
{
	SaveReplay();
}

#undef LOCTEXT_NAMESPACE
