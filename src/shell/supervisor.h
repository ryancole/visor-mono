#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QWinEventNotifier;

namespace visor {

// Starts a helper program that sits next to visor-shell (e.g. visor-wm.exe)
// and restarts it when it crashes, at most 5 times in 2 minutes. Exit code 0
// is a deliberate quit and isn't restarted. The helper is not stopped when
// the shell exits; it watches the shell (--shell-pid) and exits itself.
class Supervisor : public QObject
{
    Q_OBJECT

public:
    Supervisor(QString exe, QStringList arguments, QObject *parent = nullptr);
    ~Supervisor() override;

    void start();

signals:
    // The program exited (crashed or quit); it may be restarted.
    void exited();

private:
    void launch();
    void onExited();

    QString m_exe;
    QStringList m_arguments;
    void *m_process = nullptr;
    unsigned long m_pid = 0;
    QWinEventNotifier *m_notifier = nullptr;
    QList<qint64> m_crashTimes; // ms since m_clock started
    QElapsedTimer m_clock;
};

} // namespace visor
