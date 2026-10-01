#include "shell/launch.h"

#include <QDebug>

#include <windows.h>
#include <shellapi.h>

namespace visor {

bool shellExecute(const QString &file, const QString &parameters)
{
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
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

void openFileExplorer()
{
    shellExecute(QStringLiteral("explorer.exe"), QStringLiteral("::{20D04FE0-3AEA-1069-A2D8-08002B30309D}"));
}

void openTerminal()
{
    // wt.exe is a packaged app's execution alias, so this doubles as a check
    // of whether packaged apps launch under this shell.
    if (!shellExecute(QStringLiteral("wt.exe")))
        shellExecute(QStringLiteral("cmd.exe"));
}

void showRunDialog(void *ownerHwnd)
{
    using RunFileDlgFn = void(WINAPI *)(HWND, HICON, LPCWSTR, LPCWSTR, LPCWSTR, UINT);
    static const auto runFileDlg = reinterpret_cast<RunFileDlgFn>(
        GetProcAddress(GetModuleHandleW(L"shell32.dll"), MAKEINTRESOURCEA(61)));
    if (!runFileDlg) {
        qWarning() << "RunFileDlg not available";
        return;
    }
    runFileDlg(static_cast<HWND>(ownerHwnd), nullptr, nullptr, nullptr, nullptr, 0);
}

} // namespace visor
