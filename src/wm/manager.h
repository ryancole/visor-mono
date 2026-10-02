#pragma once

#include "wm/config.h"
#include "wm/layout.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <map>

namespace visor::wm {

// Tiles app windows on each monitor with a dwindle layout, inside the
// monitor's work area (what is left after app bars such as Visor's).
//
// Event-driven: window show/hide/destroy, cloak, minimise, focus and
// move/size events come from WinEvent hooks; display and work-area changes
// from broadcasts to a hidden window. Windows are only moved, never hidden
// or restyled, so if visor-wm stops they simply stay where they are.
class WindowManager : public QObject
{
    Q_OBJECT

public:
    explicit WindowManager(Config config, QObject *parent = nullptr);
    // Puts window border colours back to the system default.
    ~WindowManager() override;

    // Applies a reloaded config: gaps, layout options and colours take effect
    // at once. Rules apply to windows as they open, as in Hyprland.
    void setConfig(Config config);

    // Called from the WinEvent hook / the hidden window's procedure.
    void handleEvent(unsigned event, quintptr hwnd);
    std::intptr_t handleMessage(void *window, unsigned msg, std::uintptr_t wParam, std::intptr_t lParam);

private:
    struct Monitor
    {
        Rect work; // physical px
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
        bool held = false; // maximised or fullscreen: keeps its tile, isn't moved
    };

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

    Config m_config;
    void *m_hwnd = nullptr; // hidden window for display/settings broadcasts
    QList<void *> m_hooks;
    QHash<QString, Monitor> m_monitors;          // by device name, e.g. \\.\DISPLAY1
    std::map<QString, Workspace> m_workspaces;   // one per monitor (for now)
    QHash<quintptr, Managed> m_managed;
    QSet<quintptr> m_colored;                    // windows whose border we've set
    quintptr m_active = 0;
    QTimer m_settleTimer;
};

} // namespace visor::wm
