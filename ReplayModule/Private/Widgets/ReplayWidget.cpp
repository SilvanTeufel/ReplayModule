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

	if (TimeSlider)
	{
		TimeSlider->OnValueChanged.AddUniqueDynamic(this, &UReplayWidget::HandleSliderValueChanged);
		TimeSlider->OnMouseCaptureBegin.AddUniqueDynamic(this, &UReplayWidget::HandleSliderCaptureBegin);
		TimeSlider->OnMouseCaptureEnd.AddUniqueDynamic(this, &UReplayWidget::HandleSliderCaptureEnd);
	}

	ApplyMaterials();
	LoadReplayFromStorage();

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
	RefreshLabels();
}

void UReplayWidget::PauseReplay()
{
	bPlaying = false;
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
	PlaybackSpeed = FMath::Clamp(NewSpeed, 0.1f, 100.f);
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

	RefreshFrame();
	RefreshLabels();
}

void UReplayWidget::CloseReplay()
{
	bPlaying = false;
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
		MakeButton(TEXT("CloseButton"), TEXT("CloseText"), LOCTEXT("ReplayClose", "Close"), CloseButton, CloseLabel);
	}
}

#undef LOCTEXT_NAMESPACE
