// visor-session: the program Winlogon starts as the user's shell, and the
// one the hosted-mode Run entry starts (`visor-session --mode hosted`).
//
// It owns no windows and does no shell work itself. It starts visor-shell,
// restarts it if it crashes, and hands the session to Explorer whenever
// visor-shell can't run, so a bad build can never leave a black screen:
//
//   - Shift held at logon, or %LOCALAPPDATA%\visor-shell\safe-mode exists
//   - visor-shell.exe missing or failing to start
//   - visor-shell crashing 3 times within 60 seconds
//   - visor-shell asking for it (exitcode::StartExplorer)
//
// In hosted mode Explorer is the shell already, so in each of those cases it
// simply gives up, and a visor-shell that exits cleanly (Ctrl+Alt+Q) ends it.
//
// Linked against the static CRT and only system DLLs, so it keeps working
// even when the Qt or VC++ runtime DLLs are broken.

#include "common/exitcodes.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <deque>
#include <string>

namespace {

constexpr int kMaxCrashes = 3;
constexpr ULONGLONG kCrashWindowMs = 60'000;
constexpr DWORD kRestartDelayMs = 1'000;
constexpr long kMaxLogBytes = 1 << 20;

std::wstring g_dataDir; // %LOCALAPPDATA%\visor-shell

std::wstring localAppData()
{
    PWSTR path = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path)))
        result = path;
    CoTaskMemFree(path);
    return result;
}

void log(const wchar_t *format, ...)
{
    if (g_dataDir.empty())
        return;
    const std::wstring dir = g_dataDir + L"\\logs";
    CreateDirectoryW(g_dataDir.c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);

    FILE *f = nullptr;
    if (_wfopen_s(&f, (dir + L"\\session.log").c_str(), L"a+, ccs=UTF-8") != 0 || !f)
        return;

    SYSTEMTIME t;
    GetLocalTime(&t);
    fwprintf(f, L"%04u-%02u-%02u %02u:%02u:%02u.%03u [%lu] ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
             t.wSecond, t.wMilliseconds, GetCurrentProcessId());
    va_list args;
    va_start(args, format);
    vfwprintf(f, format, args);
    va_end(args);
    fputwc(L'\n', f);
    fclose(f);
}

// Keep the log from growing forever: start over once it passes 1 MB.
void trimLog()
{
    const std::wstring path = g_dataDir + L"\\logs\\session.log";
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) && info.nFileSizeHigh == 0
        && info.nFileSizeLow > kMaxLogBytes) {
        DeleteFileW(path.c_str());
    }
}

std::wstring exeDir()
{
    wchar_t buffer[MAX_PATH * 2];
    const DWORD len = GetModuleFileNameW(nullptr, buffer, DWORD(std::size(buffer)));
    std::wstring path(buffer, len);
    return path.substr(0, path.find_last_of(L'\\'));
}

bool fileExists(const std::wstring &path)
{
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool shiftHeld()
{
    return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
}

// Shift is checked for half a second rather than once, since logon can take a
// moment to reach us after the user presses Enter.
bool safeModeRequested()
{
    if (fileExists(g_dataDir + L"\\safe-mode")) {
        log(L"safe-mode file present");
        return true;
    }
    for (int i = 0; i < 10; ++i) {
        if (shiftHeld()) {
            log(L"Shift held at logon");
            return true;
        }
        Sleep(50);
    }
    return false;
}

// With no shell window in the session, a bare explorer.exe becomes the full
// shell (taskbar and desktop), which is the normal Windows experience.
//
// Waits (up to kExplorerWaitMs) for Explorer's shell window before returning:
// Winlogon restarts the configured shell (us) when the shell-window process
// exits, and our instance mutex has to be held until Explorer owns the
// session, or the restarted visor-session would start visor-shell again.
constexpr DWORD kExplorerWaitMs = 15'000;

void startExplorer(const wchar_t *reason)
{
    log(L"starting Explorer: %ls", reason);
    wchar_t windows[MAX_PATH];
    GetWindowsDirectoryW(windows, MAX_PATH);
    std::wstring cmd = std::wstring(L"\"") + windows + L"\\explorer.exe\"";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        log(L"failed to start Explorer (error %lu)", GetLastError());
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    const ULONGLONG start = GetTickCount64();
    while (!GetShellWindow() && GetTickCount64() - start < kExplorerWaitMs)
        Sleep(100);
    if (GetShellWindow())
        log(L"Explorer is the shell after %llu ms", GetTickCount64() - start);
    else
        log(L"Explorer did not create a shell window within %lu ms", kExplorerWaitMs);
}

bool sessionEnding()
{
    return GetSystemMetrics(SM_SHUTTINGDOWN) != 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR commandLine, int)
{
    g_dataDir = localAppData() + L"\\visor-shell";
    trimLog();
    // The only option: --mode hosted (Explorer stays; the Run entry's way).
    const bool hosted = wcsstr(commandLine, L"--mode hosted") != nullptr;
    log(L"visor-session " VISOR_VERSION " starting (%ls)", hosted ? L"hosted" : L"replace");

    // Winlogon restarts the configured shell when the shell-window process
    // (visor-shell) dies, so a crash can start a second visor-session while
    // the first is already restarting visor-shell. The first one keeps
    // ownership, along with its crash count. Released when we exit.
    CreateMutexW(nullptr, FALSE, L"Local\\visor-session.instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        log(L"another visor-session is running; exiting");
        return 1;
    }

    // Hosted, Explorer is there: where replace mode would start it, just
    // stop. Exit codes below are always non-zero: see common/exitcodes.h.
    const auto giveUp = [hosted](const wchar_t *reason) {
        if (hosted)
            log(L"stopping: %ls", reason);
        else
            startExplorer(reason);
        return 1;
    };

    if (safeModeRequested())
        return giveUp(L"safe mode");

    const std::wstring shell = exeDir() + L"\\visor-shell.exe";
    if (!fileExists(shell))
        return giveUp(L"visor-shell.exe not found");

    std::deque<ULONGLONG> crashes;
    for (;;) {
        std::wstring cmd = L"\"" + shell + (hosted ? L"\" --mode hosted" : L"\" --mode replace");
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            log(L"failed to start visor-shell (error %lu)", GetLastError());
            return giveUp(L"visor-shell failed to start");
        }
        CloseHandle(pi.hThread);
        log(L"visor-shell started (pid %lu)", pi.dwProcessId);

        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        log(L"visor-shell exited with code %lu (0x%08lx)", code, code);

        if (sessionEnding()) {
            log(L"session is ending");
            return 1;
        }

        switch (int(code)) {
        case visor::exitcode::Restart:
            continue;
        case visor::exitcode::StartExplorer:
            startExplorer(L"requested by visor-shell");
            return 1;
        case visor::exitcode::AlreadyRunning:
        case visor::exitcode::Refused:
            // Something else already owns the session; stay out of its way.
            return 1;
        case 0:
            // A hosted visor-shell quit on purpose (Ctrl+Alt+Q, --quit). As
            // the shell it never exits 0; treat that as a crash.
            if (hosted)
                return 0;
            break;
        default:
            break;
        }

        // Anything else is a crash (or a missing DLL, which shows up as
        // STATUS_DLL_NOT_FOUND). Back off and retry a few times.
        const ULONGLONG now = GetTickCount64();
        crashes.push_back(now);
        while (!crashes.empty() && now - crashes.front() > kCrashWindowMs)
            crashes.pop_front();
        if (int(crashes.size()) >= kMaxCrashes)
            return giveUp(L"visor-shell keeps crashing");
        Sleep(kRestartDelayMs);
    }
}
