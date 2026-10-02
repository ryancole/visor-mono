#include "wm/manager.h"

#include "common/launch.h"
#include "wm/windows.h"

#include <QDebug>

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <tuple>
#include <utility>

namespace visor::wm {

namespace {

constexpr int kSettleMs = 300;
constexpr int kRecolorMs = 150;
constexpr UINT kHookKeyMessage = WM_APP + 1; // from KeyHook; wParam = binding id

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
    m_recolorTimer.setSingleShot(true);
    m_recolorTimer.setInterval(kRecolorMs);
    connect(&m_recolorTimer, &QTimer::timeout, this, [this] {
        if (m_previousActive && m_previousActive != m_active && m_colored.contains(m_previousActive)
            && win::exists(m_previousActive))
            colorBorder(m_previousActive, false);
        if (m_active && m_colored.contains(m_active) && win::exists(m_active))
            colorBorder(m_active, true);
    });

    // Top-level (hidden): WM_DISPLAYCHANGE and WM_SETTINGCHANGE are only
    // broadcast to top-level windows, not message-only ones.
    const HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, windowClass(), L"visor-wm", WS_POPUP, 0, 0, 0, 0, nullptr,
                                      nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd)
        qCritical() << "failed to create window, error" << GetLastError();
    m_hwnd = hwnd;
    m_keyHook = std::make_unique<KeyHook>(m_hwnd, kHookKeyMessage);

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
        // Focus changes are wanted even when we cause them (movefocus); our
        // own windows are hidden, so they never take focus themselves.
        const DWORD flags = WINEVENT_OUTOFCONTEXT | (from == EVENT_SYSTEM_FOREGROUND ? 0 : WINEVENT_SKIPOWNPROCESS);
        if (HWINEVENTHOOK hook = SetWinEventHook(from, to, nullptr, winEventProc, 0, 0, flags))
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
    registerBindings();
}

WindowManager::~WindowManager()
{
    g_instance = nullptr;
    unregisterBindings();
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
    unregisterBindings();
    m_config = std::move(config);
    registerBindings();
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
        m_tileOverride.remove(hwnd);
        m_floatFullscreen.remove(hwnd);
        if (hwnd == m_active)
            m_active = 0;
        if (hwnd == m_previousActive)
            m_previousActive = 0;
        m_focusedAt.remove(hwnd);
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
    if (msg == WM_HOTKEY || msg == kHookKeyMessage) {
        const qsizetype index = qsizetype(wParam) - 1;
        if (index >= 0 && index < m_registeredBindings && index < m_config.bindings.size())
            dispatch(m_config.bindings[index]);
        return 0;
    }
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
                    name, {toRect(info.rcMonitor), toRect(info.rcWork), (info.dwFlags & MONITORINFOF_PRIMARY) != 0});
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
    win::Kind kind = win::classify(hwnd, m_config, &reason);
    // togglefloating beats rules and heuristics, for as long as the window lives.
    if (kind != win::Kind::Ignore) {
        if (const auto it = m_tileOverride.constFind(hwnd); it != m_tileOverride.cend())
            kind = *it ? win::Kind::Tile : win::Kind::Float;
    }
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
    m_tiles.remove(hwnd);
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
        m_tiles.insert(p.window, p.rect);
        if (!win::exists(p.window) || win::isMinimized(p.window) || win::isMaximized(p.window))
            continue;
        if (m_managed.value(p.window).fullscreen)
            win::moveTo(p.window, mon->full);
        else if (!win::isFullscreen(p.window))
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
    if (m_active != hwnd)
        m_previousActive = m_active;
    m_active = hwnd;
    if (hwnd)
        m_focusedAt.insert(hwnd, ++m_focusCount);
    m_recolorTimer.start();
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

void WindowManager::registerBindings()
{
    const auto hwnd = static_cast<HWND>(m_hwnd);
    if (!hwnd)
        return;
    // RegisterHotKey first: it works everywhere, even while an elevated
    // window has focus. Keys Windows keeps for itself go to the hook.
    const QList<Binding> &bindings = m_config.bindings;
    QList<KeyHook::Key> hooked;
    for (qsizetype i = 0; i < bindings.size(); ++i) {
        const Binding &b = bindings[i];
        const int id = int(i + 1);
        if (RegisterHotKey(hwnd, id, b.modifiers | (b.repeat ? 0 : MOD_NOREPEAT), b.key))
            continue;
        if (GetLastError() != ERROR_HOTKEY_ALREADY_REGISTERED)
            qWarning().noquote() << "cannot bind" << b.name << "error" << GetLastError();
        hooked.append({b.modifiers, b.key, b.repeat, id});
    }
    m_registeredBindings = bindings.size();
    m_keyHook->setKeys(hooked);
    qInfo() << m_registeredBindings << "key bindings," << hooked.size() << "of them through the keyboard hook";
}

void WindowManager::unregisterBindings()
{
    const auto hwnd = static_cast<HWND>(m_hwnd);
    for (qsizetype i = 0; hwnd && i < m_registeredBindings; ++i)
        UnregisterHotKey(hwnd, int(i + 1));
    m_registeredBindings = 0;
    if (m_keyHook)
        m_keyHook->setKeys({});
}

void WindowManager::dispatch(const Binding &binding)
{
    const QString &d = binding.dispatcher;
    const QString arg = binding.argument.trimmed().toLower();
    const auto direction = [&arg] {
        return arg == QLatin1String("l")   ? Direction::Left
               : arg == QLatin1String("r") ? Direction::Right
               : arg == QLatin1String("u") ? Direction::Up
                                           : Direction::Down;
    };

    qInfo().noquote() << binding.name << "->" << d << binding.argument;
    if (d == QLatin1String("exec")) {
        visor::run(binding.argument);
    } else if (d == QLatin1String("killactive")) {
        killActive();
    } else if (d == QLatin1String("togglefloating")) {
        toggleFloating();
    } else if (d == QLatin1String("fullscreen")) {
        fullscreen(arg == QLatin1String("1"));
    } else if (d == QLatin1String("movefocus")) {
        moveFocus(direction());
    } else if (d == QLatin1String("swapwindow")) {
        swapWindow(direction());
    } else if (d == QLatin1String("togglesplit")) {
        toggleSplit();
    } else if (d == QLatin1String("resizeactive")) {
        const QStringList xy = arg.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        resizeActive(xy.value(0).toInt(), xy.value(1).toInt());
    }
}

void WindowManager::killActive()
{
    // Never the desktop or the bar.
    const quintptr hwnd = win::foreground();
    if (hwnd && win::classify(hwnd, m_config) != win::Kind::Ignore)
        win::close(hwnd);
}

void WindowManager::toggleFloating()
{
    const quintptr hwnd = win::foreground();
    if (!hwnd)
        return;
    if (const auto it = m_managed.constFind(hwnd); it != m_managed.cend()) {
        // Like Hyprland: float it centred, at a size that shows it's floating.
        const Rect work = m_monitors.value(it->monitor).work;
        m_tileOverride.insert(hwnd, false);
        unmanage(hwnd);
        if (win::isMaximized(hwnd))
            win::unmaximize(hwnd);
        const int w = work.width() * 2 / 3;
        const int h = work.height() * 2 / 3;
        const int left = work.left + (work.width() - w) / 2;
        const int top = work.top + (work.height() - h) / 2;
        win::moveTo(hwnd, {left, top, left + w, top + h});
        win::raise(hwnd);
    } else if (win::classify(hwnd, m_config) != win::Kind::Ignore) {
        m_tileOverride.insert(hwnd, true);
        m_floatFullscreen.remove(hwnd);
        manage(hwnd);
    }
}

void WindowManager::fullscreen(bool maximizeOnly)
{
    const quintptr hwnd = win::foreground();
    if (!hwnd || win::classify(hwnd, m_config) == win::Kind::Ignore)
        return;
    if (maximizeOnly) {
        // fullscreen 1: Windows' own maximise (fills the work area, so the
        // bar stays). A tiled window keeps its tile for when it's restored.
        win::toggleMaximized(hwnd);
        return;
    }

    // fullscreen 0: the whole monitor, over the bar.
    if (const auto it = m_managed.find(hwnd); it != m_managed.end()) {
        it->fullscreen = !it->fullscreen;
        if (it->fullscreen) {
            if (win::isMaximized(hwnd))
                win::unmaximize(hwnd);
            win::raise(hwnd);
        }
        arrange(it->monitor);
        settleSoon(); // after an unmaximise the borders are only right once it's done
        return;
    }
    if (const auto it = m_floatFullscreen.constFind(hwnd); it != m_floatFullscreen.cend()) {
        win::moveTo(hwnd, *it);
        m_floatFullscreen.erase(it);
        return;
    }
    if (win::isMaximized(hwnd))
        win::unmaximize(hwnd);
    m_floatFullscreen.insert(hwnd, win::frameRect(hwnd));
    win::moveTo(hwnd, m_monitors.value(monitorOf(hwnd)).full);
    win::raise(hwnd);
}

void WindowManager::moveFocus(Direction direction)
{
    const quintptr hwnd = win::foreground();
    Rect from;
    if (m_managed.contains(hwnd) && m_tiles.contains(hwnd))
        from = m_tiles.value(hwnd);
    else if (hwnd && win::classify(hwnd, m_config) != win::Kind::Ignore)
        from = win::frameRect(hwnd); // a floating window
    else
        return; // e.g. the desktop: nothing to move from
    const quintptr target = neighbor(from, direction, hwnd);
    if (target && !win::focus(target))
        qWarning() << "could not focus window" << Qt::hex << target << "error" << Qt::dec << GetLastError();
}

void WindowManager::swapWindow(Direction direction)
{
    const quintptr hwnd = win::foreground();
    const auto a = m_managed.find(hwnd);
    if (a == m_managed.end())
        return; // Hyprland doesn't swap floating windows either
    const quintptr target = neighbor(m_tiles.value(hwnd), direction, hwnd);
    const auto b = m_managed.find(target);
    if (!target || b == m_managed.end())
        return;

    const QString from = a->monitor;
    const QString to = b->monitor;
    if (from == to) {
        workspace(from).layout.swap(hwnd, target);
    } else {
        workspace(from).layout.replace(hwnd, target);
        workspace(to).layout.replace(target, hwnd);
        a->monitor = to;
        b->monitor = from;
        workspace(to).lastFocused = hwnd;
        if (workspace(from).lastFocused == hwnd)
            workspace(from).lastFocused = target;
        arrange(to);
    }
    arrange(from);
}

void WindowManager::toggleSplit()
{
    const quintptr hwnd = win::foreground();
    if (const auto it = m_managed.constFind(hwnd); it != m_managed.cend()) {
        workspace(it->monitor).layout.toggleSplit(hwnd);
        arrange(it->monitor);
    }
}

void WindowManager::resizeActive(int dx, int dy)
{
    const quintptr hwnd = win::foreground();
    if (const auto it = m_managed.constFind(hwnd); it != m_managed.cend()) {
        if (workspace(it->monitor).layout.resize(hwnd, dx, dy))
            arrange(it->monitor);
    }
}

quintptr WindowManager::neighbor(const Rect &from, Direction direction, quintptr exclude) const
{
    // Candidates must lie in the direction (their centre beyond ours) and
    // overlap us across it (share some height for left/right, some width for
    // up/down). Of those: the nearest facing edge, then (like Hyprland) the
    // most recently focused, then the most aligned.
    const auto centerX = [](const Rect &r) { return (r.left + r.right) / 2; };
    const auto centerY = [](const Rect &r) { return (r.top + r.bottom) / 2; };
    const bool horizontal = direction == Direction::Left || direction == Direction::Right;

    quintptr best = 0;
    std::tuple<int, qint64, int> bestScore;
    for (auto it = m_tiles.cbegin(); it != m_tiles.cend(); ++it) {
        const quintptr w = it.key();
        if (w == exclude || !m_managed.contains(w) || win::isMinimized(w))
            continue;
        const Rect &r = it.value();
        int edge = 0;
        bool beyond = false;
        switch (direction) {
        case Direction::Left:
            beyond = centerX(r) < centerX(from);
            edge = from.left - r.right;
            break;
        case Direction::Right:
            beyond = centerX(r) > centerX(from);
            edge = r.left - from.right;
            break;
        case Direction::Up:
            beyond = centerY(r) < centerY(from);
            edge = from.top - r.bottom;
            break;
        case Direction::Down:
            beyond = centerY(r) > centerY(from);
            edge = r.top - from.bottom;
            break;
        }
        if (!beyond)
            continue;
        const int overlap = horizontal ? std::min(from.bottom, r.bottom) - std::max(from.top, r.top)
                                       : std::min(from.right, r.right) - std::max(from.left, r.left);
        if (overlap <= 0)
            continue;
        const int misalignment = horizontal ? std::abs(centerY(r) - centerY(from)) : std::abs(centerX(r) - centerX(from));
        const std::tuple<int, qint64, int> score{std::max(edge, 0), -qint64(m_focusedAt.value(w)), misalignment};
        if (!best || score < bestScore) {
            best = w;
            bestScore = score;
        }
    }
    return best;
}

} // namespace visor::wm
