#include "services/inputlanguage.h"

#include <QDebug>
#include <QList>

#include <windows.h>

namespace {

// Hook procedures are plain functions; route to every live instance (two,
// briefly, while a config reload swaps QML engines).
QList<InputLanguage *> &instances()
{
    static QList<InputLanguage *> list;
    return list;
}

void CALLBACK winEventProc(HWINEVENTHOOK, DWORD event, HWND, LONG idObject, LONG idChild, DWORD, DWORD)
{
    if (event != EVENT_SYSTEM_FOREGROUND || idObject != OBJID_WINDOW || idChild != CHILDID_SELF)
        return;
    for (InputLanguage *l : instances())
        l->refresh();
}

// The shell hook message, for HSHELL_LANGUAGE: the focused window's input
// language changed (Win+Space, or the app itself). Any window may register
// for it, shell or not.
const UINT kShellHook = RegisterWindowMessageW(L"SHELLHOOK");

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == kShellHook && wParam == HSHELL_LANGUAGE) {
        for (InputLanguage *l : instances())
            l->refresh();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

const wchar_t *windowClass()
{
    static const ATOM atom = [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = windowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"VisorInputLanguage";
        return RegisterClassExW(&wc);
    }();
    return atom ? L"VisorInputLanguage" : nullptr;
}

// The layout of the window with keyboard focus (each thread has its own).
HKL currentLayout()
{
    const HWND foreground = GetForegroundWindow();
    return GetKeyboardLayout(foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0);
}

QList<HKL> layouts()
{
    QList<HKL> list(GetKeyboardLayoutList(0, nullptr));
    if (!list.isEmpty())
        list.resize(GetKeyboardLayoutList(int(list.size()), list.data()));
    return list;
}

QString localeInfo(const wchar_t *locale, LCTYPE type)
{
    wchar_t buffer[128];
    const int len = GetLocaleInfoEx(locale, type, buffer, int(std::size(buffer)));
    return len > 1 ? QString::fromWCharArray(buffer, len - 1) : QString();
}

} // namespace

struct InputLanguage::Impl
{
    HWND hwnd = nullptr;
    HWINEVENTHOOK hook = nullptr;
};

InputLanguage::InputLanguage(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Impl>())
{
    instances().append(this);
    // Top-level and hidden: shell hook messages are posted to the window.
    d->hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, windowClass(), L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                              GetModuleHandleW(nullptr), nullptr);
    if (d->hwnd && !RegisterShellHookWindow(d->hwnd))
        qWarning() << "InputLanguage: RegisterShellHookWindow failed, error" << GetLastError();
    d->hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, winEventProc, 0, 0,
                              WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    refresh();
}

InputLanguage::~InputLanguage()
{
    instances().removeAll(this);
    if (d->hook)
        UnhookWinEvent(d->hook);
    if (d->hwnd) {
        DeregisterShellHookWindow(d->hwnd);
        DestroyWindow(d->hwnd);
    }
}

void InputLanguage::refresh()
{
    const int count = int(layouts().size());
    QString code, name;
    // The low word of an HKL is the language; LCIDToLocaleName turns that
    // into "en-US", which the locale API names.
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
    const LANGID lang = LOWORD(reinterpret_cast<ULONG_PTR>(currentLayout()));
    if (LCIDToLocaleName(MAKELCID(lang, SORT_DEFAULT), locale, LOCALE_NAME_MAX_LENGTH, 0)) {
        code = localeInfo(locale, LOCALE_SISO639LANGNAME2).toUpper();
        name = localeInfo(locale, LOCALE_SLOCALIZEDDISPLAYNAME);
    }
    if (count == m_count && code == m_code && name == m_name)
        return;
    m_count = count;
    m_code = code;
    m_name = name;
    emit changed();
}

void InputLanguage::next()
{
    const QList<HKL> list = layouts();
    const HWND foreground = GetForegroundWindow();
    if (list.size() < 2 || !foreground)
        return;
    const qsizetype index = list.indexOf(currentLayout());
    const HKL target = list[(index + 1) % list.size()];
    // What the language bar does: ask the window to switch its own layout.
    PostMessageW(foreground, WM_INPUTLANGCHANGEREQUEST, 0, reinterpret_cast<LPARAM>(target));
}
