#include "shell/supervisor.h"

#include "common/exitcodes.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>
#include <QWinEventNotifier>

#include <windows.h>

namespace visor {

namespace {

constexpr int kMaxCrashes = 5;
constexpr qint64 kCrashWindowMs = 120'000;
constexpr int kRestartDelayMs = 1'000;

} // namespace

Supervisor::Supervisor(QString exe, QStringList arguments, QObject *parent)
    : QObject(parent)
    , m_exe(std::move(exe))
    , m_arguments(std::move(arguments))
{
    m_clock.start();
}

Supervisor::~Supervisor()
{
    delete m_notifier; // before its handle (see onExited)
    if (m_process)
        CloseHandle(static_cast<HANDLE>(m_process));
}

void Supervisor::start()
{
    launch();
}

void Supervisor::launch()
{
    const QString path = QDir(QCoreApplication::applicationDirPath()).filePath(m_exe);
    if (!QFileInfo::exists(path)) {
        qInfo() << m_exe << "not deployed at" << path;
        return;
    }
    qint64 pid = 0;
    if (!QProcess::startDetached(path, m_arguments, QFileInfo(path).absolutePath(), &pid)) {
        qWarning() << "failed to start" << path;
        return;
    }
    m_pid = DWORD(pid);
    m_process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, m_pid);
    if (!m_process) {
        qWarning() << "cannot watch" << m_exe << "pid" << pid << "error" << GetLastError();
        return;
    }
    qInfo() << "started" << m_exe << "pid" << pid;
    m_notifier = new QWinEventNotifier(m_process, this);
    connect(m_notifier, &QWinEventNotifier::activated, this, &Supervisor::onExited);
}

void Supervisor::onExited()
{
    DWORD code = 0;
    GetExitCodeProcess(static_cast<HANDLE>(m_process), &code);
    qInfo() << m_exe << "(pid" << m_pid << ") exited with code" << code;

    // Inside the notifier's own signal: delete it later, and keep the handle
    // open until then (its thread-pool wait still uses it).
    const HANDLE process = static_cast<HANDLE>(m_process);
    m_notifier->setEnabled(false);
    connect(m_notifier, &QObject::destroyed, [process] { CloseHandle(process); });
    m_notifier->deleteLater();
    m_notifier = nullptr;
    m_process = nullptr;
    m_pid = 0;
    emit exited();

    if (code == 0 || code == DWORD(exitcode::AlreadyRunning))
        return;

    const qint64 now = m_clock.elapsed();
    m_crashTimes.append(now);
    while (!m_crashTimes.isEmpty() && now - m_crashTimes.first() > kCrashWindowMs)
        m_crashTimes.removeFirst();
    if (m_crashTimes.size() >= kMaxCrashes) {
        qWarning() << m_exe << "crashed" << m_crashTimes.size() << "times in 2 minutes; giving up";
        return;
    }
    QTimer::singleShot(kRestartDelayMs, this, [this] {
        if (!m_process)
            launch();
    });
}

} // namespace visor
