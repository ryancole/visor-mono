#include "tray.h"

#include "app.h"
#include "resources/resource.h"

#include <QCoreApplication>

#include <windows.h>
#include <shellapi.h>
#include <windowsx.h>

#include <string>

namespace {

#define VISOR_WIDEN2(s) L##s
#define VISOR_WIDEN(s) VISOR_WIDEN2(s)

constexpr UINT kTrayCallback = WM_APP + 1;
constexpr UINT kIconId = 1;
const UINT kTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

enum Command : UINT {
    CmdRendererCpu = 1,
    CmdRendererGpu,
    CmdReload,
    CmdOpenConfig,
    CmdQuit,
};

// Win32 popup menus are light-themed unless the process opts in to dark mode.
// The opt-in is an undocumented uxtheme export (ordinal 135, stable since
// Windows 10 1903 and used by Explorer itself); if it's missing we just keep
// light menus.
void followSystemMenuTheme()
{
    enum PreferredAppMode { Default, AllowDark, ForceDark, ForceLight };
    using SetPreferredAppMode = int(WINAPI *)(int);
    using FlushMenuThemes = void(WINAPI *)();

    const HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!uxtheme)
        return;
    const auto setMode = reinterpret_cast<SetPreferredAppMode>(
        reinterpret_cast<void *>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(135))));
    const auto flush = reinterpret_cast<FlushMenuThemes>(
        reinterpret_cast<void *>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(136))));
    if (setMode)
        setMode(AllowDark);
    if (flush)
        flush();
}

LRESULT CALLBACK trayWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto *tray = reinterpret_cast<TrayIcon *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (tray && tray->handleMessage(msg, wParam, lParam))
        return 0;
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

const wchar_t *windowClass()
{
    static const wchar_t *name = [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = trayWindowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"visor.TrayWindow";
        RegisterClassExW(&wc);
        return wc.lpszClassName;
    }();
    return name;
}

} // namespace

TrayIcon::TrayIcon(App *app)
    : m_app(app)
{
    followSystemMenuTheme();

    // A hidden top-level window rather than a message-only one: message-only
    // windows don't receive the TaskbarCreated broadcast.
    const HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, windowClass(), L"visor", WS_POPUP, 0, 0, 0, 0, nullptr,
                                      nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    m_hwnd = hwnd;

    m_icon = LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_VISOR), IMAGE_ICON,
                        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    add();
}

TrayIcon::~TrayIcon()
{
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = static_cast<HWND>(m_hwnd);
    nid.uID = kIconId;
    Shell_NotifyIconW(NIM_DELETE, &nid);

    DestroyWindow(static_cast<HWND>(m_hwnd));
    if (m_icon)
        DestroyIcon(static_cast<HICON>(m_icon));
}

void TrayIcon::add()
{
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = static_cast<HWND>(m_hwnd);
    nid.uID = kIconId;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
    nid.uCallbackMessage = kTrayCallback;
    nid.hIcon = static_cast<HICON>(m_icon);
    wcscpy_s(nid.szTip, L"visor");
    Shell_NotifyIconW(NIM_ADD, &nid);

    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

bool TrayIcon::handleMessage(unsigned msg, unsigned long long wParam, long long lParam)
{
    if (msg == kTrayCallback) {
        // NOTIFYICON_VERSION_4: event in LOWORD(lParam), anchor in wParam.
        switch (LOWORD(lParam)) {
        case WM_CONTEXTMENU:
        case NIN_SELECT:
        case NIN_KEYSELECT:
            showMenu(GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam));
            break;
        }
        return true;
    }
    if (msg == kTaskbarCreated) {
        add(); // Explorer restarted.
        return true;
    }
    return false;
}

void TrayIcon::showMenu(int x, int y)
{
    const bool gpu = m_app->preferredRenderer() == Settings::Renderer::Gpu;
    const bool activeGpu = m_app->activeRenderer() == Settings::Renderer::Gpu;

    const HMENU renderer = CreatePopupMenu();
    AppendMenuW(renderer, MF_STRING, CmdRendererCpu, L"CPU (lowest memory)");
    AppendMenuW(renderer, MF_STRING, CmdRendererGpu, L"GPU (Direct3D 11, enables shader effects)");
    CheckMenuRadioItem(renderer, CmdRendererCpu, CmdRendererGpu, gpu ? CmdRendererGpu : CmdRendererCpu,
                       MF_BYCOMMAND);

    const std::wstring header = L"visor " VISOR_WIDEN(VISOR_VERSION) L" — rendering on "
                                + std::wstring(activeGpu ? L"GPU" : L"CPU");

    const HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, header.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(renderer), L"Renderer");
    AppendMenuW(menu, MF_STRING, CmdReload, L"Reload config");
    AppendMenuW(menu, MF_STRING, CmdOpenConfig, L"Open config folder");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CmdQuit, L"Quit");

    // Required so the menu closes when clicking elsewhere (KB135788).
    const auto hwnd = static_cast<HWND>(m_hwnd);
    SetForegroundWindow(hwnd);
    const UINT flags = TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY
                       | (GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN);
    const UINT cmd = UINT(TrackPopupMenuEx(menu, flags, x, y, hwnd, nullptr));
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu); // also destroys the submenu

    if (cmd)
        runCommand(cmd);
}

void TrayIcon::runCommand(unsigned id)
{
    switch (id) {
    case CmdRendererCpu: m_app->setPreferredRenderer(Settings::Renderer::Software); break;
    case CmdRendererGpu: m_app->setPreferredRenderer(Settings::Renderer::Gpu); break;
    case CmdReload: m_app->reload(); break;
    case CmdOpenConfig: m_app->openConfigFolder(); break;
    case CmdQuit: m_app->quit(); break;
    }
}
