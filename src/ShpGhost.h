#pragma once

#include <Windows.h>

class Surface;

// The YRpp copy used by this project only forward declares SHPStruct, so the handful of
// engine entry points needed to read frame data are called through their addresses.
namespace ShpGhost
{
	// True when the shape can be read pixel by pixel (stored, not RLE compressed).
	bool IsReadable(void* pShp, int frame);

	// Draws the frame as a translucent silhouette tinted with the given RGB565 colour.
	// The shape is anchored bottom centre on pAnchor, the way a unit stands on the ground.
	void Draw(Surface* pSurface, void* pShp, int frame, int anchorX, int anchorY, DWORD tint,
		int opacity, int minX, int minY, int maxX, int maxY);
}
