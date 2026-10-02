#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QUuid>

#include <cstdint>

namespace visor {

class AppBars;

// The notification area's host: visor-shell becomes Shell_TrayWnd, the window
// Shell_NotifyIcon and SHAppBarMessage talk to (WM_COPYDATA). Icons are kept
// here (with our own copy of each HICON) and shown by Visor; clicks in Visor
// come back here and are forwarded to the owning app the way Explorer does.
// Replace mode only.
class TrayHost : public QObject
{
    Q_OBJECT

public:
    struct Icon
    {
        int id = 0;            // ours; stable for the icon's lifetime
        quintptr hwnd = 0;     // owner window
        quint32 uid = 0;
        QUuid guid;            // identity instead of hwnd+uid when set
        quint32 callback = 0;  // owner's callback message
        quint32 version = 0;   // NOTIFYICON_VERSION(_4), from NIM_SETVERSION
        quint32 pid = 0;
        quintptr icon = 0;     // HICON copy, owned by us
        QString tip;
        bool hidden = false;
    };

    explicit TrayHost(AppBars *appBars, QObject *parent = nullptr);
    ~TrayHost() override;

    QList<Icon> icons() const;

    // A click in Visor at screen position (x, y), physical pixels.
    // button: "left", "right", "middle" or "double".
    void click(int id, const QString &button, int x, int y);

    // Window procedure body; called from the Win32 window procedure.
    std::intptr_t handleMessage(void *window, unsigned msg, std::uintptr_t wParam, std::intptr_t lParam);

signals:
    void iconAdded(const visor::TrayHost::Icon &icon);
    void iconChanged(const visor::TrayHost::Icon &icon);
    void iconRemoved(int id);

private:
    bool notifyIcon(unsigned message, const void *data, unsigned long size);
    std::intptr_t iconRect(const void *data, unsigned long size);
    int find(quintptr hwnd, quint32 uid, const QUuid &guid) const;
    void remove(int index);
    void pruneDeadOwners();
    void placeWindow();

    AppBars *m_appBars;
    void *m_hwnd = nullptr;
    void *m_notifyHwnd = nullptr;
    QList<Icon> m_icons;
    int m_nextId = 1;
    // Where the last click happened; apps ask for their icon's rect right
    // after a click to position flyouts.
    int m_lastClickId = 0;
    long m_lastClickX = 0;
    long m_lastClickY = 0;
};

} // namespace visor
