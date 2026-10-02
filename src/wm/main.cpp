// visor-wm: the tiling window manager of a Visor desktop (Hyprland's role in
// Omarchy). Started and supervised by visor-shell in replace mode; can also be
// run by hand for testing.
//
//   visor-wm [--config <wm.conf>] [--shell-pid <pid>]
//
// With --shell-pid it exits when that process does, so a restarted shell
// starts a fresh visor-wm rather than finding a stale one. If a new shell
// appears within a few seconds (visor-session restarted it), windows on other
// desktops stay hidden and are handed over to the visor-wm it starts;
// otherwise (e.g. the session went back to Explorer) every window is shown.

#include "common/exitcodes.h"
#include "common/linkprotocol.h"
#include "common/log.h"
#include "wm/config.h"
#include "wm/manager.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDebug>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QWinEventNotifier>

#include <windows.h>

#include <memory>

namespace {

// A restarted shell can start us while the previous visor-wm is still
// shutting down, so wait a moment for the instance mutex.
constexpr DWORD kInstanceWaitMs = 3'000;
constexpr int kReloadDelayMs = 150;
constexpr int kNewShellWaitMs = 5'000;
constexpr int kNewShellPollMs = 100;

visor::wm::Config loadConfig(const QString &path)
{
    if (path.isEmpty()) {
        qInfo() << "no wm.conf found; using defaults";
        return {};
    }
    visor::wm::Config config = visor::wm::Config::load(path);
    qInfo().noquote() << "config" << path << "-" << config.rules.size() << "window rules";
    for (const QString &error : std::as_const(config.errors))
        qWarning().noquote() << "wm.conf" << error;
    return config;
}

} // namespace

int main(int argc, char *argv[])
{
    // Physical pixels everywhere, so window rects and monitor work areas
    // agree on every monitor whatever its scale.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("visor-wm"));
    QCoreApplication::setApplicationVersion(QStringLiteral(VISOR_VERSION));
    visor::installLogHandler(QStringLiteral("wm"));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption configOption(QStringLiteral("config"), QStringLiteral("Path to wm.conf."),
                                    QStringLiteral("path"));
    QCommandLineOption shellOption(QStringLiteral("shell-pid"), QStringLiteral("Exit when this process exits."),
                                   QStringLiteral("pid"));
    parser.addOption(configOption);
    parser.addOption(shellOption);
    parser.process(app);

    qInfo() << "visor-wm" << VISOR_VERSION << "starting, pid" << QCoreApplication::applicationPid();

    // One visor-wm per session; released when we exit.
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\visor-wm.instance");
    const DWORD wait = mutex ? WaitForSingleObject(mutex, kInstanceWaitMs) : WAIT_FAILED;
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
        qWarning() << "visor-wm is already running in this session";
        return visor::exitcode::AlreadyRunning;
    }

    // Exit with the shell that started us.
    HANDLE shell = nullptr;
    if (parser.isSet(shellOption)) {
        shell = OpenProcess(SYNCHRONIZE, FALSE, parser.value(shellOption).toULong());
        if (!shell) {
            qWarning() << "shell process" << parser.value(shellOption) << "is gone; exiting";
            return 0;
        }
    }

    const QString configPath = visor::wm::resolveConfigPath(parser.value(configOption));
    // Destroyed explicitly before the instance mutex is released (below), so
    // its hand-over is written before the next visor-wm reads it.
    auto manager = std::make_unique<visor::wm::WindowManager>(loadConfig(configPath));

    std::unique_ptr<QWinEventNotifier> shellExited;
    QTimer newShellPoll;
    if (shell) {
        shellExited = std::make_unique<QWinEventNotifier>(shell);
        newShellPoll.setInterval(kNewShellPollMs);
        QElapsedTimer waited;
        QObject::connect(shellExited.get(), &QWinEventNotifier::activated, &app, [&] {
            shellExited->setEnabled(false);
            qInfo() << "visor-shell exited; waiting briefly for a new one";
            waited.start();
            newShellPoll.start();
        });
        QObject::connect(&newShellPoll, &QTimer::timeout, &app, [&] {
            if (FindWindowW(visor::link::kShellLinkClass, nullptr)) {
                qInfo() << "a new visor-shell is up; handing over";
                manager->handOver();
                QCoreApplication::quit();
            } else if (waited.elapsed() > kNewShellWaitMs) {
                qInfo() << "no new visor-shell; stopping";
                QCoreApplication::quit();
            }
        });
    }

    // Live reload. Editors often save by replacing the file, which drops it
    // from the watcher, so the folder is watched too and the file re-added.
    QFileSystemWatcher watcher;
    QTimer reloadTimer;
    QDateTime lastModified = QFileInfo(configPath).lastModified();
    reloadTimer.setSingleShot(true);
    reloadTimer.setInterval(kReloadDelayMs);
    if (!configPath.isEmpty()) {
        watcher.addPath(configPath);
        watcher.addPath(QFileInfo(configPath).absolutePath());
        const auto changed = [&] { reloadTimer.start(); };
        QObject::connect(&watcher, &QFileSystemWatcher::fileChanged, &app, changed);
        QObject::connect(&watcher, &QFileSystemWatcher::directoryChanged, &app, changed);
        QObject::connect(&reloadTimer, &QTimer::timeout, &app, [&] {
            if (!QFileInfo::exists(configPath))
                return;
            if (!watcher.files().contains(configPath))
                watcher.addPath(configPath);
            const QDateTime modified = QFileInfo(configPath).lastModified();
            if (modified == lastModified)
                return; // another file in the folder changed
            lastModified = modified;
            qInfo() << "reloading config";
            manager->setConfig(loadConfig(configPath));
        });
    }

    const int code = app.exec();
    qInfo() << "visor-wm exiting with code" << code;
    manager.reset();
    // The notifier's thread-pool wait uses the handle until it is destroyed.
    shellExited.reset();
    if (shell)
        CloseHandle(shell);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return code;
}
