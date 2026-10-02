#include "services/shellcontrol.h"

#include "common/launch.h"
#include "services/shelllink.h"

#include <QDebug>

#include <windows.h>
#include <powrprof.h>

namespace {

// Shutting down, restarting and signing out need SE_SHUTDOWN_NAME, which
// every interactive user has but must enable first.
bool exitWindows(UINT flags)
{
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid);
        AdjustTokenPrivileges(token, FALSE, &tp, 0, nullptr, nullptr);
        CloseHandle(token);
    }
    if (ExitWindowsEx(flags, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_FLAG_PLANNED))
        return true;
    qWarning() << "ExitWindowsEx failed, error" << GetLastError();
    return false;
}

} // namespace

ShellControl::ShellControl(QObject *parent)
    : QObject(parent)
{
    if (ShellLink *link = ShellLink::instance()) {
        connect(link, &ShellLink::connectedChanged, this, &ShellControl::changed);
        connect(link, &ShellLink::commandReceived, this, &ShellControl::command);
    }
}

bool ShellControl::available() const
{
    return ShellLink::instance() && ShellLink::instance()->connected();
}

QString ShellControl::mode() const
{
    return ShellLink::instance() ? ShellLink::instance()->mode() : QString();
}

void ShellControl::run(const QString &commandLine)
{
    visor::run(commandLine);
}

void ShellControl::showRunDialog()
{
    // The dialog comes up on a worker thread of ours; make sure a window of
    // this process may take the foreground first.
    if (ShellLink *link = ShellLink::instance())
        link->grantForeground(0);
    visor::showRunDialog();
}

void ShellControl::lock()
{
    LockWorkStation();
}

void ShellControl::signOut()
{
    exitWindows(EWX_LOGOFF);
}

void ShellControl::sleep()
{
    SetSuspendState(FALSE, FALSE, FALSE);
}

void ShellControl::restart()
{
    exitWindows(EWX_REBOOT);
}

void ShellControl::shutDown()
{
    exitWindows(EWX_SHUTDOWN | EWX_POWEROFF);
}

void ShellControl::quitToExplorer()
{
    if (ShellLink *link = ShellLink::instance())
        link->send({{"type", "shell.quit"}});
}
