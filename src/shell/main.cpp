// visor-shell: shell services for a Visor desktop.
//
// Two modes (see docs/design.md):
//   replace  visor-shell is the Winlogon shell (started by visor-session):
//            owns the desktop window and the shell-ready signal.
//   hosted   Explorer is the shell; visor-shell runs alongside it. This is the
//            mode for day-to-day development on a real machine.
// The default, auto, picks replace only when no shell window exists yet.

#include "common/exitcodes.h"
#include "shell/desktopwindow.h"
#include "shell/hotkeys.h"
#include "shell/launch.h"
#include "shell/log.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcess>

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

// Phase 0: Visor is started once if it was deployed next to us (visor\visor.exe).
// Supervising it comes with the VisorLink work in phase 1.
void startVisor()
{
    const QString path = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("visor/visor.exe"));
    if (!QFileInfo::exists(path)) {
        qInfo() << "Visor not deployed at" << path;
        return;
    }
    if (QProcess::startDetached(path, {}, QFileInfo(path).absolutePath()))
        qInfo() << "started Visor";
    else
        qWarning() << "failed to start Visor at" << path;
}

} // namespace

int main(int argc, char *argv[])
{
    // QtCore doesn't set DPI awareness (QtGui would); without it the desktop
    // window would be bitmap-stretched on scaled displays.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("visor-shell"));
    QCoreApplication::setApplicationVersion(QStringLiteral(VISOR_SHELL_VERSION));
    visor::installLogHandler();

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption modeOption(QStringLiteral("mode"), QStringLiteral("auto (default), replace or hosted."),
                                  QStringLiteral("mode"), QStringLiteral("auto"));
    parser.addOption(modeOption);
    parser.process(app);

    qInfo() << "visor-shell" << VISOR_SHELL_VERSION << "starting, pid" << QCoreApplication::applicationPid();

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
    if (mode == Mode::Replace) {
        desktop = std::make_unique<visor::DesktopWindow>();
        if (!desktop->show())
            return visor::exitcode::Refused;
        signalShellReady();
        startVisor();
    }

    visor::Hotkeys hotkeys;
    // Queued so long-running actions (the Run dialog's modal loop) run outside
    // the WM_HOTKEY handler.
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
                visor::showRunDialog(desktop ? desktop->hwnd() : nullptr);
                break;
            case visor::Hotkeys::QuitToExplorer:
                qInfo() << "quit requested";
                // As the shell, ask visor-session to hand over to Explorer.
                // Hosted, Explorer is already there: just exit.
                QCoreApplication::exit(mode == Mode::Replace ? visor::exitcode::StartExplorer : 0);
                break;
            }
        },
        Qt::QueuedConnection);

    const int code = app.exec();
    qInfo() << "visor-shell exiting with code" << code;
    return code;
}
