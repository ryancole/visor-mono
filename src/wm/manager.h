#pragma once

#include "wm/config.h"
#include "wm/keyhook.h"
#include "wm/layout.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <QSize>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace visor::wm {

// Tiles app windows on each monitor with a dwindle layout, inside the
// monitor's work area (what is left after app bars such as Visor's).
//
// Event-driven: window show/hide/destroy, cloak, minimise, focus and
// move/size events come from WinEvent hooks; display and work-area changes
// from broadcasts to a hidden window. Windows are only moved, shown and
// hidden, never restyled.
//
// Desktops work like Windows 11's virtual desktops (which live in Explorer,
// so they're gone when visor-shell replaces it): as many as you create,
// named "Desktop N", each spanning every monitor. Every app window belongs to
// one; switching hides the old desktop's windows (and the windows they own)
// and shows the new one's. The desktops and the windows hidden on them are
// recorded in a file, so a visor-wm started after this one died (or was
// replaced, see handOver) picks them up again instead of losing them; a
// clean exit shows every window straight away.
//
// Key bindings from the config are global hotkeys (RegisterHotKey) on the
// hidden window, or, for keys Windows reserves, caught by a KeyHook; each
// runs a Hyprland-style dispatcher on the focused window.
class WindowManager : public QObject
{
    Q_OBJECT

public:
    explicit WindowManager(Config config, QObject *parent = nullptr);
    // Shows windows hidden on other desktops (unless handed over) and puts
    // window border colours back to the system default.
    ~WindowManager() override;

    // On exit, leave windows on other desktops hidden and recorded, for the
    // visor-wm a restarted visor-shell starts next.
    void handOver() { m_handOver = true; }

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
        double scale = 1.0; // DPI / 96: gaps are in logical px, as in Hyprland
        bool primary = false;
    };
    struct Workspace // one desktop's windows on one monitor
    {
        DwindleLayout layout;
        quintptr lastFocused = 0;
    };
    struct Desktop
    {
        std::map<QString, Workspace> monitors; // by monitor device name
        quintptr lastFocused = 0;              // on any monitor
    };
    struct Managed
    {
        Desktop *desktop = nullptr;
        QString monitor;
        bool held = false;       // maximised or fullscreen: keeps its tile, isn't moved
        bool fullscreen = false; // our `fullscreen 0`: covers the monitor, keeps its tile
    };
    enum class Direction { Left, Right, Up, Down };

    void refreshMonitors();
    QString monitorOf(quintptr hwnd) const;
    QString primaryMonitor() const;
    Desktop *current() const { return m_desktops[size_t(m_current)].get(); }
    int indexOf(const Desktop *desktop) const;
    // The current desktop's workspace on `monitor`, or a window's own.
    Workspace &workspace(const QString &monitor) { return current()->monitors[monitor]; }
    Workspace &workspaceOf(const Managed &m) { return m.desktop->monitors[m.monitor]; }

    // Tracks, tiles or untiles `hwnd` if its eligibility changed.
    void consider(quintptr hwnd);
    void track(quintptr hwnd);
    void untrack(quintptr hwnd);
    void manage(quintptr hwnd);
    void unmanage(quintptr hwnd);
    void moveToMonitor(quintptr hwnd, const QString &monitor);
    // The monitor next to `monitor` in `direction`, or empty.
    QString monitorInDirection(const QString &monitor, Direction direction) const;

    void arrange(const QString &monitor);
    void arrangeAll();
    // Learns minimum sizes: a window that ended up bigger than we asked
    // won't go smaller, so later layouts give it that much room (kept
    // on-screen) instead of letting it spill over the edge. Returns true if
    // anything was learnt.
    bool learnMinimumSizes();
    // Some apps place their own window just after showing it (restoring a
    // saved position); lay out again once they've settled.
    void settleSoon();

    void focusChanged(quintptr hwnd);
    void colorBorder(quintptr hwnd, bool active);

    // Desktops.
    void activateDesktop(int index);
    void newDesktop();
    void closeDesktop();
    void moveToDesktop(quintptr hwnd, int index, bool follow);
    // Resolves a `workspace` argument: N (1-based), e+1/+1, e-1/-1. -1 if none.
    int desktopIndex(const QString &argument) const;
    void hideWindows(const QSet<quintptr> &roots, Desktop *desktop);
    void showWindows(Desktop *desktop);
    void focusDesktop(Desktop *desktop);
    void saveState() const;
    void restoreState();
    QJsonObject desktopState() const;
    void sendState();
    // The key bindings, for Visor's cheat sheet (linkprotocol.h).
    void sendBindings();
    void sendToShell(const QJsonObject &message);

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
    // The tiled window nearest to `from` in `direction` (any monitor) on the
    // current desktop.
    quintptr neighbor(const Rect &from, Direction direction, quintptr exclude) const;

    Config m_config;
    void *m_hwnd = nullptr; // hidden window: broadcasts, hotkeys, messages from the shell
    QList<void *> m_hooks;
    QHash<QString, Monitor> m_monitors;          // by device name, e.g. \\.\DISPLAY1
    std::vector<std::unique_ptr<Desktop>> m_desktops;
    int m_current = 0;
    QHash<quintptr, Desktop *> m_desktopOf;      // every app window we know (tiled or floating)
    QHash<quintptr, Desktop *> m_hidden;         // windows we hid, and whose desktop they're on
    QSet<quintptr> m_expectHide;                 // our own pending hides/shows, so their
    QSet<quintptr> m_expectShow;                 // events aren't taken for the app's
    QHash<quintptr, Managed> m_managed;
    QHash<quintptr, Rect> m_tiles;               // each tiled window's tile, from the last arrange
    QHash<quintptr, Rect> m_placed;              // where we last put it (the tile, or more if it needs it)
    QHash<quintptr, QSize> m_minimumSize;        // learnt by learnMinimumSizes
    bool m_handOver = false;
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
    QTimer m_stateTimer; // coalesces desktop state updates to Visor
    QTimer m_learnTimer; // after a layout: learnMinimumSizes once windows have resized
};

} // namespace visor::wm
