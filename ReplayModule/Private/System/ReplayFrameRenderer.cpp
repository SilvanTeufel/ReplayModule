// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "System/ReplayFrameRenderer.h"

#include "Engine/Texture2D.h"
#include "ReplayModule.h"
#include "RenderUtils.h"

namespace
{
	/** Full range of the uint16 grid a dot position was quantized onto. */
	static constexpr float NormalizedScale = 65535.f;
}

void UReplayFrameRenderer::Configure(int32 InTextureSize)
{
	const int32 ClampedSize = FMath::Clamp(InTextureSize, 64, 2048);
	if (ClampedSize == TextureSize && ReplayTexture)
	{
		return;
	}

	TextureSize = ClampedSize;
	ReplayTexture = nullptr;
	LastRenderedFrame = INDEX_NONE;

	EnsureTexture();
}

void UReplayFrameRenderer::SetRecording(TSharedPtr<const FReplayRecording, ESPMode::ThreadSafe> InRecording)
{
	Recording = MoveTemp(InRecording);
	LastRenderedFrame = INDEX_NONE;

	BuildBackgroundTexture();
}

int32 UReplayFrameRenderer::GetFrameCount() const
{
	return Recording.IsValid() ? Recording->Frames.Num() : 0;
}

void UReplayFrameRenderer::EnsureTexture()
{
	if (ReplayTexture || TextureSize <= 0)
	{
		return;
	}

	ReplayTexture = UTexture2D::CreateTransient(TextureSize, TextureSize, PF_B8G8R8A8);
	if (!ReplayTexture)
	{
		UE_LOG(LogReplayModule, Error, TEXT("Could not create the replay texture at %d x %d."), TextureSize, TextureSize);
		return;
	}

	ReplayTexture->SRGB = true;
	ReplayTexture->CompressionSettings = TC_Default;
	ReplayTexture->Filter = TF_Bilinear;
	ReplayTexture->AddressX = TA_Clamp;
	ReplayTexture->AddressY = TA_Clamp;
	ReplayTexture->NeverStream = true;
	ReplayTexture->UpdateResource();

	Pixels.SetNumUninitialized(TextureSize * TextureSize);
}

void UReplayFrameRenderer::BuildBackgroundTexture()
{
	BackgroundTexture = nullptr;

	if (!Recording.IsValid() || Recording->BackgroundSize <= 0)
	{
		return;
	}

	const int32 Size = Recording->BackgroundSize;
	if (Recording->BackgroundPixels.Num() != Size * Size)
	{
		UE_LOG(LogReplayModule, Warning, TEXT("Background pixel count does not match its declared size - skipping the terrain layer."));
		return;
	}

	BackgroundTexture = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8);
	if (!BackgroundTexture)
	{
		return;
	}

	BackgroundTexture->SRGB = true;
	BackgroundTexture->CompressionSettings = TC_Default;
	BackgroundTexture->Filter = TF_Bilinear;
	BackgroundTexture->AddressX = TA_Clamp;
	BackgroundTexture->AddressY = TA_Clamp;
	BackgroundTexture->NeverStream = true;
	BackgroundTexture->UpdateResource();

	UploadPixels(BackgroundTexture, Recording->BackgroundPixels);
}

void UReplayFrameRenderer::RenderFrame(int32 FrameIndex)
{
	EnsureTexture();

	if (!ReplayTexture || !Recording.IsValid() || !Recording->Frames.IsValidIndex(FrameIndex))
	{
		return;
	}

	const FReplayFrame& Frame = Recording->Frames[FrameIndex];
	const FReplayStyle& Style = Recording->Style;

	// Radii were quantized against the recording's reference size; the replay may run at another.
	const float RadiusScale = (Recording->ReferenceTextureSize > 0)
		? static_cast<float>(TextureSize) / static_cast<float>(Recording->ReferenceTextureSize)
		: 1.f;

	auto ToPixel = [this](uint16 Value)
	{
		return FMath::RoundToInt((static_cast<float>(Value) / NormalizedScale) * (TextureSize - 1));
	};

	auto ScaleRadius = [RadiusScale, &Style](uint8 Quantized)
	{
		return FMath::Max(1, FMath::RoundToInt(static_cast<float>(Quantized) * RadiusScale * Style.DotScale));
	};

	// --- Pass 1: everything starts unexplored ---
	const FColor FogColor = Style.GetEffectiveFogColor();
	for (FColor& Pixel : Pixels)
	{
		Pixel = FogColor;
	}

	// --- Pass 2: markers with a sight radius clear the fog ---
	for (const FReplayDot& Dot : Frame.Dots)
	{
		if (Dot.SightPixelRadius == 0)
		{
			continue;
		}

		DrawFilledCircle(
			ToPixel(Dot.X),
			ToPixel(Dot.Y),
			FMath::Max(1, FMath::RoundToInt(static_cast<float>(Dot.SightPixelRadius) * RadiusScale)),
			Style.RevealedColor);
	}

	// --- Pass 3: the markers themselves ---
	for (const FReplayDot& Dot : Frame.Dots)
	{
		const int32 CenterX = ToPixel(Dot.X);
		const int32 CenterY = ToPixel(Dot.Y);
		const int32 Radius = ScaleRadius(Dot.PixelRadius);

		if (Style.bDrawDotOutline)
		{
			DrawCircleOutline(CenterX, CenterY, Radius, Style.DotOutlineThickness, Style.DotOutlineColor);
		}

		DrawFilledCircle(CenterX, CenterY, Radius, Style.GetColor(Dot.ColorIndex));
	}

	// --- Pass 4: where the camera was pointing ---
	if (Style.bDrawViewport && Frame.Viewport.bValid && Frame.Viewport.Corners.Num() == 4)
	{
		int32 CornerX[4];
		int32 CornerY[4];

		for (int32 Index = 0; Index < 4; ++Index)
		{
			CornerX[Index] = FMath::Clamp(FMath::RoundToInt(Frame.Viewport.Corners[Index].X * (TextureSize - 1)), 0, TextureSize - 1);
			CornerY[Index] = FMath::Clamp(FMath::RoundToInt(Frame.Viewport.Corners[Index].Y * (TextureSize - 1)), 0, TextureSize - 1);
		}

		for (int32 Index = 0; Index < 4; ++Index)
		{
			const int32 Next = (Index + 1) % 4;
			DrawLine(CornerX[Index], CornerY[Index], CornerX[Next], CornerY[Next], Style.ViewportThickness, Style.ViewportColor);
		}
	}

	UploadPixels(ReplayTexture, Pixels);
	LastRenderedFrame = FrameIndex;
}

void UReplayFrameRenderer::UploadPixels(UTexture2D* Texture, const TArray<FColor>& InPixels) const
{
	if (!Texture || InPixels.Num() == 0)
	{
		return;
	}

	const int32 Size = Texture->GetSizeX();
	const int32 DataSize = InPixels.Num() * sizeof(FColor);

	// The render thread reads this asynchronously, so it gets its own copy that the cleanup
	// callback frees once the upload is through - never a pointer into our scratch buffer.
	uint8* DataCopy = static_cast<uint8*>(FMemory::Malloc(DataSize));
	FMemory::Memcpy(DataCopy, InPixels.GetData(), DataSize);

	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Size, Texture->GetSizeY());

	Texture->UpdateTextureRegions(
		0,
		1,
		Region,
		Size * sizeof(FColor),
		sizeof(FColor),
		DataCopy,
		[](uint8* SrcData, const FUpdateTextureRegion2D* Regions)
		{
			FMemory::Free(SrcData);
			delete Regions;
		});
}

void UReplayFrameRenderer::DrawFilledCircle(int32 CenterX, int32 CenterY, int32 Radius, const FColor& Color)
{
	const int32 RadiusSq = Radius * Radius;

	for (int32 DY = -Radius; DY <= Radius; ++DY)
	{
		const int32 PY = CenterY + DY;
		if (PY < 0 || PY >= TextureSize)
		{
			continue;
		}

		for (int32 DX = -Radius; DX <= Radius; ++DX)
		{
			if (DX * DX + DY * DY > RadiusSq)
			{
				continue;
			}

			const int32 PX = CenterX + DX;
			if (PX < 0 || PX >= TextureSize)
			{
				continue;
			}

			Pixels[PY * TextureSize + PX] = Color;
		}
	}
}

void UReplayFrameRenderer::DrawCircleOutline(int32 CenterX, int32 CenterY, int32 Radius, int32 Thickness, const FColor& Color)
{
	const int32 OuterRadius = Radius + FMath::Max(1, Thickness);
	const int32 OuterRadiusSq = OuterRadius * OuterRadius;
	const int32 InnerRadiusSq = Radius * Radius;

	for (int32 DY = -OuterRadius; DY <= OuterRadius; ++DY)
	{
		const int32 PY = CenterY + DY;
		if (PY < 0 || PY >= TextureSize)
		{
			continue;
		}

		for (int32 DX = -OuterRadius; DX <= OuterRadius; ++DX)
		{
			const int32 DistSq = DX * DX + DY * DY;
			if (DistSq > OuterRadiusSq || DistSq <= InnerRadiusSq)
			{
				continue;
			}

			const int32 PX = CenterX + DX;
			if (PX < 0 || PX >= TextureSize)
			{
				continue;
			}

			Pixels[PY * TextureSize + PX] = Color;
		}
	}
}

void UReplayFrameRenderer::DrawLine(int32 X0, int32 Y0, int32 X1, int32 Y1, int32 Thickness, const FColor& Color)
{
	// Bresenham with a square brush, same as the live minimap's camera rectangle.
	const int32 DX = FMath::Abs(X1 - X0);
	const int32 DY = FMath::Abs(Y1 - Y0);
	const int32 StepX = (X0 < X1) ? 1 : -1;
	const int32 StepY = (Y0 < Y1) ? 1 : -1;
	const int32 HalfThickness = FMath::Max(1, Thickness) / 2;

	int32 Error = DX - DY;
	int32 X = X0;
	int32 Y = Y0;

	while (true)
	{
		for (int32 BrushY = -HalfThickness; BrushY <= HalfThickness; ++BrushY)
		{
			const int32 PY = Y + BrushY;
			if (PY < 0 || PY >= TextureSize)
			{
				continue;
			}

			for (int32 BrushX = -HalfThickness; BrushX <= HalfThickness; ++BrushX)
			{
				const int32 PX = X + BrushX;
				if (PX < 0 || PX >= TextureSize)
				{
					continue;
				}

				Pixels[PY * TextureSize + PX] = Color;
			}
		}

		if (X == X1 && Y == Y1)
		{
			break;
		}

		const int32 DoubleError = 2 * Error;
		if (DoubleError > -DY)
		{
			Error -= DY;
			X += StepX;
		}
		if (DoubleError < DX)
		{
			Error += DX;
			Y += StepY;
		}
	}
}
