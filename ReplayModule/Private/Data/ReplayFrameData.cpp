// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Data/ReplayFrameData.h"

namespace ReplayCoords
{
	/** Full range of the uint16 grid a dot position is quantized onto. */
	static constexpr float NormalizedScale = 65535.f;
}

FReplayStyle::FReplayStyle()
{
	Palette.SetNum(static_cast<int32>(EReplayMarkerCategory::Count));

	Palette[static_cast<int32>(EReplayMarkerCategory::FriendlyUnit)]		= FColor(0, 255, 0, 255);
	Palette[static_cast<int32>(EReplayMarkerCategory::AlliedUnit)]			= FColor(128, 0, 128, 255);
	Palette[static_cast<int32>(EReplayMarkerCategory::EnemyUnit)]			= FColor(255, 0, 0, 255);
	Palette[static_cast<int32>(EReplayMarkerCategory::FriendlyBuilding)]	= FColor(120, 255, 120, 255);
	Palette[static_cast<int32>(EReplayMarkerCategory::AlliedBuilding)]		= FColor(190, 120, 220, 255);
	Palette[static_cast<int32>(EReplayMarkerCategory::EnemyBuilding)]		= FColor(255, 120, 120, 255);
	Palette[static_cast<int32>(EReplayMarkerCategory::NeutralUnit)]			= FColor(200, 200, 200, 255);
	Palette[static_cast<int32>(EReplayMarkerCategory::EffectArea)]			= FColor(255, 180, 40, 255);
	Palette[static_cast<int32>(EReplayMarkerCategory::PointOfInterest)]		= FColor(255, 255, 0, 255);
	Palette[static_cast<int32>(EReplayMarkerCategory::Custom)]				= FColor(255, 255, 255, 255);
}

FColor FReplayStyle::GetColor(uint8 ColorIndex) const
{
	if (Palette.IsValidIndex(ColorIndex))
	{
		return Palette[ColorIndex];
	}

	return FColor::White;
}

FColor FReplayStyle::GetEffectiveFogColor() const
{
	FColor Result = FogColor;
	Result.A = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(FogOpacity * 255.f), 0, 255));
	return Result;
}

bool FReplayRecording::IsValid() const
{
	return Frames.Num() > 0
		&& !FMath::IsNearlyEqual(WorldMax.X, WorldMin.X)
		&& !FMath::IsNearlyEqual(WorldMax.Y, WorldMin.Y);
}

float FReplayRecording::GetDurationSeconds() const
{
	return Frames.Num() > 0 ? Frames.Last().TimeSeconds : 0.f;
}

int32 FReplayRecording::FindFrameIndexForTime(float TimeSeconds) const
{
	if (Frames.Num() == 0)
	{
		return INDEX_NONE;
	}

	if (TimeSeconds <= Frames[0].TimeSeconds)
	{
		return 0;
	}

	if (TimeSeconds >= Frames.Last().TimeSeconds)
	{
		return Frames.Num() - 1;
	}

	// Frames are written at a fixed interval, but a hitching game can space them unevenly, so
	// binary search rather than dividing by the interval.
	int32 Low = 0;
	int32 High = Frames.Num() - 1;

	while (Low + 1 < High)
	{
		const int32 Mid = (Low + High) / 2;
		if (Frames[Mid].TimeSeconds <= TimeSeconds)
		{
			Low = Mid;
		}
		else
		{
			High = Mid;
		}
	}

	return Low;
}

int64 FReplayRecording::GetApproxMemoryBytes() const
{
	int64 Bytes = sizeof(FReplayRecording);
	Bytes += static_cast<int64>(BackgroundPixels.Num()) * sizeof(FColor);

	for (const FReplayFrame& Frame : Frames)
	{
		Bytes += sizeof(FReplayFrame);
		Bytes += static_cast<int64>(Frame.Dots.Num()) * sizeof(FReplayDot);
		Bytes += static_cast<int64>(Frame.Viewport.Corners.Num()) * sizeof(FVector2f);
	}

	return Bytes;
}

FReplayInfo FReplayRecording::GetInfo() const
{
	FReplayInfo Info;
	Info.MapName = MapName;
	Info.RecordedAtUtc = RecordedAtUtc;
	Info.FrameCount = Frames.Num();
	Info.DurationSeconds = GetDurationSeconds();
	Info.IntervalSeconds = IntervalSeconds;
	Info.LocalTeamId = LocalTeamId;
	Info.ApproxMemoryKB = static_cast<int32>(GetApproxMemoryBytes() / 1024);
	return Info;
}

bool FReplayRecording::WorldToNormalized(const FVector& WorldLocation, uint16& OutX, uint16& OutY) const
{
	const double ExtentX = WorldMax.X - WorldMin.X;
	const double ExtentY = WorldMax.Y - WorldMin.Y;

	if (FMath::IsNearlyZero(ExtentX) || FMath::IsNearlyZero(ExtentY))
	{
		return false;
	}

	const double U = (WorldLocation.X - WorldMin.X) / ExtentX;
	const double V = (WorldLocation.Y - WorldMin.Y) / ExtentY;

	// Markers outside the map would clamp onto the border and draw a misleading line of dots there.
	if (U < 0.0 || U > 1.0 || V < 0.0 || V > 1.0)
	{
		return false;
	}

	OutX = static_cast<uint16>(FMath::Clamp(FMath::RoundToInt(U * ReplayCoords::NormalizedScale), 0, 65535));
	OutY = static_cast<uint16>(FMath::Clamp(FMath::RoundToInt(V * ReplayCoords::NormalizedScale), 0, 65535));
	return true;
}

void FReplayRecording::Reset()
{
	MapName.Reset();
	RecordedAtUtc = FDateTime();
	LocalTeamId = -1;
	IntervalSeconds = 1.f;
	ReferenceTextureSize = 256;
	WorldMin = FVector2D::ZeroVector;
	WorldMax = FVector2D::ZeroVector;
	Style = FReplayStyle();
	Frames.Reset();
	BackgroundPixels.Reset();
	BackgroundSize = 0;
}
