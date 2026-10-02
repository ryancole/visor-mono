#include "wm/manager.h"

#include "wm/windows.h"

#include <QDebug>

#include <windows.h>

#include <algorithm>
#include <utility>

namespace visor::wm {

namespace {

constexpr int kSettleMs = 300;

WindowManager *g_instance = nullptr;

void CALLBACK winEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD)
{
    if (!hwnd || idObject != OBJID_WINDOW || idChild != CHILDID_SELF || !g_instance)
        return;
    g_instance->handleEvent(event, reinterpret_cast<quintptr>(hwnd));
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE) {
        const auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    if (auto *self = reinterpret_cast<WindowManager *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
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
        wc.lpszClassName = L"VisorWm";
        return RegisterClassExW(&wc);
    }();
    return atom ? L"VisorWm" : nullptr;
}

Rect toRect(const RECT &r)
{
    return {int(r.left), int(r.top), int(r.right), int(r.bottom)};
}

QString deviceName(HMONITOR monitor, MONITORINFOEXW *info = nullptr)
{
    MONITORINFOEXW local{};
    MONITORINFOEXW &mi = info ? *info : local;
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi))
        return {};
    return QString::fromWCharArray(mi.szDevice);
}

} // namespace

WindowManager::WindowManager(Config config, QObject *parent)
    : QObject(parent)
    , m_config(std::move(config))
{
    g_instance = this;
    m_settleTimer.setSingleShot(true);
    m_settleTimer.setInterval(kSettleMs);
    connect(&m_settleTimer, &QTimer::timeout, this, &WindowManager::arrangeAll);

    // Top-level (hidden): WM_DISPLAYCHANGE and WM_SETTINGCHANGE are only
    // broadcast to top-level windows, not message-only ones.
    const HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, windowClass(), L"visor-wm", WS_POPUP, 0, 0, 0, 0, nullptr,
                                      nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd)
        qCritical() << "failed to create window, error" << GetLastError();
    m_hwnd = hwnd;

    refreshMonitors();

    const std::pair<DWORD, DWORD> ranges[] = {
        {EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND},
        {EVENT_SYSTEM_MOVESIZEEND, EVENT_SYSTEM_MOVESIZEEND},
        {EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND},
        {EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE},
        {EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE},
        {EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED},
    };
    for (const auto &[from, to] : ranges) {
        if (HWINEVENTHOOK hook = SetWinEventHook(from, to, nullptr, winEventProc, 0, 0,
                                                 WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS))
            m_hooks.append(hook);
        else
            qWarning() << "SetWinEventHook failed for event" << Qt::hex << from << "error" << GetLastError();
    }

    // Adopt what is already open, bottom of the z-order first so the
    // longest-standing windows get the biggest tiles.
    QList<quintptr> existing;
    EnumWindows(
        [](HWND w, LPARAM list) -> BOOL {
            reinterpret_cast<QList<quintptr> *>(list)->append(reinterpret_cast<quintptr>(w));
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&existing));
    std::reverse(existing.begin(), existing.end());
    for (quintptr w : std::as_const(existing))
        consider(w);
    focusChanged(reinterpret_cast<quintptr>(GetForegroundWindow()));
    qInfo() << "tiling" << m_managed.size() << "windows on" << m_monitors.size() << "monitors";
}

WindowManager::~WindowManager()
{
    g_instance = nullptr;
    for (void *hook : std::as_const(m_hooks))
        UnhookWinEvent(static_cast<HWINEVENTHOOK>(hook));
    for (quintptr w : std::as_const(m_colored)) {
        if (win::exists(w))
            win::resetBorderColor(w);
    }
    if (m_hwnd)
        DestroyWindow(static_cast<HWND>(m_hwnd));
}

void WindowManager::setConfig(Config config)
{
    m_config = std::move(config);
    for (quintptr w : std::as_const(m_colored)) {
        if (!win::exists(w))
            continue;
        if (m_config.borderSize > 0)
            win::setBorderColor(w, w == m_active ? m_config.activeBorder : m_config.inactiveBorder);
        else
            win::resetBorderColor(w);
    }
    if (m_config.borderSize <= 0)
        m_colored.clear();
    arrangeAll();
}

void WindowManager::handleEvent(unsigned event, quintptr hwnd)
{
    switch (event) {
    case EVENT_OBJECT_SHOW:
    case EVENT_OBJECT_UNCLOAKED:
    case EVENT_SYSTEM_MINIMIZEEND:
        consider(hwnd);
        break;
    case EVENT_OBJECT_DESTROY:
        m_colored.remove(hwnd);
        if (hwnd == m_active)
            m_active = 0;
        Q_FALLTHROUGH();
    case EVENT_OBJECT_HIDE:
    case EVENT_OBJECT_CLOAKED:
    case EVENT_SYSTEM_MINIMIZESTART:
        if (m_managed.contains(hwnd))
            unmanage(hwnd);
        break;
    case EVENT_SYSTEM_FOREGROUND:
        consider(hwnd);
        focusChanged(hwnd);
        break;
    case EVENT_SYSTEM_MOVESIZEEND: {
        // The user dragged or resized a tiled window: it goes back into its
        // tile, or into the layout of the monitor it was dropped on.
        const auto it = m_managed.constFind(hwnd);
        if (it == m_managed.cend())
            break;
        const QString monitor = monitorOf(hwnd);
        if (monitor != it->monitor)
            moveToMonitor(hwnd, monitor);
        else
            arrange(monitor);
        break;
    }
    case EVENT_OBJECT_LOCATIONCHANGE: {
        const auto it = m_managed.find(hwnd);
        if (it == m_managed.end())
            break;
        const bool held = win::isMaximized(hwnd) || win::isFullscreen(hwnd);
        if (held != it->held) {
            it->held = held;
            if (!held)
                arrange(it->monitor); // back from maximised/fullscreen: into its tile
        }
        break;
    }
    default:
        break;
    }
}

std::intptr_t WindowManager::handleMessage(void *window, unsigned msg, std::uintptr_t wParam, std::intptr_t lParam)
{
    if (msg == WM_DISPLAYCHANGE || (msg == WM_SETTINGCHANGE && wParam == SPI_SETWORKAREA)) {
        refreshMonitors();
        arrangeAll();
        settleSoon();
    }
    return DefWindowProcW(static_cast<HWND>(window), msg, WPARAM(wParam), LPARAM(lParam));
}

void WindowManager::refreshMonitors()
{
    QHash<QString, Monitor> monitors;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM list) -> BOOL {
            MONITORINFOEXW info{};
            const QString name = deviceName(monitor, &info);
            if (!name.isEmpty()) {
                reinterpret_cast<QHash<QString, Monitor> *>(list)->insert(
                    name, {toRect(info.rcWork), (info.dwFlags & MONITORINFOF_PRIMARY) != 0});
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&monitors));
    if (monitors.isEmpty())
        return; // mid display change; keep what we had
    m_monitors = monitors;

    // Windows on a monitor that went away move to the primary one.
    const QString primary = primaryMonitor();
    for (auto it = m_workspaces.begin(); it != m_workspaces.end();) {
        if (m_monitors.contains(it->first)) {
            ++it;
            continue;
        }
        const QList<quintptr> orphans = it->second.layout.windows();
        it = m_workspaces.erase(it);
        Workspace &target = workspace(primary);
        const Rect area = m_monitors.value(primary).work;
        for (quintptr w : orphans) {
            target.layout.insert(w, 0, area, m_config.dwindle);
            m_managed[w].monitor = primary;
        }
        qInfo() << "monitor removed; moved" << orphans.size() << "windows to" << primary;
    }
}

QString WindowManager::monitorOf(quintptr hwnd) const
{
    const QString name = deviceName(MonitorFromWindow(reinterpret_cast<HWND>(hwnd), MONITOR_DEFAULTTONEAREST));
    return m_monitors.contains(name) ? name : primaryMonitor();
}

QString WindowManager::primaryMonitor() const
{
    for (auto it = m_monitors.cbegin(); it != m_monitors.cend(); ++it) {
        if (it->primary)
            return it.key();
    }
    return m_monitors.isEmpty() ? QString() : m_monitors.cbegin().key();
}

WindowManager::Workspace &WindowManager::workspace(const QString &monitor)
{
    return m_workspaces[monitor];
}

void WindowManager::consider(quintptr hwnd)
{
    QString reason;
    const win::Kind kind = win::classify(hwnd, m_config, &reason);
    const bool managed = m_managed.contains(hwnd);
    if (!managed && kind == win::Kind::Tile)
        manage(hwnd);
    // A tiled window that turns maximised, fullscreen or topmost keeps its
    // tile; only one that stops being an app window at all is dropped.
    else if (managed && kind == win::Kind::Ignore)
        unmanage(hwnd);
}

void WindowManager::manage(quintptr hwnd)
{
    const QString monitor = monitorOf(hwnd);
    Workspace &ws = workspace(monitor);
    // Hyprland splits the focused window; that's still the previous one when
    // the new window's show event arrives.
    const quintptr target = ws.layout.contains(ws.lastFocused) ? ws.lastFocused : 0;
    POINT cursor{};
    GetCursorPos(&cursor);

    if (win::isMaximized(hwnd))
        win::unmaximize(hwnd);
    ws.layout.insert(hwnd, target, m_monitors.value(monitor).work, m_config.dwindle, cursor.x, cursor.y);
    m_managed.insert(hwnd, {monitor, false});
    colorBorder(hwnd, hwnd == m_active);
    qInfo().noquote() << "tile" << win::exeName(hwnd) << win::className(hwnd) << QLatin1Char('"') + win::title(hwnd) + QLatin1Char('"')
                      << "on" << monitor;
    arrange(monitor);
    settleSoon();
}

void WindowManager::unmanage(quintptr hwnd)
{
    const QString monitor = m_managed.take(hwnd).monitor;
    Workspace &ws = workspace(monitor);
    ws.layout.remove(hwnd);
    if (ws.lastFocused == hwnd)
        ws.lastFocused = 0;
    arrange(monitor);
}

void WindowManager::moveToMonitor(quintptr hwnd, const QString &monitor)
{
    Managed &m = m_managed[hwnd];
    const QString from = m.monitor;
    Workspace &old = workspace(from);
    old.layout.remove(hwnd);
    if (old.lastFocused == hwnd)
        old.lastFocused = 0;

    Workspace &ws = workspace(monitor);
    const quintptr target = ws.layout.contains(ws.lastFocused) ? ws.lastFocused : 0;
    POINT cursor{};
    GetCursorPos(&cursor);
    ws.layout.insert(hwnd, target, m_monitors.value(monitor).work, m_config.dwindle, cursor.x, cursor.y);
    m.monitor = monitor;
    arrange(from);
    arrange(monitor);
}

void WindowManager::arrange(const QString &monitor)
{
    const auto mon = m_monitors.constFind(monitor);
    if (mon == m_monitors.cend())
        return;
    const QList<DwindleLayout::Placement> placements =
        workspace(monitor).layout.arrange(mon->work, m_config.gapsIn, m_config.gapsOut, m_config.dwindle);
    for (const DwindleLayout::Placement &p : placements) {
        if (!win::exists(p.window) || win::isMinimized(p.window) || win::isMaximized(p.window)
            || win::isFullscreen(p.window))
            continue;
        win::moveTo(p.window, p.rect);
    }
}

void WindowManager::arrangeAll()
{
    for (auto it = m_monitors.cbegin(); it != m_monitors.cend(); ++it)
        arrange(it.key());
}

void WindowManager::settleSoon()
{
    m_settleTimer.start();
}

void WindowManager::focusChanged(quintptr hwnd)
{
    if (m_active && m_active != hwnd && m_colored.contains(m_active) && win::exists(m_active))
        colorBorder(m_active, false);
    m_active = hwnd;
    if (!hwnd)
        return;
    if (win::classify(hwnd, m_config) != win::Kind::Ignore)
        colorBorder(hwnd, true);
    if (const auto it = m_managed.constFind(hwnd); it != m_managed.cend())
        workspace(it->monitor).lastFocused = hwnd;
}

void WindowManager::colorBorder(quintptr hwnd, bool active)
{
    if (m_config.borderSize <= 0)
        return;
    win::setBorderColor(hwnd, active ? m_config.activeBorder : m_config.inactiveBorder);
    m_colored.insert(hwnd);
}

} // namespace visor::wm
