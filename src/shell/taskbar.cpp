#include "shell/taskbar.h"

#include <QDebug>
#include <QSettings>
#include <QTimer>

#include <windows.h>
#include <shellapi.h>

namespace visor {

namespace {

// Where install.ps1 keeps its state too.
const QString kStateKey = QStringLiteral("HKEY_CURRENT_USER\\Software\\visor-shell");
const QString kPreviousAutoHide = QStringLiteral("PreviousTaskbarAutoHide");

// Broadcast when Explorer (re)starts.
const UINT kTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
constexpr int kReapplyDelayMs = 1'000;

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE) {
        const auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    if (auto *self = reinterpret_cast<Taskbar *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
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
        wc.lpszClassName = L"VisorTaskbar";
        return RegisterClassExW(&wc);
    }();
    return atom ? L"VisorTaskbar" : nullptr;
}

// ABM_GETSTATE / ABM_SETSTATE: the taskbar's ABS_AUTOHIDE and
// ABS_ALWAYSONTOP flags. Explorer answers for its taskbar; with none
// running there is nothing to ask.
bool taskbarState(UINT &state)
{
    APPBARDATA abd{};
    abd.cbSize = sizeof(abd);
    abd.hWnd = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!abd.hWnd)
        return false;
    state = UINT(SHAppBarMessage(ABM_GETSTATE, &abd));
    return true;
}

void setTaskbarState(UINT state)
{
    APPBARDATA abd{};
    abd.cbSize = sizeof(abd);
    abd.hWnd = FindWindowW(L"Shell_TrayWnd", nullptr);
    abd.lParam = state;
    SHAppBarMessage(ABM_SETSTATE, &abd);
}

} // namespace

Taskbar::Taskbar(QObject *parent)
    : QObject(parent)
{
    // A hidden top-level window: message-only windows don't receive the
    // TaskbarCreated broadcast.
    const HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, windowClass(), L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                                      GetModuleHandleW(nullptr), this);
    if (!hwnd)
        qCritical() << "failed to create taskbar window, error" << GetLastError();
    m_hwnd = hwnd;
    hide();
}

Taskbar::~Taskbar()
{
    if (!m_sessionEnding)
        restore();
    if (m_hwnd)
        DestroyWindow(static_cast<HWND>(m_hwnd));
}

void Taskbar::hide()
{
    UINT state = 0;
    if (!taskbarState(state)) {
        qWarning() << "no Shell_TrayWnd; leaving the taskbar alone";
        return;
    }
    // Only the first change records the original: after a crash the taskbar
    // is already auto-hidden, and that is not what to put back.
    QSettings store(kStateKey, QSettings::NativeFormat);
    if (!store.contains(kPreviousAutoHide))
        store.setValue(kPreviousAutoHide, bool(state & ABS_AUTOHIDE));
    if (state & ABS_AUTOHIDE)
        return;
    setTaskbarState(state | ABS_AUTOHIDE);
    qInfo() << "taskbar set to auto-hide";
}

void Taskbar::restore()
{
    QSettings store(kStateKey, QSettings::NativeFormat);
    if (!store.contains(kPreviousAutoHide))
        return;
    UINT state = 0;
    if (!taskbarState(state)) {
        qWarning() << "no Shell_TrayWnd; the taskbar's state stays recorded for the next run";
        return;
    }
    const bool autoHide = store.value(kPreviousAutoHide).toBool();
    setTaskbarState(autoHide ? state | ABS_AUTOHIDE : state & ~UINT(ABS_AUTOHIDE));
    store.remove(kPreviousAutoHide);
    qInfo() << "taskbar auto-hide restored to" << autoHide;
}

std::intptr_t Taskbar::handleMessage(void *window, unsigned msg, std::uintptr_t wParam, std::intptr_t lParam)
{
    if (msg == kTaskbarCreated) {
        // A restarted Explorer may come back with its saved (shown) state.
        QTimer::singleShot(kReapplyDelayMs, this, &Taskbar::hide);
        return 0;
    }
    if (msg == WM_ENDSESSION) {
        m_sessionEnding = wParam != 0;
        return 0;
    }
    return DefWindowProcW(static_cast<HWND>(window), msg, WPARAM(wParam), LPARAM(lParam));
}

} // namespace visor
