#include "shell/desktopwindow.h"

#include "shell/wallpaper.h"

#include <QDebug>

#include <windows.h>

namespace visor {

namespace {

const wchar_t kClassName[] = L"VisorDesktop";

// Shown in the corner of the primary monitor while there is no launcher yet.
const wchar_t kHints[] = L"visor-shell " VISOR_SHELL_VERSION L" — phase 0\n"
                         L"Ctrl+Alt+E    File Explorer\n"
                         L"Ctrl+Alt+T    Terminal\n"
                         L"Ctrl+Alt+R    Run\n"
                         L"Ctrl+Alt+Q    Quit to Explorer\n"
                         L"Ctrl+Shift+Esc    Task Manager";

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE) {
        const auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto *self = reinterpret_cast<DesktopWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self && self->hwnd() == hwnd)
        return self->handleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

const wchar_t *windowClass()
{
    static const ATOM atom = [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = windowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        return RegisterClassExW(&wc);
    }();
    return atom ? kClassName : nullptr;
}

// Undocumented but stable since NT 4; every alternative shell uses it.
bool setShellWindow(HWND hwnd)
{
    using SetShellWindowFn = BOOL(WINAPI *)(HWND);
    static const auto fn =
        reinterpret_cast<SetShellWindowFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetShellWindow"));
    return fn && fn(hwnd);
}

} // namespace

DesktopWindow::DesktopWindow(QObject *parent)
    : QObject(parent)
{
    // WS_EX_TOOLWINDOW keeps the desktop out of Alt+Tab and task lists.
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, windowClass(), L"visor desktop", WS_POPUP | WS_CLIPCHILDREN, 0, 0, 0,
                                0, nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd) {
        qCritical() << "failed to create desktop window, error" << GetLastError();
        return;
    }
    m_hwnd = hwnd;
    fitToVirtualScreen();
}

DesktopWindow::~DesktopWindow()
{
    if (m_hwnd)
        DestroyWindow(static_cast<HWND>(m_hwnd));
}

bool DesktopWindow::show()
{
    const auto hwnd = static_cast<HWND>(m_hwnd);
    if (!hwnd)
        return false;
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (!setShellWindow(hwnd)) {
        qCritical() << "SetShellWindow failed; existing shell window" << GetShellWindow();
        return false;
    }
    qInfo() << "desktop window registered as shell window" << hwnd;
    return true;
}

void DesktopWindow::fitToVirtualScreen()
{
    const int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    SetWindowPos(static_cast<HWND>(m_hwnd), HWND_BOTTOM, x, y, w, h, SWP_NOACTIVATE);
    InvalidateRect(static_cast<HWND>(m_hwnd), nullptr, FALSE);
}

void DesktopWindow::paint()
{
    const auto hwnd = static_cast<HWND>(m_hwnd);
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);

    // PaintDesktop draws nothing without Explorer (phase 0), so render the
    // wallpaper ourselves.
    paintWallpaper(dc, ps.rcPaint);

    // Hints on the primary monitor, whose top-left is screen (0, 0).
    const int originX = -GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int originY = -GetSystemMetrics(SM_YVIRTUALSCREEN);
    const UINT dpi = GetDpiForWindow(hwnd);
    const int margin = MulDiv(24, int(dpi), 96);
    HFONT font = CreateFontW(-MulDiv(11, int(dpi), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);

    RECT text{};
    DrawTextW(dc, kHints, -1, &text, DT_CALCRECT | DT_EXPANDTABS);
    const int width = text.right - text.left;
    const int height = text.bottom - text.top;
    RECT rc{originX + GetSystemMetrics(SM_CXSCREEN) - margin - width,
            originY + GetSystemMetrics(SM_CYSCREEN) - margin - height, 0, 0};
    rc.right = rc.left + width;
    rc.bottom = rc.top + height;

    RECT shadow = rc;
    OffsetRect(&shadow, 1, 1);
    SetTextColor(dc, RGB(0, 0, 0));
    DrawTextW(dc, kHints, -1, &shadow, DT_EXPANDTABS);
    SetTextColor(dc, RGB(240, 240, 240));
    DrawTextW(dc, kHints, -1, &rc, DT_EXPANDTABS);

    SelectObject(dc, oldFont);
    DeleteObject(font);
    EndPaint(hwnd, &ps);
}

std::intptr_t DesktopWindow::handleMessage(unsigned msg, std::uintptr_t wParam, std::intptr_t lParam)
{
    const auto hwnd = static_cast<HWND>(m_hwnd);
    switch (msg) {
    case WM_WINDOWPOSCHANGING: {
        // Stay behind every other window, whatever activated us.
        auto *pos = reinterpret_cast<WINDOWPOS *>(lParam);
        pos->hwndInsertAfter = HWND_BOTTOM;
        pos->flags &= ~SWP_NOZORDER;
        return 0;
    }
    case WM_ERASEBKGND:
        return 1; // paint() covers everything
    case WM_PAINT:
        paint();
        return 0;
    case WM_DISPLAYCHANGE:
        fitToVirtualScreen();
        return 0;
    case WM_DPICHANGED:
        // We size ourselves to the virtual screen; ignore the suggested rect.
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_SETTINGCHANGE:
        if (wParam == SPI_SETDESKWALLPAPER || wParam == SPI_SETWORKAREA)
            InvalidateRect(hwnd, nullptr, FALSE);
        break;
    case WM_CLOSE:
        return 0; // Alt+F4 on the desktop must not close the shell
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, WPARAM(wParam), LPARAM(lParam));
}

} // namespace visor
