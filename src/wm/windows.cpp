#include "wm/windows.h"

#include "wm/config.h"

#include <QFileInfo>

#include <windows.h>
#include <dwmapi.h>

namespace visor::wm::win {

namespace {

// Not in older SDK headers.
constexpr DWORD kBorderColor = 34;              // DWMWA_BORDER_COLOR
constexpr COLORREF kColorDefault = 0xFFFFFFFF;  // DWMWA_COLOR_DEFAULT

HWND toHwnd(quintptr hwnd)
{
    return reinterpret_cast<HWND>(hwnd);
}

bool isCloaked(HWND hwnd)
{
    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked;
}

DWORD processId(HWND hwnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid;
}

DWORD integrityLevel(HANDLE token)
{
    DWORD size = 0;
    GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &size);
    if (!size)
        return 0;
    QByteArray buffer(int(size), Qt::Uninitialized);
    auto *label = reinterpret_cast<TOKEN_MANDATORY_LABEL *>(buffer.data());
    if (!GetTokenInformation(token, TokenIntegrityLevel, label, size, &size))
        return 0;
    const PSID sid = label->Label.Sid;
    return *GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1);
}

// User Interface Privilege Isolation stops us moving windows of processes
// with a higher integrity level than ours (apps run as administrator, Task
// Manager). Their tokens can't even be opened, which counts as higher too.
bool isHigherIntegrity(HWND hwnd)
{
    static const DWORD ours = [] {
        HANDLE token = nullptr;
        DWORD level = 0;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
            level = integrityLevel(token);
            CloseHandle(token);
        }
        return level;
    }();

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId(hwnd));
    if (!process)
        return true;
    HANDLE token = nullptr;
    DWORD level = MAXDWORD;
    if (OpenProcessToken(process, TOKEN_QUERY, &token)) {
        level = integrityLevel(token);
        CloseHandle(token);
    }
    CloseHandle(process);
    return level > ours;
}

bool monitorRect(HWND hwnd, RECT *out)
{
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info))
        return false;
    *out = info.rcMonitor;
    return true;
}

} // namespace

QString className(quintptr hwnd)
{
    wchar_t buffer[256];
    const int len = GetClassNameW(toHwnd(hwnd), buffer, int(std::size(buffer)));
    return QString::fromWCharArray(buffer, qMax(0, len));
}

QString title(quintptr hwnd)
{
    // Doesn't send WM_GETTEXT across processes, so a hung app can't block us.
    wchar_t buffer[512];
    const int len = GetWindowTextW(toHwnd(hwnd), buffer, int(std::size(buffer)));
    return QString::fromWCharArray(buffer, qMax(0, len));
}

QString exeName(quintptr hwnd)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId(toHwnd(hwnd)));
    if (!process)
        return {};
    wchar_t buffer[MAX_PATH * 2];
    DWORD size = DWORD(std::size(buffer));
    QString name;
    if (QueryFullProcessImageNameW(process, 0, buffer, &size))
        name = QFileInfo(QString::fromWCharArray(buffer, int(size))).fileName();
    CloseHandle(process);
    return name;
}

Kind classify(quintptr window, const Config &config, QString *reason)
{
    const auto why = [reason](const char *text) {
        if (reason)
            *reason = QString::fromLatin1(text);
    };
    const HWND hwnd = toHwnd(window);

    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd)) {
        why("not shown");
        return Kind::Ignore;
    }
    if (GetAncestor(hwnd, GA_PARENT) != GetDesktopWindow()) {
        why("not top-level");
        return Kind::Ignore;
    }
    if (processId(hwnd) == GetCurrentProcessId() || isCloaked(hwnd)) {
        why("ours or cloaked");
        return Kind::Ignore;
    }
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((style & WS_CHILD) || (!(ex & WS_EX_APPWINDOW) && (ex & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)))) {
        why("tool window");
        return Kind::Ignore;
    }

    // The bar and the shell's own windows (desktop, tray host).
    const QString exe = exeName(window);
    if (exe.compare(QLatin1String("visor.exe"), Qt::CaseInsensitive) == 0
        || exe.compare(QLatin1String("visor-shell.exe"), Qt::CaseInsensitive) == 0) {
        why("part of visor");
        return Kind::Ignore;
    }

    if (isHigherIntegrity(hwnd)) {
        why("elevated (can't be moved)");
        return Kind::Float;
    }

    const QString cls = className(window);
    const QString text = title(window);
    for (qsizetype i = config.rules.size() - 1; i >= 0; --i) {
        const WindowRule &rule = config.rules[i];
        if (rule.matches(cls, text, exe)) {
            why("window rule");
            return rule.action == WindowRule::Tile ? Kind::Tile : Kind::Float;
        }
    }

    // Dialogs and other owned windows float, as in Hyprland.
    if (GetWindow(hwnd, GW_OWNER) && !(ex & WS_EX_APPWINDOW)) {
        why("owned (dialog)");
        return Kind::Float;
    }
    if (!(style & WS_THICKFRAME)) {
        why("fixed size");
        return Kind::Float;
    }
    if (ex & WS_EX_TOPMOST) {
        why("always on top");
        return Kind::Float;
    }
    if (isFullscreen(window)) {
        why("fullscreen");
        return Kind::Float;
    }
    return Kind::Tile;
}

bool isMinimized(quintptr hwnd)
{
    return IsIconic(toHwnd(hwnd));
}

bool isMaximized(quintptr hwnd)
{
    return IsZoomed(toHwnd(hwnd));
}

bool isFullscreen(quintptr window)
{
    const HWND hwnd = toHwnd(window);
    if (IsZoomed(hwnd) || (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CAPTION) == WS_CAPTION)
        return false;
    RECT rect{};
    RECT monitor{};
    return GetWindowRect(hwnd, &rect) && monitorRect(hwnd, &monitor) && rect.left <= monitor.left
           && rect.top <= monitor.top && rect.right >= monitor.right && rect.bottom >= monitor.bottom;
}

bool exists(quintptr hwnd)
{
    return IsWindow(toHwnd(hwnd));
}

void unmaximize(quintptr hwnd)
{
    // SW_SHOWNOACTIVATE shows the window at its restored size and position.
    ShowWindowAsync(toHwnd(hwnd), SW_SHOWNOACTIVATE);
}

void toggleMaximized(quintptr hwnd)
{
    ShowWindowAsync(toHwnd(hwnd), IsZoomed(toHwnd(hwnd)) ? SW_RESTORE : SW_MAXIMIZE);
}

Rect frameRect(quintptr hwnd)
{
    RECT frame{};
    if (FAILED(DwmGetWindowAttribute(toHwnd(hwnd), DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame))))
        GetWindowRect(toHwnd(hwnd), &frame);
    return {int(frame.left), int(frame.top), int(frame.right), int(frame.bottom)};
}

quintptr foreground()
{
    return reinterpret_cast<quintptr>(GetForegroundWindow());
}

bool focus(quintptr window)
{
    const HWND hwnd = toHwnd(window);
    if (IsIconic(hwnd))
        ShowWindowAsync(hwnd, SW_RESTORE);
    return SetForegroundWindow(hwnd);
}

void close(quintptr hwnd)
{
    PostMessageW(toHwnd(hwnd), WM_CLOSE, 0, 0);
}

void raise(quintptr hwnd)
{
    SetWindowPos(toHwnd(hwnd), HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_ASYNCWINDOWPOS);
}

bool isVisible(quintptr hwnd)
{
    return IsWindowVisible(toHwnd(hwnd));
}

quint32 processId(quintptr hwnd)
{
    return processId(toHwnd(hwnd));
}

quintptr owner(quintptr hwnd)
{
    return reinterpret_cast<quintptr>(GetWindow(toHwnd(hwnd), GW_OWNER));
}

quintptr rootOwner(quintptr hwnd)
{
    return reinterpret_cast<quintptr>(GetAncestor(toHwnd(hwnd), GA_ROOTOWNER));
}

quintptr shellWindow()
{
    return reinterpret_cast<quintptr>(GetShellWindow());
}

void hide(quintptr hwnd)
{
    ShowWindowAsync(toHwnd(hwnd), SW_HIDE);
}

void show(quintptr hwnd)
{
    ShowWindowAsync(toHwnd(hwnd), SW_SHOWNA);
}

bool moveTo(quintptr window, const Rect &target)
{
    const HWND hwnd = toHwnd(window);
    RECT outer{};
    if (!GetWindowRect(hwnd, &outer))
        return false;
    RECT frame = outer;
    DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame));

    const RECT want{
        target.left - (frame.left - outer.left),
        target.top - (frame.top - outer.top),
        target.right + (outer.right - frame.right),
        target.bottom + (outer.bottom - frame.bottom),
    };
    if (EqualRect(&want, &outer))
        return false;
    SetWindowPos(hwnd, nullptr, want.left, want.top, want.right - want.left, want.bottom - want.top,
                 SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    return true;
}

void setBorderColor(quintptr hwnd, quint32 rgb)
{
    const COLORREF color = RGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
    DwmSetWindowAttribute(toHwnd(hwnd), kBorderColor, &color, sizeof(color));
}

void resetBorderColor(quintptr hwnd)
{
    const COLORREF color = kColorDefault;
    DwmSetWindowAttribute(toHwnd(hwnd), kBorderColor, &color, sizeof(color));
}

quint32 accentColor()
{
    // The value Settings writes (0xAABBGGRR) and that DWM and every app read
    // on the "ImmersiveColorSet" broadcast. DwmGetColorizationColor lags it.
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", RRF_RT_REG_DWORD,
                     nullptr, &value, &size) == ERROR_SUCCESS) {
        return ((value & 0xff) << 16) | (value & 0xff00) | ((value >> 16) & 0xff);
    }
    DWORD argb = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&argb, &opaque)))
        return argb & 0xffffff;
    return 0x0078d4; // Windows' default blue
}

} // namespace visor::wm::win
