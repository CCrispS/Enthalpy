#include "ShpGhost.h"

#include <Surface.h>

namespace
{
	// Addresses taken from the engine build used by this project (gamemd.exe 1.001).
	using HasCompressionFn = bool(__thiscall*)(void* self, int frame);
	using GetPixelsFn = BYTE*(__thiscall*)(void* self, int frame);
	using GetFrameBoundsFn = void*(__thiscall*)(void* self, void* pBuffer, int frame);
	using LoadFn = void(__thiscall*)(void* self);

	constexpr DWORD kHasCompressionAddress = 0x69E900;
	constexpr DWORD kGetPixelsAddress = 0x69E740;
	constexpr DWORD kGetFrameBoundsAddress = 0x69E7E0;
	constexpr DWORD kLoadAddress = 0x69E090;

	// Header of a shape file; only these four fields are needed and they come first in
	// both the stored and the cached-reference forms.
	struct ShpHeader
	{
		WORD Type;
		short Width;
		short Height;
		short Frames;
	};

	DWORD Blend565(DWORD dst, DWORD src, int srcEighths)
	{
		const int inverse = 8 - srcEighths;

		const int dr = (dst >> 11) & 0x1F;
		const int dg = (dst >> 5) & 0x3F;
		const int db = dst & 0x1F;

		const int sr = (src >> 11) & 0x1F;
		const int sg = (src >> 5) & 0x3F;
		const int sb = src & 0x1F;

		const int r = (dr * inverse + sr * srcEighths) >> 3;
		const int g = (dg * inverse + sg * srcEighths) >> 3;
		const int b = (db * inverse + sb * srcEighths) >> 3;

		return (r << 11) | (g << 5) | b;
	}

	int IRound(double value)
	{
		return static_cast<int>(value < 0.0 ? value - 0.5 : value + 0.5);
	}
}

bool ShpGhost::IsReadable(void* pShp, int frame)
{
	if (!pShp || frame < 0)
		return false;

	const auto pHeader = static_cast<const ShpHeader*>(pShp);

	if (pHeader->Type != 0xFFFF
		&& (pHeader->Frames <= 0 || frame >= pHeader->Frames))
	{
		return false;
	}

	if (pHeader->Width < 0 || pHeader->Width > 400 || pHeader->Height < 0
		|| pHeader->Height > 400)
	{
		return false;
	}

	return true;
}

void ShpGhost::Draw(Surface* pSurface, void* pShp, int frame, int anchorX, int anchorY,
	DWORD tint, int opacity, int minX, int minY, int maxX, int maxY)
{
	if (!pSurface || !pShp)
		return;

	const auto pHeader = static_cast<const ShpHeader*>(pShp);
	int useFrame = frame;

	if (pHeader->Type == 0xFFFF)
	{
		// A cached reference has to be loaded before any of its frame data is valid.
		const auto load = reinterpret_cast<LoadFn>(kLoadAddress);
		load(pShp);
		useFrame = 0;
	}
	else if (pHeader->Frames <= 0 || useFrame >= pHeader->Frames)
	{
		useFrame = 0;
	}

	if (useFrame < 0)
		return;

	const auto hasCompression = reinterpret_cast<HasCompressionFn>(kHasCompressionAddress);
	if (hasCompression(pShp, useFrame))
		return;

	RectangleStruct bounds = { 0, 0, 0, 0 };
	const auto getFrameBounds = reinterpret_cast<GetFrameBoundsFn>(kGetFrameBoundsAddress);
	getFrameBounds(pShp, &bounds, useFrame);

	if (bounds.Width <= 0 || bounds.Height <= 0 || bounds.Width > 400 || bounds.Height > 400)
		return;

	const auto getPixels = reinterpret_cast<GetPixelsFn>(kGetPixelsAddress);
	const BYTE* pPixels = getPixels(pShp, useFrame);
	if (!pPixels)
		return;

	// Units stand on their cell, so the shape is anchored bottom centre.
	const int originX = anchorX - bounds.Width / 2;
	const int originY = anchorY - bounds.Height + 6;

	Point2D pixel = { 0, 0 };

	for (int y = 0; y < bounds.Height; ++y)
	{
		const int screenY = originY + y;
		if (screenY < minY || screenY > maxY)
			continue;

		const BYTE* pRow = pPixels + static_cast<size_t>(y) * bounds.Width;

		for (int x = 0; x < bounds.Width; ++x)
		{
			// Index zero is the transparent background of a shape frame.
			if (pRow[x] == 0)
				continue;

			const int screenX = originX + x;
			if (screenX < minX || screenX > maxX)
				continue;

			pixel.X = static_cast<short>(screenX);
			pixel.Y = static_cast<short>(screenY);

			const DWORD existing = pSurface->GetPixel(&pixel);
			pSurface->SetPixel(&pixel, Blend565(existing, tint, opacity));
		}
	}
}
