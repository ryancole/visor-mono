#include "shell/appbars.h"

#include <QDebug>

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <algorithm>

namespace visor {

namespace {

// SHAppBarMessage's wire format (shell32 -> Shell_TrayWnd). Handles are sent as
// 32-bit values even from 64-bit processes.
struct AppBarData32
{
    DWORD cbSize;
    DWORD hWnd;
    DWORD uCallbackMessage;
    DWORD uEdge;
    RECT rc;
    INT64 lParam;
};
static_assert(sizeof(AppBarData32) == 40);

struct AppBarMessage
{
    AppBarData32 abd;
    DWORD dwMessage;
    DWORD padding1;
    INT64 hSharedMemory; // the caller's APPBARDATA, for results
    DWORD dwSourceProcessId;
    DWORD padding2;
};
static_assert(sizeof(AppBarMessage) == 64);

// Where results go: the caller's copy of the APPBARDATA, in memory it shared
// with us (SHAllocShared).
class SharedData
{
public:
    SharedData(const AppBarMessage &msg)
        : m_data(static_cast<AppBarData32 *>(
              SHLockShared(reinterpret_cast<HANDLE>(msg.hSharedMemory), msg.dwSourceProcessId)))
    {
    }
    ~SharedData()
    {
        if (m_data)
            SHUnlockShared(m_data);
    }
    AppBarData32 *operator->() const { return m_data; }
    explicit operator bool() const { return m_data != nullptr; }

private:
    AppBarData32 *m_data;
};

RECT toRect(const AppBars::Rect &r)
{
    return {r.left, r.top, r.right, r.bottom};
}

AppBars::Rect fromRect(const RECT &r)
{
    return {r.left, r.top, r.right, r.bottom};
}

HMONITOR monitorOf(const AppBars::Rect &r)
{
    const RECT rc = toRect(r);
    return MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);
}

RECT monitorRect(HMONITOR monitor)
{
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(monitor, &mi);
    return mi.rcMonitor;
}

QList<HMONITOR> monitors()
{
    QList<HMONITOR> list;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR m, HDC, LPRECT, LPARAM data) -> BOOL {
            reinterpret_cast<QList<HMONITOR> *>(data)->append(m);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&list));
    return list;
}

// A window counts as fullscreen when it is a real, visible app window
// covering its whole monitor (borderless or exclusive alike).
bool isFullscreen(HWND hwnd, HMONITOR monitor)
{
    if (!hwnd || !IsWindowVisible(hwnd) || IsIconic(hwnd) || hwnd == GetShellWindow())
        return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId())
        return false;
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return false;
    wchar_t cls[64] = {};
    GetClassNameW(hwnd, cls, int(std::size(cls)));
    if (wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0)
        return false;

    RECT rc;
    if (!GetWindowRect(hwnd, &rc))
        return false;
    const RECT m = monitorRect(monitor);
    return rc.left <= m.left && rc.top <= m.top && rc.right >= m.right && rc.bottom >= m.bottom;
}

} // namespace

AppBars::AppBars(QObject *parent)
    : QObject(parent)
{
    applyWorkAreas();
}

AppBars::~AppBars()
{
    for (HMONITOR monitor : monitors()) {
        RECT full = monitorRect(monitor);
        SystemParametersInfoW(SPI_SETWORKAREA, 0, &full, SPIF_SENDCHANGE);
    }
}

int AppBars::indexOf(quintptr hwnd) const
{
    for (qsizetype i = 0; i < m_bars.size(); ++i) {
        if (m_bars[i].hwnd == hwnd)
            return int(i);
    }
    return -1;
}

std::intptr_t AppBars::handle(const void *data, unsigned long size)
{
    if (size != sizeof(AppBarMessage))
        return 0;
    const auto &msg = *static_cast<const AppBarMessage *>(data);
    if (msg.abd.cbSize != sizeof(AppBarData32))
        return 0;
    const auto hwnd = quintptr(msg.abd.hWnd);

    switch (msg.dwMessage) {
    case ABM_NEW: {
        prune();
        if (indexOf(hwnd) >= 0)
            return FALSE;
        Bar bar;
        bar.hwnd = hwnd;
        bar.callback = msg.abd.uCallbackMessage;
        m_bars.append(bar);
        qInfo() << "appbar registered" << Qt::hex << hwnd;
        return TRUE;
    }
    case ABM_REMOVE: {
        const int i = indexOf(hwnd);
        if (i < 0)
            return FALSE;
        const bool positioned = m_bars[i].positioned;
        m_bars.removeAt(i);
        qInfo() << "appbar removed" << Qt::hex << hwnd;
        if (positioned) {
            applyWorkAreas();
            notifyOthers(hwnd, ABN_POSCHANGED);
            emit trayRectChanged();
        }
        return TRUE;
    }
    case ABM_QUERYPOS:
    case ABM_SETPOS: {
        const int i = indexOf(hwnd);
        SharedData shared(msg);
        if (i < 0 || !shared)
            return FALSE;
        Bar &bar = m_bars[i];
        bar.edge = shared->uEdge;
        const Rect rect = adjust(bar, fromRect(shared->rc));
        shared->rc = toRect(rect);
        if (msg.dwMessage == ABM_SETPOS) {
            const bool moved = !bar.positioned || memcmp(&bar.rect, &rect, sizeof(rect)) != 0;
            bar.rect = rect;
            bar.positioned = true;
            if (moved) {
                applyWorkAreas();
                notifyOthers(hwnd, ABN_POSCHANGED);
                emit trayRectChanged();
            }
        }
        return TRUE;
    }
    case ABM_GETTASKBARPOS: {
        SharedData shared(msg);
        if (!shared)
            return FALSE;
        shared->rc = toRect(trayRect());
        shared->uEdge = ABE_TOP;
        return TRUE;
    }
    case ABM_GETSTATE:
        return 0; // no auto-hide taskbar
    case ABM_GETAUTOHIDEBAR:
    case ABM_GETAUTOHIDEBAREX:
        return 0; // none
    case ABM_SETAUTOHIDEBAR:
    case ABM_SETAUTOHIDEBAREX:
        return FALSE; // auto-hide bars aren't supported (yet)
    case ABM_ACTIVATE:
    case ABM_WINDOWPOSCHANGED:
    case ABM_SETSTATE:
        return TRUE;
    default:
        return 0;
    }
}

// Moves `proposed` off the bars registered before this one on the same edge
// of the same monitor, keeping its thickness.
AppBars::Rect AppBars::adjust(const Bar &bar, Rect r) const
{
    const HMONITOR monitor = monitorOf(r);
    const RECT m = monitorRect(monitor);
    const long width = r.right - r.left;
    const long height = r.bottom - r.top;

    long inner = 0; // how far from the monitor edge earlier bars reach
    switch (bar.edge) {
    case ABE_LEFT: inner = m.left; break;
    case ABE_TOP: inner = m.top; break;
    case ABE_RIGHT: inner = m.right; break;
    case ABE_BOTTOM: inner = m.bottom; break;
    }
    for (const Bar &other : m_bars) {
        if (other.hwnd == bar.hwnd)
            break; // only bars registered earlier
        if (!other.positioned || other.edge != bar.edge || monitorOf(other.rect) != monitor)
            continue;
        switch (bar.edge) {
        case ABE_LEFT: inner = std::max(inner, other.rect.right); break;
        case ABE_TOP: inner = std::max(inner, other.rect.bottom); break;
        case ABE_RIGHT: inner = std::min(inner, other.rect.left); break;
        case ABE_BOTTOM: inner = std::min(inner, other.rect.top); break;
        }
    }

    switch (bar.edge) {
    case ABE_LEFT: r.left = inner; r.right = inner + width; break;
    case ABE_TOP: r.top = inner; r.bottom = inner + height; break;
    case ABE_RIGHT: r.right = inner; r.left = inner - width; break;
    case ABE_BOTTOM: r.bottom = inner; r.top = inner - height; break;
    }
    return r;
}

void AppBars::applyWorkAreas()
{
    for (HMONITOR monitor : monitors()) {
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(monitor, &mi);
        RECT work = mi.rcMonitor;
        for (const Bar &bar : m_bars) {
            if (!bar.positioned || monitorOf(bar.rect) != monitor)
                continue;
            switch (bar.edge) {
            case ABE_LEFT: work.left = std::max(work.left, bar.rect.right); break;
            case ABE_TOP: work.top = std::max(work.top, bar.rect.bottom); break;
            case ABE_RIGHT: work.right = std::min(work.right, bar.rect.left); break;
            case ABE_BOTTOM: work.bottom = std::min(work.bottom, bar.rect.top); break;
            }
        }
        if (!EqualRect(&work, &mi.rcWork)) {
            // Windows resizes maximised windows to the new work area itself.
            SystemParametersInfoW(SPI_SETWORKAREA, 0, &work, SPIF_SENDCHANGE);
            qInfo() << "work area" << work.left << work.top << work.right << work.bottom;
        }
    }
}

void AppBars::notifyOthers(quintptr except, unsigned code, std::intptr_t lParam)
{
    for (const Bar &bar : m_bars) {
        if (bar.hwnd != except)
            PostMessageW(reinterpret_cast<HWND>(bar.hwnd), bar.callback, code, lParam);
    }
}

void AppBars::prune()
{
    bool changed = false;
    for (qsizetype i = m_bars.size() - 1; i >= 0; --i) {
        if (!IsWindow(reinterpret_cast<HWND>(m_bars[i].hwnd))) {
            qInfo() << "appbar window gone" << Qt::hex << m_bars[i].hwnd;
            changed |= m_bars[i].positioned;
            m_bars.removeAt(i);
        }
    }
    if (changed) {
        applyWorkAreas();
        notifyOthers(0, ABN_POSCHANGED);
        emit trayRectChanged();
    }
}

void AppBars::displayChanged()
{
    prune();
    applyWorkAreas();
    notifyOthers(0, ABN_POSCHANGED);
    emit trayRectChanged();
}

AppBars::Rect AppBars::trayRect() const
{
    const HMONITOR primary = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    for (const Bar &bar : m_bars) {
        if (bar.positioned && bar.edge == ABE_TOP && monitorOf(bar.rect) == primary)
            return bar.rect;
    }
    const RECT m = monitorRect(primary);
    return {m.left, m.top, m.right, m.top};
}

void AppBars::checkFullscreen()
{
    // Only the foreground window counts: a bar hides while a fullscreen app
    // is in front on its monitor and comes back when anything else is.
    const HWND foreground = GetForegroundWindow();
    const HMONITOR monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
    QList<quintptr> now;
    if (isFullscreen(foreground, monitor))
        now.append(quintptr(monitor));
    if (now == m_fullscreenMonitors)
        return;

    for (const Bar &bar : m_bars) {
        const auto barMonitor = quintptr(MonitorFromWindow(reinterpret_cast<HWND>(bar.hwnd), MONITOR_DEFAULTTONEAREST));
        const bool was = m_fullscreenMonitors.contains(barMonitor);
        const bool is = now.contains(barMonitor);
        if (was != is)
            PostMessageW(reinterpret_cast<HWND>(bar.hwnd), bar.callback, ABN_FULLSCREENAPP, is);
    }
    m_fullscreenMonitors = now;
    qInfo() << "fullscreen app" << (now.isEmpty() ? "gone" : "in front");
}

} // namespace visor
