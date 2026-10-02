#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QTimer>

#include <cstdint>

class QWinEventNotifier;

namespace visor {

// visor-shell's end of the link to Visor (protocol: common/linkprotocol.h),
// and Visor's supervisor:
//
//   - start(true) launches Visor (visor.exe next to us) unless a running
//     Visor connects first.
//   - Whichever Visor process is connected is watched. A crash relaunches it
//     (at most 5 times in 2 minutes). A clean exit is either a quit or one of
//     Visor's self-restarts (renderer change), so it waits for a successor to
//     connect instead of relaunching.
class VisorLink : public QObject
{
    Q_OBJECT

public:
    explicit VisorLink(QObject *parent = nullptr);
    ~VisorLink() override;

    // Announces the link so a running Visor connects. With launchVisor, also
    // starts Visor if none connects and supervises it.
    void start(bool launchVisor);
    // Asks Visor to quit and stops supervising it (handing over to Explorer).
    void stopVisor();

    bool connected() const { return m_client != nullptr; }
    void send(const QJsonObject &message);
    // To visor-wm's window, if it is running.
    void sendToWm(const QJsonObject &message);

    // Window procedure body; called from the Win32 window procedure.
    std::intptr_t handleMessage(void *window, unsigned msg, std::uintptr_t wParam, std::intptr_t lParam);

signals:
    // A Visor said hello (first start, restart, or reconnect).
    void clientConnected();
    // The connected Visor exited.
    void clientDisconnected();
    // Any other message from Visor (e.g. "tray.click").
    void messageReceived(const QJsonObject &message);
    // A message from visor-wm (desktop state, key bindings, a command for Visor).
    void wmMessageReceived(const QJsonObject &message);

private:
    void launch();
    void watch(unsigned long pid);
    void onProcessExited();

    void *m_hwnd = nullptr;
    void *m_client = nullptr;    // Visor's link window
    void *m_process = nullptr;   // handle of the Visor process being watched
    unsigned long m_pid = 0;
    QWinEventNotifier *m_notifier = nullptr;
    QTimer m_successorTimer;     // after a clean exit: wait for a new Visor
    QList<qint64> m_crashTimes;  // ms since m_clock started
    QElapsedTimer m_clock;
    bool m_supervise = false;
    bool m_stopping = false;
};

} // namespace visor
