#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

class QWinEventNotifier;

// visor's end of the link to visor-shell, the shell services process that
// replaces Explorer. Process-wide (owned by App) so its state survives config
// reloads; QML sees it through types like `Tasks`.
//
// When visor runs without visor-shell (e.g. under Explorer) it simply stays
// disconnected and everything built on it is empty.
//
// Protocol: WM_COPYDATA with a JSON payload between two hidden windows; see
// src/common/linkprotocol.h.
class ShellLink : public QObject
{
    Q_OBJECT

public:
    struct Task
    {
        quintptr hwnd = 0;
        QString title;
        quint32 pid = 0;
        QString path;
        bool flashing = false;
    };

    // A notification-area icon hosted by visor-shell.
    struct TrayIcon
    {
        int id = 0;
        quint32 pid = 0;
        QString tip;
        quintptr icon = 0; // HICON owned by visor-shell
        bool hidden = false;
    };

    explicit ShellLink(QObject *parent = nullptr);
    ~ShellLink() override;

    // Null until App creates it.
    static ShellLink *instance();

    bool connected() const { return m_shell != nullptr; }
    const QList<Task> &tasks() const { return m_tasks; }
    quintptr activeTask() const { return m_active; }
    const QList<TrayIcon> &trayIcons() const { return m_trayIcons; }

    // Sends a message to visor-shell; dropped when not connected.
    void send(const QJsonObject &message);

    // Lets `pid` take the foreground, as Explorer does before forwarding a
    // tray or taskbar click. Clicks on visor's bars (WS_EX_NOACTIVATE) don't
    // give visor foreground rights of its own, so visor first takes the
    // foreground with its hidden link window, then passes the rights on.
    // Call only in response to a user click.
    void grantForeground(quint32 pid);

    // Window procedure hook; returns true if the message was handled.
    bool handleMessage(unsigned msg, unsigned long long wParam, long long lParam);

signals:
    void connectedChanged();
    void tasksReset();
    void taskAdded(const ShellLink::Task &task);
    void taskChanged(const ShellLink::Task &task);
    void taskRemoved(quintptr hwnd);
    void activeTaskChanged();
    void trayReset();
    void trayIconAdded(const ShellLink::TrayIcon &icon);
    void trayIconChanged(const ShellLink::TrayIcon &icon);
    void trayIconRemoved(int id);
    // visor-shell is handing the session to Explorer.
    void quitRequested();

private:
    void hello();
    void disconnect();
    void onMessage(const QByteArray &json);

    void *m_hwnd = nullptr;
    void *m_shell = nullptr;          // visor-shell's link window
    void *m_shellProcess = nullptr;   // watched so we notice it going away
    QWinEventNotifier *m_shellExit = nullptr;
    QList<Task> m_tasks;
    quintptr m_active = 0;
    QList<TrayIcon> m_trayIcons;
};
