// visor-wm: the tiling window manager of a Visor desktop (Hyprland's role in
// Omarchy). Started and supervised by visor-shell (always in replace mode,
// and in hosted mode when tiling is turned on); can also be run by hand.
//
//   visor-wm [--config <wm.conf>] [--shell-pid <pid>] [--mode auto|replace|hosted]
//
// With --shell-pid it exits when that process does, so a restarted shell
// starts a fresh visor-wm rather than finding a stale one. If a new shell
// appears within a few seconds (visor-session restarted it), windows on other
// desktops stay hidden and are handed over to the visor-wm it starts;
// otherwise (e.g. the session went back to Explorer) every window is shown.
//
// --mode hosted means Explorer is the shell: Windows' own keys and desktops
// are left to it (see WindowManager). The default, auto, looks at who owns
// the shell window.

#include "common/exitcodes.h"
#include "common/linkprotocol.h"
#include "common/log.h"
#include "wm/config.h"
#include "wm/manager.h"
#include "wm/windows.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDebug>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QWinEventNotifier>

#include <windows.h>
#include <objbase.h>

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
    QCommandLineOption modeOption(QStringLiteral("mode"), QStringLiteral("auto (default), replace or hosted."),
                                  QStringLiteral("mode"), QStringLiteral("auto"));
    parser.addOption(configOption);
    parser.addOption(shellOption);
    parser.addOption(modeOption);
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

    // Hosted (Explorer is the shell) or replace (visor-shell is): Explorer's
    // desktop window gives it away.
    const QString modeName = parser.value(modeOption);
    bool hosted = false;
    if (modeName == QLatin1String("hosted")) {
        hosted = true;
    } else if (modeName == QLatin1String("auto")) {
        const auto shellWindow = reinterpret_cast<quintptr>(GetShellWindow());
        hosted = shellWindow
                 && visor::wm::win::exeName(shellWindow).compare(QLatin1String("explorer.exe"), Qt::CaseInsensitive)
                        == 0;
    } else if (modeName != QLatin1String("replace")) {
        qCritical() << "unknown --mode" << modeName;
        return visor::exitcode::Refused;
    }
    qInfo() << "mode:" << (hosted ? "hosted" : "replace");

    // IVirtualDesktopManager (hosted mode) and ShellExecute want an STA.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    const QString configPath = visor::wm::resolveConfigPath(parser.value(configOption));
    // Destroyed explicitly before the instance mutex is released (below), so
    // its hand-over is written before the next visor-wm reads it.
    auto manager = std::make_unique<visor::wm::WindowManager>(loadConfig(configPath), hosted);

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

    // Live reload of wm.conf and the files it sources (the theme's border
    // colours in ~/.config/visor/current/wm.conf). Editors often save by
    // replacing a file, which drops it from the watcher, so folders are
    // watched too and files re-added. A sourced file that doesn't exist yet
    // is watched through its nearest existing parent folder, so the first
    // theme ever applied is picked up as well.
    QFileSystemWatcher watcher;
    QTimer reloadTimer;
    QStringList watched;  // wm.conf and its sources
    QStringList snapshot; // "path|mtime|size" of each, for change detection
    const auto stamp = [](const QStringList &files) {
        QStringList entries;
        for (const QString &file : files) {
            const QFileInfo info(file);
            entries << QStringLiteral("%1|%2|%3")
                           .arg(file)
                           .arg(info.exists() ? info.lastModified().toMSecsSinceEpoch() : 0)
                           .arg(info.exists() ? info.size() : -1);
        }
        return entries;
    };
    const auto rewatch = [&](const QStringList &files) {
        watched = files;
        snapshot = stamp(files);
        if (!watcher.files().isEmpty())
            watcher.removePaths(watcher.files());
        if (!watcher.directories().isEmpty())
            watcher.removePaths(watcher.directories());
        QStringList paths;
        for (const QString &file : files) {
            if (QFileInfo::exists(file))
                paths << file;
            // The nearest folder that exists (QDir::cdUp won't step into a
            // missing parent, so walk the path instead).
            QString dir = QFileInfo(file).absolutePath();
            while (!QFileInfo::exists(dir)) {
                const QString parent = QFileInfo(dir).absolutePath();
                if (parent == dir)
                    break;
                dir = parent;
            }
            paths << dir;
        }
        paths.removeDuplicates();
        watcher.addPaths(paths);
        qInfo().noquote() << "watching" << paths.join(QLatin1String(", "));
    };
    reloadTimer.setSingleShot(true);
    reloadTimer.setInterval(kReloadDelayMs);
    if (!configPath.isEmpty()) {
        rewatch(QStringList{configPath} + manager->config().sources);
        const auto changed = [&] { reloadTimer.start(); };
        QObject::connect(&watcher, &QFileSystemWatcher::fileChanged, &app, changed);
        QObject::connect(&watcher, &QFileSystemWatcher::directoryChanged, &app, changed);
        QObject::connect(&reloadTimer, &QTimer::timeout, &app, [&] {
            if (stamp(watched) == snapshot) {
                rewatch(watched); // something else in a folder changed; re-arm
                return;
            }
            qInfo() << "reloading config";
            manager->setConfig(loadConfig(configPath));
            rewatch(QStringList{configPath} + manager->config().sources);
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
