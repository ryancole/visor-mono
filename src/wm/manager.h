#pragma once

#include "wm/config.h"
#include "wm/keyhook.h"
#include "wm/layout.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <map>
#include <memory>

namespace visor::wm {

// Tiles app windows on each monitor with a dwindle layout, inside the
// monitor's work area (what is left after app bars such as Visor's).
//
// Event-driven: window show/hide/destroy, cloak, minimise, focus and
// move/size events come from WinEvent hooks; display and work-area changes
// from broadcasts to a hidden window. Windows are only moved, never hidden
// or restyled, so if visor-wm stops they simply stay where they are.
//
// Key bindings from the config are global hotkeys (RegisterHotKey) on the
// hidden window, or, for keys Windows reserves, caught by a KeyHook; each
// runs a Hyprland-style dispatcher on the focused window.
class WindowManager : public QObject
{
    Q_OBJECT

public:
    explicit WindowManager(Config config, QObject *parent = nullptr);
    // Puts window border colours back to the system default.
    ~WindowManager() override;

    // Applies a reloaded config: gaps, layout options, colours and key
    // bindings take effect at once. Rules apply to windows as they open, as
    // in Hyprland.
    void setConfig(Config config);

    // Called from the WinEvent hook / the hidden window's procedure.
    void handleEvent(unsigned event, quintptr hwnd);
    std::intptr_t handleMessage(void *window, unsigned msg, std::uintptr_t wParam, std::intptr_t lParam);

private:
    struct Monitor
    {
        Rect full; // physical px
        Rect work; // without app bars
        bool primary = false;
    };
    struct Workspace
    {
        DwindleLayout layout;
        quintptr lastFocused = 0;
    };
    struct Managed
    {
        QString monitor;
        bool held = false;       // maximised or fullscreen: keeps its tile, isn't moved
        bool fullscreen = false; // our `fullscreen 0`: covers the monitor, keeps its tile
    };
    enum class Direction { Left, Right, Up, Down };

    void refreshMonitors();
    QString monitorOf(quintptr hwnd) const;
    QString primaryMonitor() const;
    Workspace &workspace(const QString &monitor);

    // Tiles or untiles `hwnd` if its eligibility changed.
    void consider(quintptr hwnd);
    void manage(quintptr hwnd);
    void unmanage(quintptr hwnd);
    void moveToMonitor(quintptr hwnd, const QString &monitor);

    void arrange(const QString &monitor);
    void arrangeAll();
    // Some apps place their own window just after showing it (restoring a
    // saved position); lay out again once they've settled.
    void settleSoon();

    void focusChanged(quintptr hwnd);
    void colorBorder(quintptr hwnd, bool active);

    void registerBindings();
    void unregisterBindings();
    void dispatch(const Binding &binding);
    void killActive();
    void toggleFloating();
    void fullscreen(bool maximizeOnly);
    void moveFocus(Direction direction);
    void swapWindow(Direction direction);
    void toggleSplit();
    void resizeActive(int dx, int dy);
    // The tiled window nearest to `from` in `direction` (any monitor).
    quintptr neighbor(const Rect &from, Direction direction, quintptr exclude) const;

    Config m_config;
    void *m_hwnd = nullptr; // hidden window for display/settings broadcasts
    QList<void *> m_hooks;
    QHash<QString, Monitor> m_monitors;          // by device name, e.g. \\.\DISPLAY1
    std::map<QString, Workspace> m_workspaces;   // one per monitor (for now)
    QHash<quintptr, Managed> m_managed;
    QHash<quintptr, Rect> m_tiles;               // each tiled window's tile, from the last arrange
    QHash<quintptr, bool> m_tileOverride;        // togglefloating: true = tile, false = float
    QHash<quintptr, Rect> m_floatFullscreen;     // floating windows in fullscreen 0: their old frame
    qsizetype m_registeredBindings = 0;
    std::unique_ptr<KeyHook> m_keyHook;
    QSet<quintptr> m_colored;                    // windows whose border we've set
    quintptr m_active = 0;
    quintptr m_previousActive = 0;
    QHash<quintptr, quint64> m_focusedAt; // focus order, for ties in movefocus
    quint64 m_focusCount = 0;
    QTimer m_settleTimer;
    // Apps with themed frames (e.g. Windows Terminal) set their own border
    // colour when they're activated, after our focus event; colour again once
    // they're done.
    QTimer m_recolorTimer;
};

} // namespace visor::wm
