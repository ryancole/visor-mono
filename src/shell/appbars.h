#pragma once

#include <QList>
#include <QObject>

#include <cstdint>

namespace visor {

// The server side of SHAppBarMessage, which Explorer normally provides: a
// registry of docked app bars (Visor's bars, and any other app's), the
// per-monitor work area they reserve, and fullscreen-app notifications
// (ABN_FULLSCREENAPP) so bars can get out of the way.
//
// Messages arrive as WM_COPYDATA (dwData 0) at Shell_TrayWnd; TrayHost hands
// them here. Replace mode only: under Explorer, Explorer is the server.
class AppBars : public QObject
{
    Q_OBJECT

public:
    explicit AppBars(QObject *parent = nullptr);
    // Gives every monitor its full area back.
    ~AppBars() override;

    // Handles one APPBARMSGDATAV3 message (`size` bytes at `data`) and
    // returns the result for the WM_COPYDATA sender.
    std::intptr_t handle(const void *data, unsigned long size);

    // The rect Shell_TrayWnd should occupy: the top bar on the primary
    // monitor (where the tray UI is), or a zero-height strip at its top.
    // Physical pixels, screen coordinates.
    struct Rect { long left, top, right, bottom; };
    Rect trayRect() const;

    // Re-evaluates whether the foreground window is fullscreen on its
    // monitor and tells that monitor's bars when it changes.
    void checkFullscreen();
    // Display layout changed: drop dead bars, ask the rest to re-dock.
    void displayChanged();
    // Drops bars whose windows no longer exist (e.g. their app crashed).
    void prune();

signals:
    void trayRectChanged();

private:
    struct Bar
    {
        quintptr hwnd = 0;
        unsigned callback = 0;
        unsigned edge = 0;
        Rect rect{};
        bool positioned = false;
    };

    int indexOf(quintptr hwnd) const;
    Rect adjust(const Bar &bar, Rect proposed) const;
    void applyWorkAreas();
    void notifyOthers(quintptr except, unsigned code, std::intptr_t lParam = 0);

    QList<Bar> m_bars; // registration order: earlier bars sit closer to the edge
    QList<quintptr> m_fullscreenMonitors;
};

} // namespace visor
