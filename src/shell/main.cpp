// visor-shell: shell services for a Visor desktop.
//
// Two modes (see docs/design.md):
//   replace  visor-shell is the Winlogon shell (started by visor-session):
//            owns the desktop window and the shell-ready signal.
//   hosted   Explorer is the shell; visor-shell runs alongside it (started by
//            a Run entry, see etc/install.ps1 -Hosted): Explorer's taskbar is
//            asked to auto-hide and Visor's bar takes its place. This is the
//            mode for a real machine, where Settings, Store apps and Windows'
//            own toasts, flyouts and Alt+Tab keep working.
// The default, auto, picks replace only when no shell window exists yet.
// Both modes start and supervise Visor; replace mode also runs visor-wm.

#include "common/exitcodes.h"
#include "common/linkprotocol.h"
#include "shell/appbars.h"
#include "shell/desktopwindow.h"
#include "shell/hotkeys.h"
#include "shell/startup.h"
#include "common/launch.h"
#include "shell/supervisor.h"
#include "common/log.h"
#include "shell/taskbar.h"
#include "shell/tasks.h"
#include "shell/trayhost.h"
#include "shell/visorlink.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>

#include <windows.h>
#include <objbase.h>

#include <memory>

namespace {

enum class Mode { Replace, Hosted };

// Tells Winlogon the desktop is ready so it switches away from the logon
// screen. Without it Winlogon waits ~30 s before giving up.
void signalShellReady()
{
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"ShellDesktopSwitchEvent");
    if (!event) {
        qWarning() << "ShellDesktopSwitchEvent not available, error" << GetLastError();
        return;
    }
    SetEvent(event);
    CloseHandle(event);
    qInfo() << "signalled ShellDesktopSwitchEvent";
}

QJsonObject toJson(const visor::Tasks::Task &task)
{
    return {
        {QStringLiteral("hwnd"), qint64(task.hwnd)},
        {QStringLiteral("title"), task.title},
        {QStringLiteral("pid"), qint64(task.pid)},
        {QStringLiteral("path"), task.path},
        {QStringLiteral("flashing"), task.flashing},
    };
}

QJsonObject toJson(const visor::TrayHost::Icon &icon)
{
    return {
        {QStringLiteral("id"), icon.id},
        {QStringLiteral("pid"), qint64(icon.pid)},
        {QStringLiteral("tip"), icon.tip},
        {QStringLiteral("icon"), qint64(icon.icon)},
        {QStringLiteral("hidden"), icon.hidden},
    };
}

// With no taskbar, Windows parks minimised windows as title-bar stubs in the
// bottom-left corner. While Visor is connected (and can list them) hide them
// the way Explorer does; otherwise put back what was there, so minimised
// windows stay reachable. Session-only: never written to the profile.
class MinimizedWindows
{
public:
    MinimizedWindows()
    {
        m_saved.cbSize = sizeof(m_saved);
        SystemParametersInfoW(SPI_GETMINIMIZEDMETRICS, sizeof(m_saved), &m_saved, 0);
    }
    ~MinimizedWindows() { setHidden(false); }

    void setHidden(bool hidden)
    {
        if (hidden == m_hidden)
            return;
        MINIMIZEDMETRICS metrics = m_saved;
        if (hidden)
            metrics.iArrange = ARW_HIDE;
        SystemParametersInfoW(SPI_SETMINIMIZEDMETRICS, sizeof(metrics), &metrics, 0);
        m_hidden = hidden;
    }

private:
    MINIMIZEDMETRICS m_saved{};
    bool m_hidden = false;
};

} // namespace

int main(int argc, char *argv[])
{
    // QtCore doesn't set DPI awareness (QtGui would); without it the desktop
    // window would be bitmap-stretched on scaled displays.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("visor-shell"));
    QCoreApplication::setApplicationVersion(QStringLiteral(VISOR_VERSION));
    visor::installLogHandler(QStringLiteral("shell"));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption modeOption(QStringLiteral("mode"), QStringLiteral("auto (default), replace or hosted."),
                                  QStringLiteral("mode"), QStringLiteral("auto"));
    parser.addOption(modeOption);
    QCommandLineOption quitOption(QStringLiteral("quit"),
                                  QStringLiteral("Ask the running visor-shell to quit, as Ctrl+Alt+Q does."));
    parser.addOption(quitOption);
    parser.process(app);

    if (parser.isSet(quitOption)) {
        const HWND running = FindWindowW(visor::link::kShellLinkClass, nullptr);
        if (!running) {
            qInfo() << "--quit: no visor-shell is running in this session";
            return visor::exitcode::Refused;
        }
        PostMessageW(running, WM_CLOSE, 0, 0);
        return 0;
    }

    qInfo() << "visor-shell" << VISOR_VERSION << "starting, pid" << QCoreApplication::applicationPid();

    // Shell APIs (ShellExecuteEx, the Run dialog) expect an STA.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // One visor-shell per session. The handle is released when we exit.
    CreateMutexW(nullptr, FALSE, L"Local\\visor-shell.instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        qWarning() << "visor-shell is already running in this session";
        return visor::exitcode::AlreadyRunning;
    }

    const QString modeName = parser.value(modeOption);
    const HWND existingShell = GetShellWindow();
    Mode mode;
    if (modeName == QLatin1String("replace")) {
        mode = Mode::Replace;
    } else if (modeName == QLatin1String("hosted")) {
        mode = Mode::Hosted;
    } else if (modeName == QLatin1String("auto")) {
        mode = existingShell ? Mode::Hosted : Mode::Replace;
    } else {
        qCritical() << "unknown --mode" << modeName;
        return visor::exitcode::Refused;
    }

    if (mode == Mode::Replace && existingShell) {
        qCritical() << "refusing replace mode: a shell window already exists" << existingShell;
        return visor::exitcode::Refused;
    }
    qInfo() << "mode:" << (mode == Mode::Replace ? "replace" : "hosted");

    std::unique_ptr<visor::DesktopWindow> desktop;
    // Hosted: Explorer's taskbar auto-hides for as long as we run, so
    // Visor's bar is the one on screen (restored when `taskbar` goes).
    std::unique_ptr<visor::Taskbar> taskbar;
    if (mode == Mode::Replace) {
        desktop = std::make_unique<visor::DesktopWindow>();
        if (!desktop->show())
            return visor::exitcode::Refused;
        signalShellReady();
    } else {
        taskbar = std::make_unique<visor::Taskbar>();
    }

    // As the shell we also serve app bars and host the tray (Explorer does
    // both otherwise). Before Visor starts, so its bars and icon land here.
    std::unique_ptr<visor::AppBars> appBars;
    std::unique_ptr<visor::TrayHost> tray;
    if (mode == Mode::Replace) {
        appBars = std::make_unique<visor::AppBars>();
        tray = std::make_unique<visor::TrayHost>(appBars.get());
        QObject::connect(desktop.get(), &visor::DesktopWindow::displayChanged, appBars.get(),
                         &visor::AppBars::displayChanged);
    }

    // Tasks and tray icons are sent to Visor as they change; a (re)connecting
    // Visor gets the whole lists.
    visor::Tasks tasks(mode == Mode::Replace);
    visor::VisorLink link;
    MinimizedWindows minimized;

    if (appBars) {
        QObject::connect(&tasks, &visor::Tasks::activated, appBars.get(), &visor::AppBars::checkFullscreen);
        QObject::connect(&tasks, &visor::Tasks::fullscreenChanged, appBars.get(), &visor::AppBars::checkFullscreen);
        // A crashed Visor leaves its bars registered.
        QObject::connect(&link, &visor::VisorLink::clientDisconnected, appBars.get(), &visor::AppBars::prune);
    }
    if (tray) {
        QObject::connect(tray.get(), &visor::TrayHost::iconAdded, &app, [&](const visor::TrayHost::Icon &i) {
            link.send({{QStringLiteral("type"), QStringLiteral("tray.added")}, {QStringLiteral("icon"), toJson(i)}});
        });
        QObject::connect(tray.get(), &visor::TrayHost::iconChanged, &app, [&](const visor::TrayHost::Icon &i) {
            link.send({{QStringLiteral("type"), QStringLiteral("tray.changed")}, {QStringLiteral("icon"), toJson(i)}});
        });
        QObject::connect(tray.get(), &visor::TrayHost::iconRemoved, &app, [&](int id) {
            link.send({{QStringLiteral("type"), QStringLiteral("tray.removed")}, {QStringLiteral("id"), id}});
        });
    }
    // Ctrl+Alt+Q, Visor's menu or `visor-shell --quit`: hand the session to
    // Explorer. Visor goes too, in both modes; a visor-wm run by hand in
    // hosted mode is the user's to stop.
    const auto quitToExplorer = [&] {
        qInfo() << "quit requested";
        link.stopVisor();
        // Shows windows hidden on other desktops before Explorer comes.
        if (mode == Mode::Replace)
            link.sendToWm({{QStringLiteral("type"), QStringLiteral("quit")}});
        // As the shell, ask visor-session to hand over to Explorer.
        // Hosted, Explorer is already there: just exit.
        QCoreApplication::exit(mode == Mode::Replace ? visor::exitcode::StartExplorer : 0);
    };
    QObject::connect(&link, &visor::VisorLink::quitRequested, &app, quitToExplorer);

    QObject::connect(&link, &visor::VisorLink::messageReceived, &app, [&](const QJsonObject &m) {
        const QString type = m.value(QStringLiteral("type")).toString();
        if (tray && type == QLatin1String("tray.click")) {
            tray->click(m.value(QStringLiteral("id")).toInt(), m.value(QStringLiteral("button")).toString(),
                        m.value(QStringLiteral("x")).toInt(), m.value(QStringLiteral("y")).toInt());
        } else if (type == QLatin1String("workspace.activate")) {
            link.sendToWm(m);
        } else if (type == QLatin1String("shell.quit")) {
            quitToExplorer();
        }
    });

    // visor-wm's desktops and key bindings, passed on to Visor; the latest
    // of each is re-sent when Visor (re)connects. Its commands for Visor
    // (open the launcher, ...) just go through.
    QJsonObject desktops;
    QJsonObject bindings;
    QObject::connect(&link, &visor::VisorLink::wmMessageReceived, &app, [&](const QJsonObject &m) {
        const QString type = m.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("workspaces"))
            desktops = m;
        else if (type == QLatin1String("bindings"))
            bindings = m;
        link.send(m);
    });

    QObject::connect(&link, &visor::VisorLink::clientConnected, &app, [&] {
        // First, so Visor knows what it is running under (packaged apps
        // only launch with Explorer around).
        link.send({{QStringLiteral("type"), QStringLiteral("shell")},
                   {QStringLiteral("mode"), mode == Mode::Replace ? QStringLiteral("replace")
                                                                   : QStringLiteral("hosted")}});
        QJsonArray list;
        for (const visor::Tasks::Task &t : tasks.tasks())
            list.append(toJson(t));
        link.send({{QStringLiteral("type"), QStringLiteral("tasks.reset")},
                   {QStringLiteral("tasks"), list},
                   {QStringLiteral("active"), qint64(tasks.active())}});
        if (tray) {
            QJsonArray icons;
            for (const visor::TrayHost::Icon &i : tray->icons())
                icons.append(toJson(i));
            link.send({{QStringLiteral("type"), QStringLiteral("tray.reset")}, {QStringLiteral("icons"), icons}});
        }
        if (!desktops.isEmpty())
            link.send(desktops);
        if (!bindings.isEmpty())
            link.send(bindings);
        if (mode == Mode::Replace)
            minimized.setHidden(true);
    });
    QObject::connect(&link, &visor::VisorLink::clientDisconnected, &app, [&] { minimized.setHidden(false); });
    QObject::connect(&tasks, &visor::Tasks::added, &app, [&](const visor::Tasks::Task &t) {
        link.send({{QStringLiteral("type"), QStringLiteral("task.added")}, {QStringLiteral("task"), toJson(t)}});
    });
    QObject::connect(&tasks, &visor::Tasks::changed, &app, [&](const visor::Tasks::Task &t) {
        link.send({{QStringLiteral("type"), QStringLiteral("task.changed")}, {QStringLiteral("task"), toJson(t)}});
    });
    QObject::connect(&tasks, &visor::Tasks::removed, &app, [&](quintptr hwnd) {
        link.send({{QStringLiteral("type"), QStringLiteral("task.removed")}, {QStringLiteral("hwnd"), qint64(hwnd)}});
    });
    QObject::connect(&tasks, &visor::Tasks::activated, &app, [&](quintptr hwnd) {
        link.send({{QStringLiteral("type"), QStringLiteral("task.activated")}, {QStringLiteral("hwnd"), qint64(hwnd)}});
    });

    // Starts Visor (unless one is already running and connects first, as
    // when it is run from a terminal) and keeps it running.
    link.start(true);

    // Signing out or shutting down: Winlogon ends every process anyway;
    // leaving first lets Visor and visor-wm close cleanly, and visor-session
    // sees the session ending and doesn't restart us.
    if (desktop) {
        QObject::connect(desktop.get(), &visor::DesktopWindow::sessionEnding, &app, [&] {
            qInfo() << "session ending";
            link.stopVisor();
            link.sendToWm({{QStringLiteral("type"), QStringLiteral("quit")}});
            QCoreApplication::exit(visor::exitcode::Restart);
        });
    }

    // What Explorer would start at sign-in, once the desktop is up (and
    // Visor has had a moment to connect, so new windows get tracked).
    visor::Startup startup;
    if (mode == Mode::Replace)
        QTimer::singleShot(3000, &startup, &visor::Startup::run);

    // The tiling window manager. Replace mode only: on a machine where
    // Explorer is the shell, tiling is opt-in (run visor-wm by hand).
    std::unique_ptr<visor::Supervisor> windowManager;
    if (mode == Mode::Replace) {
        windowManager = std::make_unique<visor::Supervisor>(
            QStringLiteral("visor-wm.exe"),
            QStringList{QStringLiteral("--shell-pid"), QString::number(QCoreApplication::applicationPid())});
        // Until a restarted visor-wm reports in, there are no desktops.
        QObject::connect(windowManager.get(), &visor::Supervisor::exited, &app, [&] {
            desktops = {};
            bindings = {};
            link.send({{QStringLiteral("type"), QStringLiteral("workspaces")},
                       {QStringLiteral("workspaces"), QJsonArray()},
                       {QStringLiteral("active"), 0}});
            link.send({{QStringLiteral("type"), QStringLiteral("bindings")}, {QStringLiteral("bindings"), QJsonArray()}});
        });
        windowManager->start();
    }

    visor::Hotkeys hotkeys;
    // Queued so actions run outside the WM_HOTKEY handler.
    QObject::connect(
        &hotkeys, &visor::Hotkeys::triggered, &app,
        [&](visor::Hotkeys::Action action) {
            switch (action) {
            case visor::Hotkeys::OpenFileExplorer:
                visor::openFileExplorer();
                break;
            case visor::Hotkeys::OpenTerminal:
                visor::openTerminal();
                break;
            case visor::Hotkeys::ShowRun:
                visor::showRunDialog();
                break;
            case visor::Hotkeys::QuitToExplorer:
                quitToExplorer();
                break;
            }
        },
        Qt::QueuedConnection);

    const int code = app.exec();
    qInfo() << "visor-shell exiting with code" << code;
    return code;
}
