#pragma once

#include "wm/layout.h"

#include <QString>

namespace visor::wm {

struct Config;

// Win32 helpers. Windows are passed as quintptr (HWND), keeping <windows.h>
// out of headers as elsewhere in the repo.
namespace win {

QString className(quintptr hwnd);
QString title(quintptr hwnd);
QString exeName(quintptr hwnd); // e.g. "notepad.exe"; empty if unknown

enum class Kind {
    Ignore, // not an app window: hidden, child, tool window, ours, ...
    Float,  // an app window left where it is (dialogs, fixed-size, rules)
    Tile,
};

// Decides whether a window is tiled. `reason` explains Float/Ignore for the
// log. The rules from the config apply to app windows (not to Ignore ones),
// except that windows of elevated processes always float: Windows won't let
// a normal process move them.
Kind classify(quintptr hwnd, const Config &config, QString *reason = nullptr);

bool isMinimized(quintptr hwnd);
bool isMaximized(quintptr hwnd);
// Covers its whole monitor with no frame (games, video, F11 in browsers).
bool isFullscreen(quintptr hwnd);
bool exists(quintptr hwnd);

// Restores a maximised window without activating it.
void unmaximize(quintptr hwnd);
// Maximises the window, or restores it if it already is.
void toggleMaximized(quintptr hwnd);

// The visible frame (without the invisible resize borders).
Rect frameRect(quintptr hwnd);
quintptr foreground();
// Makes the window the foreground window. visor-wm may do this while it
// handles a hotkey: the process that received the last input can.
bool focus(quintptr hwnd);
// Asks the window to close, as its close button would.
void close(quintptr hwnd);
// Brings the window to the top of the z-order without activating it.
void raise(quintptr hwnd);

bool isVisible(quintptr hwnd);
quint32 processId(quintptr hwnd);
quintptr owner(quintptr hwnd);     // 0 if unowned
quintptr rootOwner(quintptr hwnd); // the end of the owner chain (itself if unowned)
// The desktop window (visor-shell's, or Explorer's).
quintptr shellWindow();
// Hides / shows (in its current state, without activating) asynchronously.
void hide(quintptr hwnd);
void show(quintptr hwnd);

// Moves and sizes the window so its *visible* frame fills `rect`. Windows 10+
// windows have invisible resize borders around the visible frame; those are
// added back so tiles line up exactly. Asynchronous: a hung app can't block
// us. Returns false if nothing needed to change.
bool moveTo(quintptr hwnd, const Rect &rect);

// The colour Windows 11 draws the window's 1 px border with (0xRRGGBB), or
// the system default with resetBorderColor. No-op on Windows 10.
void setBorderColor(quintptr hwnd, quint32 rgb);
void resetBorderColor(quintptr hwnd);
// Windows' accent colour (what Settings > Colors sets), 0xRRGGBB.
quint32 accentColor();

} // namespace win
} // namespace visor::wm
