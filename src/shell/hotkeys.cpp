#include "shell/hotkeys.h"

#include <QDebug>

#include <windows.h>

namespace visor {

namespace {

struct Binding
{
    Hotkeys::Action action;
    UINT modifiers;
    UINT key;
    const char *name;
};

constexpr Binding kBindings[] = {
    {Hotkeys::OpenFileExplorer, MOD_CONTROL | MOD_ALT, 'E', "Ctrl+Alt+E"},
    {Hotkeys::OpenTerminal, MOD_CONTROL | MOD_ALT, 'T', "Ctrl+Alt+T"},
    {Hotkeys::ShowRun, MOD_CONTROL | MOD_ALT, 'R', "Ctrl+Alt+R"},
    {Hotkeys::QuitToExplorer, MOD_CONTROL | MOD_ALT, 'Q', "Ctrl+Alt+Q"},
};

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE) {
        const auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    if (auto *self = reinterpret_cast<Hotkeys *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
        return self->handleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

const wchar_t *windowClass()
{
    static const ATOM atom = [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = windowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"VisorHotkeys";
        return RegisterClassExW(&wc);
    }();
    return atom ? L"VisorHotkeys" : nullptr;
}

} // namespace

Hotkeys::Hotkeys(QObject *parent)
    : QObject(parent)
{
    const HWND hwnd = CreateWindowExW(0, windowClass(), L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                      GetModuleHandleW(nullptr), this);
    if (!hwnd) {
        qCritical() << "failed to create hotkey window, error" << GetLastError();
        return;
    }
    m_hwnd = hwnd;

    for (const Binding &b : kBindings) {
        if (!RegisterHotKey(hwnd, b.action, b.modifiers | MOD_NOREPEAT, b.key))
            qWarning() << "could not register" << b.name << "error" << GetLastError();
    }
}

Hotkeys::~Hotkeys()
{
    const auto hwnd = static_cast<HWND>(m_hwnd);
    if (!hwnd)
        return;
    for (const Binding &b : kBindings)
        UnregisterHotKey(hwnd, b.action);
    DestroyWindow(hwnd);
}

std::intptr_t Hotkeys::handleMessage(void *hwnd, unsigned msg, std::uintptr_t wParam, std::intptr_t lParam)
{
    if (msg == WM_HOTKEY) {
        emit triggered(Action(wParam));
        return 0;
    }
    return DefWindowProcW(static_cast<HWND>(hwnd), msg, WPARAM(wParam), LPARAM(lParam));
}

} // namespace visor
