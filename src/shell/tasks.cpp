#include "shell/tasks.h"

#include <QDebug>

#include <windows.h>
#include <dwmapi.h>

#include <algorithm>

namespace visor {

namespace {

// HSHELL_* codes not in every SDK header.
constexpr WPARAM kWindowReplaced = 13;
constexpr WPARAM kWindowReplacing = 14;
constexpr WPARAM kRudeAppActivated = HSHELL_WINDOWACTIVATED | HSHELL_HIGHBIT;
constexpr WPARAM kFlash = HSHELL_REDRAW | HSHELL_HIGHBIT;

QList<Tasks *> &instances()
{
    static QList<Tasks *> list;
    return list;
}

void CALLBACK cloakEventProc(HWINEVENTHOOK, DWORD, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD)
{
    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF)
        return;
    for (Tasks *t : instances())
        t->reevaluate(reinterpret_cast<quintptr>(hwnd));
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE) {
        const auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    if (auto *self = reinterpret_cast<Tasks *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
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
        wc.lpszClassName = L"VisorTasks";
        return RegisterClassExW(&wc);
    }();
    return atom ? L"VisorTasks" : nullptr;
}

bool isCloaked(HWND hwnd)
{
    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked;
}

// Explorer's rules for a taskbar button: a visible, uncloaked top-level window
// that is either unowned and not a tool window, or explicitly WS_EX_APPWINDOW.
bool isTaskWindow(HWND hwnd)
{
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd) || GetAncestor(hwnd, GA_PARENT) != GetDesktopWindow())
        return false;
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (!(ex & WS_EX_APPWINDOW)) {
        if (ex & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE))
            return false;
        if (GetWindow(hwnd, GW_OWNER))
            return false;
    }
    return !isCloaked(hwnd);
}

QString windowTitle(HWND hwnd)
{
    // GetWindowText doesn't send WM_GETTEXT to other processes' windows, so
    // a hung app can't block us here.
    wchar_t buffer[512];
    const int len = GetWindowTextW(hwnd, buffer, int(std::size(buffer)));
    return QString::fromWCharArray(buffer, qMax(0, len));
}

QString processPath(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return {};
    wchar_t buffer[MAX_PATH * 2];
    DWORD size = DWORD(std::size(buffer));
    QString path;
    if (QueryFullProcessImageNameW(process, 0, buffer, &size))
        path = QString::fromWCharArray(buffer, int(size));
    CloseHandle(process);
    return path;
}

// Undocumented but used by every alternative shell: marks the window that
// receives task-manager notifications.
void setTaskmanWindow(HWND hwnd)
{
    using SetTaskmanWindowFn = BOOL(WINAPI *)(HWND);
    static const auto fn =
        reinterpret_cast<SetTaskmanWindowFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetTaskmanWindow"));
    if (fn && !fn(hwnd))
        qWarning() << "SetTaskmanWindow failed, error" << GetLastError();
}

} // namespace

Tasks::Tasks(bool asShell, QObject *parent)
    : QObject(parent)
{
    // A hidden top-level window: shell hook messages aren't delivered to
    // message-only windows.
    const HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, windowClass(), L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                                      GetModuleHandleW(nullptr), this);
    if (!hwnd) {
        qCritical() << "failed to create tasks window, error" << GetLastError();
        return;
    }
    m_hwnd = hwnd;
    instances().append(this);

    m_shellHookMessage = RegisterWindowMessageW(L"SHELLHOOK");
    if (asShell)
        setTaskmanWindow(hwnd);
    if (!RegisterShellHookWindow(hwnd))
        qWarning() << "RegisterShellHookWindow failed, error" << GetLastError();

    // Cloaking hides windows without the shell hook noticing: UWP frames
    // before their content arrives, and windows on other virtual desktops.
    m_cloakHook = SetWinEventHook(EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED, nullptr, cloakEventProc, 0, 0,
                                  WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    EnumWindows(
        [](HWND w, LPARAM self) -> BOOL {
            if (isTaskWindow(w))
                reinterpret_cast<Tasks *>(self)->add(reinterpret_cast<quintptr>(w));
            return TRUE;
        },
        reinterpret_cast<LPARAM>(this));
    // EnumWindows walks top of the z-order first; list oldest-looking first.
    std::reverse(m_tasks.begin(), m_tasks.end());
    m_active = reinterpret_cast<quintptr>(GetForegroundWindow());
    qInfo() << "tracking" << m_tasks.size() << "tasks";
}

Tasks::~Tasks()
{
    instances().removeOne(this);
    if (m_cloakHook)
        UnhookWinEvent(static_cast<HWINEVENTHOOK>(m_cloakHook));
    if (m_hwnd) {
        DeregisterShellHookWindow(static_cast<HWND>(m_hwnd));
        DestroyWindow(static_cast<HWND>(m_hwnd));
    }
}

QList<Tasks::Task> Tasks::tasks() const
{
    return m_tasks;
}

void Tasks::add(quintptr hwnd)
{
    for (const Task &t : m_tasks) {
        if (t.hwnd == hwnd)
            return;
    }
    const auto w = reinterpret_cast<HWND>(hwnd);
    Task task;
    task.hwnd = hwnd;
    task.title = windowTitle(w);
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    task.pid = pid;
    task.path = processPath(pid);
    m_tasks.append(task);
    emit added(task);
}

void Tasks::remove(quintptr hwnd)
{
    for (qsizetype i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks[i].hwnd == hwnd) {
            m_tasks.removeAt(i);
            emit removed(hwnd);
            return;
        }
    }
}

void Tasks::refreshTitle(quintptr hwnd)
{
    for (Task &t : m_tasks) {
        if (t.hwnd != hwnd)
            continue;
        const QString title = windowTitle(reinterpret_cast<HWND>(hwnd));
        if (title != t.title) {
            t.title = title;
            emit changed(t);
        }
        return;
    }
}

void Tasks::reevaluate(quintptr hwnd)
{
    if (isTaskWindow(reinterpret_cast<HWND>(hwnd)))
        add(hwnd);
    else
        remove(hwnd);
}

std::intptr_t Tasks::handleMessage(void *window, unsigned msg, std::uintptr_t wParam, std::intptr_t lParam)
{
    if (msg != m_shellHookMessage || !m_shellHookMessage)
        return DefWindowProcW(static_cast<HWND>(window), msg, WPARAM(wParam), LPARAM(lParam));

    const auto hwnd = quintptr(lParam);
    switch (wParam) {
    case HSHELL_WINDOWCREATED:
    case kWindowReplacing:
        reevaluate(hwnd);
        break;
    case HSHELL_WINDOWDESTROYED:
    case kWindowReplaced:
        remove(hwnd);
        break;
    case HSHELL_WINDOWACTIVATED:
    case kRudeAppActivated:
        if (hwnd)
            reevaluate(hwnd);
        for (Task &t : m_tasks) {
            if (t.hwnd == hwnd && t.flashing) {
                t.flashing = false;
                emit changed(t);
            }
        }
        if (hwnd != m_active) {
            m_active = hwnd;
            emit activated(hwnd);
        }
        break;
    case HSHELL_REDRAW:
        refreshTitle(hwnd);
        break;
    case kFlash:
        for (Task &t : m_tasks) {
            if (t.hwnd == hwnd && !t.flashing && hwnd != m_active) {
                t.flashing = true;
                emit changed(t);
            }
        }
        break;
    default:
        break;
    }
    return 0;
}

} // namespace visor
