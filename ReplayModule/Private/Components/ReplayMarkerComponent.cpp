// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Components/ReplayMarkerComponent.h"

#include "System/ReplayRecorderSubsystem.h"

UReplayMarkerComponent::UReplayMarkerComponent()
{
	// The recorder pulls from the registry on its own timer - the component never ticks.
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
}

uint8 UReplayMarkerComponent::GetColorIndex() const
{
	if (CustomColorIndex >= 0)
	{
		return static_cast<uint8>(FMath::Clamp(CustomColorIndex, 0, 255));
	}

	return static_cast<uint8>(Category);
}

void UReplayMarkerComponent::BeginPlay()
{
	Super::BeginPlay();

	if (UReplayRecorderSubsystem* Recorder = UReplayRecorderSubsystem::Get(this))
	{
		Recorder->RegisterMarker(this);
	}
}

void UReplayMarkerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UReplayRecorderSubsystem* Recorder = UReplayRecorderSubsystem::Get(this))
	{
		Recorder->UnregisterMarker(this);
	}

	Super::EndPlay(EndPlayReason);
}
