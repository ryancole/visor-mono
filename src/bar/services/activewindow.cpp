#include "services/activewindow.h"

#include <QFileInfo>
#include <QList>

#include <windows.h>

namespace {

// Hook procedures are plain functions; route events to every live instance
// (normally one, briefly two while a reload swaps QML engines).
QList<ActiveWindow *> &instances()
{
    static QList<ActiveWindow *> list;
    return list;
}

void CALLBACK winEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD)
{
    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF)
        return;

    switch (event) {
    case EVENT_SYSTEM_FOREGROUND:
        for (ActiveWindow *w : instances())
            w->onForegroundChanged(hwnd);
        break;
    case EVENT_SYSTEM_MINIMIZESTART:
    case EVENT_SYSTEM_MINIMIZEEND:
        // Minimizing doesn't always raise a foreground event; re-query.
        for (ActiveWindow *w : instances())
            w->onForegroundChanged(GetForegroundWindow());
        break;
    case EVENT_OBJECT_NAMECHANGE:
        for (ActiveWindow *w : instances())
            w->onNameChanged(hwnd);
        break;
    }
}

HWINEVENTHOOK hook(DWORD from, DWORD to)
{
    // Out-of-context: callbacks are delivered on this (the GUI) thread via its
    // message loop. Skip our own process so the bar never reports itself.
    return SetWinEventHook(from, to, nullptr, winEventProc, 0, 0,
                           WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
}

QString windowText(HWND hwnd)
{
    const int len = GetWindowTextLengthW(hwnd);
    if (len <= 0)
        return {};
    QString text(len, Qt::Uninitialized);
    const int got = GetWindowTextW(hwnd, reinterpret_cast<wchar_t *>(text.data()), len + 1);
    text.resize(qMax(0, got));
    return text;
}

QString processImagePath(DWORD pid)
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

} // namespace

ActiveWindow::ActiveWindow(QObject *parent)
    : QObject(parent)
{
    instances().append(this);
    m_foregroundHook = hook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND);
    m_minimizeHook = hook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND);
    m_nameHook = hook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE);
    onForegroundChanged(GetForegroundWindow());
}

ActiveWindow::~ActiveWindow()
{
    for (void *h : {m_foregroundHook, m_minimizeHook, m_nameHook}) {
        if (h)
            UnhookWinEvent(static_cast<HWINEVENTHOOK>(h));
    }
    instances().removeOne(this);
}

void ActiveWindow::onForegroundChanged(void *handle)
{
    auto hwnd = static_cast<HWND>(handle);
    // The desktop (Explorer's Progman, or visor-shell's window) and Visor's
    // own pop-ups aren't apps: with those in front there is no active
    // window, as the taskbar sees it.
    if (hwnd) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (hwnd == GetShellWindow() || pid == GetCurrentProcessId())
            hwnd = nullptr;
    }
    if (hwnd == m_hwnd) {
        refreshTitle();
        return;
    }
    m_hwnd = hwnd;

    DWORD pid = 0;
    if (hwnd)
        GetWindowThreadProcessId(hwnd, &pid);

    wchar_t cls[256] = {};
    if (hwnd)
        GetClassNameW(hwnd, cls, int(std::size(cls)));
    const QString className = QString::fromWCharArray(cls);

    if (pid != m_pid || className != m_className) {
        m_pid = pid;
        m_className = className;
        m_processPath = pid ? processImagePath(pid) : QString();
        m_appName = QFileInfo(m_processPath).completeBaseName();
        emit processChanged();
    }
    refreshTitle();
}

void ActiveWindow::onNameChanged(void *hwnd)
{
    // Name changes fire for every window on the desktop; only the focused
    // one matters.
    if (hwnd == m_hwnd)
        refreshTitle();
}

void ActiveWindow::refreshTitle()
{
    const QString title = m_hwnd ? windowText(static_cast<HWND>(m_hwnd)) : QString();
    if (title == m_title)
        return;
    m_title = title;
    emit titleChanged();
}
