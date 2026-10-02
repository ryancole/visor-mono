#include "common/launch.h"

#include <QDebug>

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <atomic>
#include <functional>
#include <thread>

namespace visor {

// Launching can block for a long time: activating an app that can't start
// (e.g. Settings without Explorer) hangs ShellExecuteEx until it times out.
// Each launch therefore runs on its own short-lived STA thread, so the
// shell's thread (desktop, hotkeys, later tray and appbars) never stalls.
void runDetached(std::function<void()> work)
{
    std::thread([work = std::move(work)] {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        work();
        if (SUCCEEDED(hr))
            CoUninitialize();
    }).detach();
}

namespace {

bool shellExecuteSync(const QString &file, const QString &parameters, bool asAdmin = false)
{
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = asAdmin ? L"runas" : nullptr;
    info.lpFile = reinterpret_cast<const wchar_t *>(file.utf16());
    info.lpParameters = parameters.isEmpty() ? nullptr : reinterpret_cast<const wchar_t *>(parameters.utf16());
    info.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&info)) {
        qInfo().noquote() << "launched" << file << parameters;
        return true;
    }
    qWarning().noquote() << "failed to launch" << file << parameters << "error" << GetLastError();
    return false;
}

} // namespace

void shellExecute(const QString &file, const QString &parameters, bool asAdmin)
{
    runDetached([file, parameters, asAdmin] { shellExecuteSync(file, parameters, asAdmin); });
}

void run(const QString &commandLine)
{
    const QString line = commandLine.trimmed();
    QString file;
    QString parameters;
    if (line.startsWith(QLatin1Char('"'))) {
        const qsizetype end = line.indexOf(QLatin1Char('"'), 1);
        file = end < 0 ? line.mid(1) : line.mid(1, end - 1);
        parameters = end < 0 ? QString() : line.mid(end + 1).trimmed();
    } else {
        const qsizetype space = line.indexOf(QLatin1Char(' '));
        file = space < 0 ? line : line.left(space);
        parameters = space < 0 ? QString() : line.mid(space + 1).trimmed();
    }
    if (file.isEmpty())
        return;
    shellExecute(file, parameters);
}

void openFileExplorer()
{
    shellExecute(QStringLiteral("explorer.exe"), QStringLiteral("::{20D04FE0-3AEA-1069-A2D8-08002B30309D}"));
}

void openTerminal()
{
    // wt.exe is a packaged app's execution alias, so this doubles as a check
    // of whether packaged apps launch under this shell.
    runDetached([] {
        if (!shellExecuteSync(QStringLiteral("wt.exe"), {}))
            shellExecuteSync(QStringLiteral("cmd.exe"), {});
    });
}

void showRunDialog()
{
    // One at a time: pressing the hotkey again while it's open does nothing.
    static std::atomic_bool open = false;
    if (open.exchange(true))
        return;

    using RunFileDlgFn = void(WINAPI *)(HWND, HICON, LPCWSTR, LPCWSTR, LPCWSTR, UINT);
    static const auto runFileDlg = reinterpret_cast<RunFileDlgFn>(
        GetProcAddress(GetModuleHandleW(L"shell32.dll"), MAKEINTRESOURCEA(61)));
    if (!runFileDlg) {
        qWarning() << "RunFileDlg not available";
        open = false;
        return;
    }

    // The dialog launches whatever is typed from inside its own modal loop,
    // so it lives on a worker thread too. No owner window: an owner on the
    // shell's thread would tie the two threads' input together again.
    runDetached([] {
        runFileDlg(nullptr, nullptr, nullptr, nullptr, nullptr, 0);
        open = false;
    });
}

} // namespace visor
