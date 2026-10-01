#pragma once

struct HDC__;
struct tagRECT;

namespace visor {

// Paints the user's wallpaper (HKCU\Control Panel\Desktop: WallPaper,
// WallpaperStyle, TileWallpaper) and background colour into a DC covering the
// virtual screen, the way Explorer would. Only the parts of `paintRect` (client
// coordinates) are drawn.
//
// Nothing is cached: the image is decoded and scaled with WIC on each paint.
// The window's DWM surface keeps the result, and the desktop only repaints on
// show, display, and wallpaper changes, so this costs no resident memory.
void paintWallpaper(HDC__ *dc, const tagRECT &paintRect);

} // namespace visor
