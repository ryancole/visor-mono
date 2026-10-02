#pragma once

#include <QObject>

#include <cstdint>

namespace visor {

// Explorer's taskbar, in hosted mode: asked to auto-hide while visor-shell
// runs, through ABM_SETSTATE (the documented way to set the taskbar's
// auto-hide state), so Visor's bar is the one on screen and the taskbar is a
// hover away; put back on a clean exit. Shell_TrayWnd itself is left alone.
//
// Auto-hide is Explorer's own setting (Settings > Personalization >
// Taskbar) and persists, so it would outlive a crash. The original state is
// recorded under HKCU\Software\visor-shell the first time it is changed,
// kept through restarts and crashes, and removed once a clean exit has
// restored it. Re-applied when Explorer restarts (TaskbarCreated). Not
// touched when the session ends: Explorer is going away too.
class Taskbar : public QObject
{
    Q_OBJECT

public:
    explicit Taskbar(QObject *parent = nullptr);
    ~Taskbar() override;

    // Window procedure body; called from the Win32 window procedure.
    std::intptr_t handleMessage(void *window, unsigned msg, std::uintptr_t wParam, std::intptr_t lParam);

private:
    void hide();
    void restore();

    void *m_hwnd = nullptr;
    bool m_sessionEnding = false;
};

} // namespace visor
