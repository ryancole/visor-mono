#include "wm/manager.h"

#include "common/launch.h"
#include "common/linkprotocol.h"
#include "common/log.h"
#include "wm/windows.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include <windows.h>
#include <shellscalingapi.h>

#include <algorithm>
#include <cstdlib>
#include <tuple>
#include <utility>

namespace visor::wm {

namespace {

constexpr int kSettleMs = 300;
constexpr int kRecolorMs = 150;
constexpr int kFocusAfterSwitchMs = 50; // let the shown windows appear first
constexpr UINT kSendTimeoutMs = 500;
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
        wc.lpszClassName = link::kWmClass;
        return RegisterClassExW(&wc);
    }();
    return atom ? link::kWmClass : nullptr;
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

// The desktops and the windows hidden on them, so a later visor-wm can pick
// them up if this one dies or is replaced:
//   desktops <count> <current index>
//   <hwnd> <pid> <desktop index>      one per hidden window
QString stateFile()
{
    return QDir(dataDir()).filePath(QStringLiteral("wm-hidden.txt"));
}

// `tile`, grown to at least `minimum` and kept inside `area`. It grows away
// from the middle of the area (a left-hand tile grows rightwards), so it
// covers its neighbour rather than leaving the screen.
Rect fit(const Rect &tile, const QSize &minimum, const Rect &area)
{
    Rect r = tile;
    if (minimum.width() > r.width()) {
        if ((tile.left + tile.right) / 2 <= (area.left + area.right) / 2)
            r.right = r.left + minimum.width();
        else
            r.left = r.right - minimum.width();
    }
    if (minimum.height() > r.height()) {
        if ((tile.top + tile.bottom) / 2 <= (area.top + area.bottom) / 2)
            r.bottom = r.top + minimum.height();
        else
            r.top = r.bottom - minimum.height();
    }
    const auto shift = [](int &lo, int &hi, int min, int max) {
        if (hi > max) {
            lo -= hi - max;
            hi = max;
        }
        if (lo < min) {
            hi += min - lo;
            lo = min;
        }
    };
    shift(r.left, r.right, area.left, area.right);
    shift(r.top, r.bottom, area.top, area.bottom);
    return r;
}

double monitorScale(HMONITOR monitor)
{
    UINT dpiX = 96, dpiY = 96;
    if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)))
        return 1.0;
    return dpiX / 96.0;
}

} // namespace

WindowManager::WindowManager(Config config, QObject *parent)
    : QObject(parent)
    , m_config(std::move(config))
{
    g_instance = this;
    m_desktops.push_back(std::make_unique<Desktop>());

    m_settleTimer.setSingleShot(true);
    m_settleTimer.setInterval(kSettleMs);
    connect(&m_settleTimer, &QTimer::timeout, this, &WindowManager::arrangeAll);
    // Re-arranges only when it learnt something, so it can't loop.
    m_learnTimer.setSingleShot(true);
    m_learnTimer.setInterval(kSettleMs);
    connect(&m_learnTimer, &QTimer::timeout, this, [this] {
        if (learnMinimumSizes())
            arrangeAll();
    });
    m_recolorTimer.setSingleShot(true);
    m_recolorTimer.setInterval(kRecolorMs);
    connect(&m_recolorTimer, &QTimer::timeout, this, [this] {
        if (m_previousActive && m_previousActive != m_active && m_colored.contains(m_previousActive)
            && win::exists(m_previousActive))
            colorBorder(m_previousActive, false);
        if (m_active && m_colored.contains(m_active) && win::exists(m_active))
            colorBorder(m_active, true);
    });
    m_stateTimer.setSingleShot(true);
    m_stateTimer.setInterval(0);
    connect(&m_stateTimer, &QTimer::timeout, this, &WindowManager::sendState);

    // Top-level (hidden): WM_DISPLAYCHANGE and WM_SETTINGCHANGE are only
    // broadcast to top-level windows, not message-only ones, and the shell
    // finds it by class.
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

    // Desktops and hidden windows a previous visor-wm left behind.
    restoreState();

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
    focusChanged(win::foreground());
    qInfo() << "tiling" << m_managed.size() << "windows on" << m_monitors.size() << "monitors";
    registerBindings();
    sendState();
    sendBindings();
}

WindowManager::~WindowManager()
{
    g_instance = nullptr;
    unregisterBindings();
    for (void *hook : std::as_const(m_hooks))
        UnhookWinEvent(static_cast<HWINEVENTHOOK>(hook));
    if (m_handOver) {
        saveState(); // for the next visor-wm
    } else {
        // Nothing may stay hidden once we're gone.
        for (auto it = m_hidden.cbegin(); it != m_hidden.cend(); ++it) {
            if (win::exists(it.key()))
                win::show(it.key());
        }
        QFile::remove(stateFile());
    }
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
    sendBindings();
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
        if (m_expectShow.remove(hwnd)) {
            // We showed it (desktop switch). Windows a previous visor-wm hid
            // aren't in a layout yet.
            if (!m_managed.contains(hwnd))
                consider(hwnd);
            break;
        }
        if (Desktop *desktop = m_hidden.take(hwnd)) {
            // The app showed a window we hid on another desktop (e.g. from
            // its tray icon). Windows switches to a window's desktop when it
            // is activated; do the same.
            qInfo().noquote() << "hidden window shown by its app:" << win::exeName(hwnd) << win::className(hwnd)
                              << QLatin1Char('"') + win::title(hwnd) + QLatin1Char('"');
            saveState();
            activateDesktop(indexOf(desktop));
            break;
        }
        consider(hwnd);
        break;
    case EVENT_OBJECT_UNCLOAKED:
    case EVENT_SYSTEM_MINIMIZEEND:
        consider(hwnd);
        break;
    case EVENT_OBJECT_DESTROY:
        m_colored.remove(hwnd);
        m_tileOverride.remove(hwnd);
        m_floatFullscreen.remove(hwnd);
        m_minimumSize.remove(hwnd);
        m_placed.remove(hwnd);
        m_expectHide.remove(hwnd);
        m_expectShow.remove(hwnd);
        if (m_hidden.remove(hwnd))
            saveState();
        if (hwnd == m_active)
            m_active = 0;
        if (hwnd == m_previousActive)
            m_previousActive = 0;
        m_focusedAt.remove(hwnd);
        if (m_managed.contains(hwnd))
            unmanage(hwnd);
        untrack(hwnd);
        break;
    case EVENT_OBJECT_HIDE:
        if (m_expectHide.remove(hwnd) || m_hidden.contains(hwnd))
            break; // we hid it (desktop switch): it stays on its desktop
        // The app hid it (closed to the tray, say): forget it until it's back.
        if (m_managed.contains(hwnd))
            unmanage(hwnd);
        untrack(hwnd);
        break;
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
            if (!held && it->desktop == current())
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
    if (msg == WM_COPYDATA) {
        // A request from Visor, forwarded by visor-shell (linkprotocol.h).
        const auto *cds = reinterpret_cast<const COPYDATASTRUCT *>(lParam);
        if (!cds || cds->dwData != link::kLinkMagic)
            return FALSE;
        const QJsonObject m =
            QJsonDocument::fromJson(QByteArray(static_cast<const char *>(cds->lpData), int(cds->cbData))).object();
        const QString type = m.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("quit")) {
            // visor-shell is handing the session to Explorer: show everything.
            QMetaObject::invokeMethod(QCoreApplication::instance(), &QCoreApplication::quit, Qt::QueuedConnection);
        } else if (type == QLatin1String("workspace.activate")) {
            const int index = m.value(QStringLiteral("index")).toInt();
            // From the event loop, not inside the shell's SendMessage.
            QMetaObject::invokeMethod(this, [this, index] { activateDesktop(index); }, Qt::QueuedConnection);
        }
        return TRUE;
    }
    if (msg == WM_DISPLAYCHANGE || msg == WM_DPICHANGED || (msg == WM_SETTINGCHANGE && wParam == SPI_SETWORKAREA)) {
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
                    name, {toRect(info.rcMonitor), toRect(info.rcWork), monitorScale(monitor),
                           (info.dwFlags & MONITORINFOF_PRIMARY) != 0});
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&monitors));
    if (monitors.isEmpty())
        return; // mid display change; keep what we had
    m_monitors = monitors;

    // Windows on a monitor that went away move to the primary one, on
    // whichever desktop they're on.
    const QString primary = primaryMonitor();
    const Rect area = m_monitors.value(primary).work;
    for (const auto &desktop : m_desktops) {
        auto &spaces = desktop->monitors;
        for (auto it = spaces.begin(); it != spaces.end();) {
            if (m_monitors.contains(it->first)) {
                ++it;
                continue;
            }
            const QList<quintptr> orphans = it->second.layout.windows();
            it = spaces.erase(it);
            Workspace &target = spaces[primary];
            for (quintptr w : orphans) {
                target.layout.insert(w, 0, area, m_config.dwindle);
                m_managed[w].monitor = primary;
            }
            qInfo() << "monitor removed; moved" << orphans.size() << "windows to" << primary;
        }
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

int WindowManager::indexOf(const Desktop *desktop) const
{
    for (size_t i = 0; i < m_desktops.size(); ++i) {
        if (m_desktops[i].get() == desktop)
            return int(i);
    }
    return -1;
}

void WindowManager::consider(quintptr hwnd)
{
    QString reason;
    win::Kind kind = win::classify(hwnd, m_config, &reason);
    // togglefloating beats rules and heuristics, for as long as the window lives.
    if (kind != win::Kind::Ignore) {
        if (const auto it = m_tileOverride.constFind(hwnd); it != m_tileOverride.cend())
            kind = *it ? win::Kind::Tile : win::Kind::Float;
        track(hwnd);
    }
    const bool managed = m_managed.contains(hwnd);
    if (!managed && kind == win::Kind::Tile)
        manage(hwnd);
    // A tiled window that turns maximised, fullscreen or topmost keeps its
    // tile; only one that stops being an app window at all is dropped.
    else if (managed && kind == win::Kind::Ignore)
        unmanage(hwnd);
}

void WindowManager::track(quintptr hwnd)
{
    if (m_desktopOf.contains(hwnd))
        return;
    // A dialog belongs to its owner's desktop; anything else opens on the
    // current one, as in Windows.
    Desktop *desktop = m_desktopOf.value(win::rootOwner(hwnd), current());
    m_desktopOf.insert(hwnd, desktop);
    if (!win::owner(hwnd))
        m_stateTimer.start(); // window counts changed
}

void WindowManager::untrack(quintptr hwnd)
{
    if (m_desktopOf.remove(hwnd))
        m_stateTimer.start();
}

void WindowManager::manage(quintptr hwnd)
{
    Desktop *desktop = m_desktopOf.value(hwnd, current());
    const QString monitor = monitorOf(hwnd);
    Workspace &ws = desktop->monitors[monitor];
    // Hyprland splits the focused window; that's still the previous one when
    // the new window's show event arrives.
    const quintptr target = ws.layout.contains(ws.lastFocused) ? ws.lastFocused : 0;
    POINT cursor{};
    GetCursorPos(&cursor);

    if (win::isMaximized(hwnd))
        win::unmaximize(hwnd);
    ws.layout.insert(hwnd, target, m_monitors.value(monitor).work, m_config.dwindle, cursor.x, cursor.y);
    m_managed.insert(hwnd, {desktop, monitor, false, false});
    colorBorder(hwnd, hwnd == m_active);
    qInfo().noquote() << "tile" << win::exeName(hwnd) << win::className(hwnd)
                      << QLatin1Char('"') + win::title(hwnd) + QLatin1Char('"') << "on" << monitor << "desktop"
                      << indexOf(desktop) + 1;
    if (desktop == current()) {
        arrange(monitor);
        settleSoon();
    }
}

void WindowManager::unmanage(quintptr hwnd)
{
    const Managed m = m_managed.take(hwnd);
    m_tiles.remove(hwnd);
    m_placed.remove(hwnd);
    Workspace &ws = workspaceOf(m);
    ws.layout.remove(hwnd);
    if (ws.lastFocused == hwnd)
        ws.lastFocused = 0;
    if (m.desktop == current())
        arrange(m.monitor);
}

void WindowManager::moveToMonitor(quintptr hwnd, const QString &monitor)
{
    Managed &m = m_managed[hwnd];
    const QString from = m.monitor;
    Workspace &old = workspaceOf(m);
    old.layout.remove(hwnd);
    if (old.lastFocused == hwnd)
        old.lastFocused = 0;

    Workspace &ws = m.desktop->monitors[monitor];
    const quintptr target = ws.layout.contains(ws.lastFocused) ? ws.lastFocused : 0;
    POINT cursor{};
    GetCursorPos(&cursor);
    ws.layout.insert(hwnd, target, m_monitors.value(monitor).work, m_config.dwindle, cursor.x, cursor.y);
    m.monitor = monitor;
    arrange(from);
    arrange(monitor);
}

QString WindowManager::monitorInDirection(const QString &monitor, Direction direction) const
{
    // The nearest monitor whose edge faces ours and which shares some of
    // its height (left/right) or width (up/down).
    const Rect from = m_monitors.value(monitor).full;
    QString best;
    int bestDistance = 0;
    for (auto it = m_monitors.cbegin(); it != m_monitors.cend(); ++it) {
        if (it.key() == monitor)
            continue;
        const Rect &r = it->full;
        const bool horizontal = direction == Direction::Left || direction == Direction::Right;
        const int overlap = horizontal ? std::min(from.bottom, r.bottom) - std::max(from.top, r.top)
                                       : std::min(from.right, r.right) - std::max(from.left, r.left);
        int distance = -1;
        switch (direction) {
        case Direction::Left:
            distance = r.right <= from.left ? from.left - r.right : -1;
            break;
        case Direction::Right:
            distance = r.left >= from.right ? r.left - from.right : -1;
            break;
        case Direction::Up:
            distance = r.bottom <= from.top ? from.top - r.bottom : -1;
            break;
        case Direction::Down:
            distance = r.top >= from.bottom ? r.top - from.bottom : -1;
            break;
        }
        if (distance < 0 || overlap <= 0)
            continue;
        if (best.isEmpty() || distance < bestDistance) {
            best = it.key();
            bestDistance = distance;
        }
    }
    return best;
}

void WindowManager::arrange(const QString &monitor)
{
    const auto mon = m_monitors.constFind(monitor);
    if (mon == m_monitors.cend())
        return;
    // Gaps are logical pixels, like Hyprland's: bigger on scaled monitors.
    const int gapsIn = int(std::lround(m_config.gapsIn * mon->scale));
    const int gapsOut = int(std::lround(m_config.gapsOut * mon->scale));
    const Rect inside{mon->work.left + gapsOut, mon->work.top + gapsOut, mon->work.right - gapsOut,
                      mon->work.bottom - gapsOut};
    const QList<DwindleLayout::Placement> placements =
        workspace(monitor).layout.arrange(mon->work, gapsIn, gapsOut, m_config.dwindle);
    for (const DwindleLayout::Placement &p : placements) {
        m_tiles.insert(p.window, p.rect);
        if (!win::exists(p.window) || win::isMinimized(p.window) || win::isMaximized(p.window))
            continue;
        if (m_managed.value(p.window).fullscreen) {
            win::moveTo(p.window, mon->full);
        } else if (!win::isFullscreen(p.window)) {
            const Rect target = fit(p.rect, m_minimumSize.value(p.window), inside);
            m_placed.insert(p.window, target);
            win::moveTo(p.window, target);
        }
    }
    m_learnTimer.start();
}

bool WindowManager::learnMinimumSizes()
{
    bool learnt = false;
    for (auto it = m_placed.cbegin(); it != m_placed.cend(); ++it) {
        const quintptr w = it.key();
        const auto managed = m_managed.constFind(w);
        if (managed == m_managed.cend() || managed->desktop != current() || managed->fullscreen
            || !win::isVisible(w) || win::isMinimized(w) || win::isMaximized(w))
            continue;
        const Rect frame = win::frameRect(w);
        const Rect &asked = it.value();
        QSize minimum = m_minimumSize.value(w, QSize(0, 0));
        // A pixel or two of rounding isn't a minimum size.
        if (frame.width() > asked.width() + 2 && frame.width() > minimum.width())
            minimum.setWidth(frame.width());
        if (frame.height() > asked.height() + 2 && frame.height() > minimum.height())
            minimum.setHeight(frame.height());
        if (minimum != m_minimumSize.value(w, QSize(0, 0))) {
            m_minimumSize.insert(w, minimum);
            qInfo().noquote() << win::exeName(w) << "won't go below" << minimum.width() << "x" << minimum.height();
            learnt = true;
        }
    }
    return learnt;
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
    if (Desktop *desktop = m_desktopOf.value(hwnd))
        desktop->lastFocused = hwnd;
    if (const auto it = m_managed.constFind(hwnd); it != m_managed.cend())
        workspaceOf(*it).lastFocused = hwnd;
}

void WindowManager::colorBorder(quintptr hwnd, bool active)
{
    if (m_config.borderSize <= 0)
        return;
    win::setBorderColor(hwnd, active ? m_config.activeBorder : m_config.inactiveBorder);
    m_colored.insert(hwnd);
}

// ---- Desktops -------------------------------------------------------------

void WindowManager::activateDesktop(int index)
{
    if (index < 0 || index >= int(m_desktops.size()) || index == m_current)
        return;
    Desktop *from = current();
    Desktop *to = m_desktops[size_t(index)].get();

    QSet<quintptr> leaving;
    for (auto it = m_desktopOf.cbegin(); it != m_desktopOf.cend(); ++it) {
        if (it.value() == from && win::isVisible(it.key()))
            leaving.insert(it.key());
    }
    hideWindows(leaving, from);
    m_current = index;
    showWindows(to);

    m_tiles.clear(); // the other desktop's tiles
    arrangeAll();
    settleSoon();
    focusDesktop(to);
    m_stateTimer.start();
    qInfo() << "desktop" << index + 1 << "of" << m_desktops.size();
}

void WindowManager::newDesktop()
{
    // Like Win+Ctrl+D: add one at the end and go there.
    m_desktops.push_back(std::make_unique<Desktop>());
    activateDesktop(int(m_desktops.size()) - 1);
}

void WindowManager::closeDesktop()
{
    // Like Win+Ctrl+F4: the windows move to the desktop on the left (on the
    // right when closing the first) and that desktop is shown.
    if (m_desktops.size() < 2)
        return;
    Desktop *closing = current();
    const int destIndex = m_current > 0 ? m_current - 1 : 1;
    Desktop *dest = m_desktops[size_t(destIndex)].get();

    for (auto it = m_managed.begin(); it != m_managed.end(); ++it) {
        if (it->desktop != closing)
            continue;
        Workspace &ws = dest->monitors[it->monitor];
        const quintptr target = ws.layout.contains(ws.lastFocused) ? ws.lastFocused : 0;
        ws.layout.insert(it.key(), target, m_monitors.value(it->monitor).work, m_config.dwindle);
        it->desktop = dest;
    }
    for (auto it = m_desktopOf.begin(); it != m_desktopOf.end(); ++it) {
        if (it.value() == closing)
            it.value() = dest;
    }
    for (auto it = m_hidden.begin(); it != m_hidden.end(); ++it) {
        if (it.value() == closing)
            it.value() = dest;
    }
    if (!dest->lastFocused)
        dest->lastFocused = closing->lastFocused;

    m_desktops.erase(m_desktops.begin() + m_current);
    m_current = indexOf(dest);
    showWindows(dest); // closing's windows are on screen already
    m_tiles.clear();
    arrangeAll();
    settleSoon();
    focusDesktop(dest);
    m_stateTimer.start();
    qInfo() << "closed a desktop; now on desktop" << m_current + 1 << "of" << m_desktops.size();
}

void WindowManager::moveToDesktop(quintptr hwnd, int index, bool follow)
{
    if (index < 0 || index >= int(m_desktops.size()) || index == m_current || !m_desktopOf.contains(hwnd))
        return;
    // A dialog moves with its owner.
    hwnd = win::rootOwner(hwnd);
    if (!m_desktopOf.contains(hwnd))
        return;
    Desktop *dest = m_desktops[size_t(index)].get();

    if (const auto it = m_managed.find(hwnd); it != m_managed.end()) {
        Workspace &old = workspaceOf(*it);
        old.layout.remove(hwnd);
        if (old.lastFocused == hwnd)
            old.lastFocused = 0;
        Workspace &ws = dest->monitors[it->monitor];
        const quintptr target = ws.layout.contains(ws.lastFocused) ? ws.lastFocused : 0;
        ws.layout.insert(hwnd, target, m_monitors.value(it->monitor).work, m_config.dwindle);
        it->desktop = dest;
        m_tiles.remove(hwnd);
    }
    for (auto it = m_desktopOf.begin(); it != m_desktopOf.end(); ++it) {
        if (it.key() == hwnd || win::rootOwner(it.key()) == hwnd)
            it.value() = dest; // with its dialogs
    }
    if (current()->lastFocused == hwnd)
        current()->lastFocused = 0;
    dest->lastFocused = hwnd;

    if (follow) {
        activateDesktop(index); // hides the rest, shows hwnd's new desktop
        return;
    }
    hideWindows({hwnd}, dest);
    arrangeAll();
    focusDesktop(current());
    m_stateTimer.start();
}

int WindowManager::desktopIndex(const QString &argument) const
{
    const QString arg = argument.trimmed().toLower();
    // No wrapping at the ends, as in Windows.
    if (arg == QLatin1String("e+1") || arg == QLatin1String("+1"))
        return m_current + 1 < int(m_desktops.size()) ? m_current + 1 : -1;
    if (arg == QLatin1String("e-1") || arg == QLatin1String("-1"))
        return m_current - 1;
    bool ok = false;
    const int n = arg.toInt(&ok);
    return ok && n >= 1 && n <= int(m_desktops.size()) ? n - 1 : -1;
}

void WindowManager::hideWindows(const QSet<quintptr> &roots, Desktop *desktop)
{
    if (roots.isEmpty())
        return;
    // Take focus off them first. Hiding the focused window makes Windows
    // activate the next one, which may be another we're hiding, and some
    // apps (Windows Terminal) show their window again when it's activated.
    const quintptr focused = win::foreground();
    if (focused && (roots.contains(focused) || roots.contains(win::rootOwner(focused)))) {
        if (const quintptr desk = win::shellWindow())
            win::focus(desk);
    }
    // Their dialogs and other owned windows go too (hiding an owner doesn't
    // hide what it owns), but only ones with a size: 0x0 owned windows are
    // plumbing, and hiding some breaks their app. A console's
    // PseudoConsoleWindow, for one, passes visibility on to Windows Terminal,
    // which then hides and re-shows its own window.
    QSet<quintptr> windows = roots;
    struct Search
    {
        const QSet<quintptr> *roots;
        QSet<quintptr> *found;
    } search{&roots, &windows};
    EnumWindows(
        [](HWND w, LPARAM param) -> BOOL {
            auto *s = reinterpret_cast<Search *>(param);
            const auto window = reinterpret_cast<quintptr>(w);
            if (win::isVisible(window) && s->roots->contains(win::rootOwner(window))) {
                const Rect r = win::frameRect(window);
                if (r.width() > 0 && r.height() > 0)
                    s->found->insert(window);
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));

    for (quintptr w : std::as_const(windows)) {
        m_hidden.insert(w, desktop);
        m_expectHide.insert(w);
        m_expectShow.remove(w);
        win::hide(w);
    }
    saveState();
}

void WindowManager::showWindows(Desktop *desktop)
{
    for (auto it = m_hidden.begin(); it != m_hidden.end();) {
        if (it.value() != desktop) {
            ++it;
            continue;
        }
        const quintptr w = it.key();
        it = m_hidden.erase(it);
        if (!win::exists(w))
            continue;
        m_expectShow.insert(w);
        m_expectHide.remove(w);
        win::show(w);
    }
    saveState();
}

void WindowManager::focusDesktop(Desktop *desktop)
{
    // Its last focused window, or else the desktop itself, so keyboard focus
    // doesn't stay on a window that is now hidden. After a moment: the shown
    // windows are shown asynchronously.
    QTimer::singleShot(kFocusAfterSwitchMs, this, [this, desktop] {
        if (indexOf(desktop) < 0)
            return;
        quintptr target = desktop->lastFocused;
        if (!target || m_desktopOf.value(target) != desktop || !win::isVisible(target)) {
            target = 0;
            for (auto it = m_desktopOf.cbegin(); it != m_desktopOf.cend(); ++it) {
                if (it.value() == desktop && win::isVisible(it.key()) && !win::isMinimized(it.key())
                    && !win::owner(it.key())) {
                    target = it.key();
                    break;
                }
            }
        }
        if (!target)
            target = win::shellWindow();
        if (target)
            win::focus(target);
    });
}

void WindowManager::saveState() const
{
    if (m_hidden.isEmpty() && m_desktops.size() < 2) {
        QFile::remove(stateFile());
        return;
    }
    QFile file(stateFile());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning() << "cannot write" << file.fileName() << file.errorString();
        return;
    }
    file.write("desktops " + QByteArray::number(qulonglong(m_desktops.size())) + ' ' + QByteArray::number(m_current)
               + '\n');
    for (auto it = m_hidden.cbegin(); it != m_hidden.cend(); ++it) {
        file.write(QByteArray::number(qulonglong(it.key())) + ' ' + QByteArray::number(win::processId(it.key())) + ' '
                   + QByteArray::number(indexOf(it.value())) + '\n');
    }
}

void WindowManager::restoreState()
{
    QFile file(stateFile());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    int count = 1;
    int active = 0;
    struct Entry
    {
        quintptr hwnd;
        quint32 pid;
        int desktop;
    };
    QList<Entry> entries;
    while (!file.atEnd()) {
        const QList<QByteArray> f = file.readLine().trimmed().split(' ');
        if (f.size() == 3 && f[0] == "desktops") {
            count = std::clamp(f[1].toInt(), 1, 100);
            active = f[2].toInt();
        } else if (f.size() >= 2) {
            // (Older files had no desktop column: those windows just come back.)
            entries.append({quintptr(f[0].toULongLong()), quint32(f[1].toULong()), f.size() > 2 ? f[2].toInt() : -1});
        }
    }
    file.close();

    m_desktops.clear();
    for (int i = 0; i < count; ++i)
        m_desktops.push_back(std::make_unique<Desktop>());
    m_current = std::clamp(active, 0, count - 1);

    int kept = 0;
    int shown = 0;
    for (const Entry &e : std::as_const(entries)) {
        // The same window (not a reused handle), still hidden.
        if (!win::exists(e.hwnd) || win::processId(e.hwnd) != e.pid || win::isVisible(e.hwnd))
            continue;
        if (e.desktop < 0 || e.desktop >= count || e.desktop == m_current) {
            win::show(e.hwnd); // adopted when its show event arrives
            ++shown;
            continue;
        }
        Desktop *desktop = m_desktops[size_t(e.desktop)].get();
        m_hidden.insert(e.hwnd, desktop);
        m_desktopOf.insert(e.hwnd, desktop); // tiled when its desktop is shown
        ++kept;
    }
    saveState();
    qInfo() << "picked up" << count << "desktops (on" << m_current + 1 << ")," << kept << "hidden windows;" << shown
            << "shown";
}

QJsonObject WindowManager::desktopState() const
{
    QJsonArray desktops;
    for (size_t i = 0; i < m_desktops.size(); ++i) {
        int windows = 0;
        for (auto it = m_desktopOf.cbegin(); it != m_desktopOf.cend(); ++it) {
            if (it.value() == m_desktops[i].get() && !win::owner(it.key()))
                ++windows;
        }
        desktops.append(QJsonObject{
            {QStringLiteral("name"), QStringLiteral("Desktop %1").arg(i + 1)},
            {QStringLiteral("windows"), windows},
        });
    }
    return {
        {QStringLiteral("type"), QStringLiteral("workspaces")},
        {QStringLiteral("workspaces"), desktops},
        {QStringLiteral("active"), m_current},
        {QStringLiteral("pid"), qint64(QCoreApplication::applicationPid())},
    };
}

void WindowManager::sendState()
{
    sendToShell(desktopState());
}

void WindowManager::sendBindings()
{
    QJsonArray bindings;
    for (const Binding &b : std::as_const(m_config.bindings)) {
        bindings.append(QJsonObject{
            {QStringLiteral("keys"), b.name},
            {QStringLiteral("description"), b.description},
            {QStringLiteral("dispatcher"), b.dispatcher},
            {QStringLiteral("argument"), b.argument},
            {QStringLiteral("group"), b.group},
        });
    }
    sendToShell({{QStringLiteral("type"), QStringLiteral("bindings")}, {QStringLiteral("bindings"), bindings}});
}

void WindowManager::sendToShell(const QJsonObject &message)
{
    // To visor-shell, which passes it on to Visor (linkprotocol.h).
    const HWND shell = FindWindowW(link::kShellLinkClass, nullptr);
    if (!shell)
        return;
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    COPYDATASTRUCT cds{};
    cds.dwData = link::kLinkMagic;
    cds.cbData = DWORD(payload.size());
    cds.lpData = const_cast<char *>(payload.constData());
    DWORD_PTR result = 0;
    SendMessageTimeoutW(shell, WM_COPYDATA, WPARAM(m_hwnd), LPARAM(&cds), SMTO_ABORTIFHUNG | SMTO_BLOCK,
                        kSendTimeoutMs, &result);
}

// ---- Key bindings -----------------------------------------------------------

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
        // Release bindings only exist in the hook (hotkeys fire on press).
        if (!b.release && RegisterHotKey(hwnd, id, b.modifiers | (b.repeat ? 0 : MOD_NOREPEAT), b.key))
            continue;
        if (!b.release && GetLastError() != ERROR_HOTKEY_ALREADY_REGISTERED)
            qWarning().noquote() << "cannot bind" << b.name << "error" << GetLastError();
        hooked.append({b.modifiers, b.key, b.repeat, b.release, id});
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
    } else if (d == QLatin1String("visor")) {
        // Visor opens a window that takes typing (the launcher): let it come
        // to the front, since the key press gave us the foreground rights.
        AllowSetForegroundWindow(ASFW_ANY);
        sendToShell({{QStringLiteral("type"), QStringLiteral("visor.command")},
                     {QStringLiteral("name"), binding.argument.trimmed().toLower()}});
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
    } else if (d == QLatin1String("workspace")) {
        if (arg == QLatin1String("new"))
            newDesktop();
        else
            activateDesktop(desktopIndex(arg));
    } else if (d == QLatin1String("movetoworkspace") || d == QLatin1String("movetoworkspacesilent")) {
        const quintptr hwnd = win::foreground();
        if (!m_desktopOf.contains(hwnd))
            return;
        int index = desktopIndex(arg);
        if (arg == QLatin1String("new")) {
            m_desktops.push_back(std::make_unique<Desktop>());
            index = int(m_desktops.size()) - 1;
        }
        moveToDesktop(hwnd, index, d == QLatin1String("movetoworkspace"));
    } else if (d == QLatin1String("closeworkspace")) {
        closeDesktop();
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
    if (!hwnd || win::classify(hwnd, m_config) == win::Kind::Ignore)
        return;
    const auto a = m_managed.find(hwnd);
    const quintptr target = a == m_managed.end() ? 0 : neighbor(m_tiles.value(hwnd), direction, hwnd);
    const auto b = m_managed.find(target);
    if (!target || b == m_managed.end()) {
        // Nothing to swap with that way: move it to the monitor there, as
        // Win+Shift+Left/Right does in Windows (floating windows too).
        const QString from = a == m_managed.end() ? monitorOf(hwnd) : a->monitor;
        const QString to = monitorInDirection(from, direction);
        if (to.isEmpty())
            return;
        if (a != m_managed.end()) {
            moveToMonitor(hwnd, to);
            workspace(to).lastFocused = hwnd;
        } else {
            // Same place relative to the work area, made to fit.
            const Rect src = m_monitors.value(from).work;
            const Rect dst = m_monitors.value(to).work;
            const Rect f = win::frameRect(hwnd);
            const int w = std::min(f.width(), dst.width());
            const int h = std::min(f.height(), dst.height());
            const int left = dst.left + int(qint64(f.left - src.left) * (dst.width() - w) / std::max(1, src.width() - f.width()));
            const int top = dst.top + int(qint64(f.top - src.top) * (dst.height() - h) / std::max(1, src.height() - f.height()));
            win::moveTo(hwnd, fit({left, top, left + w, top + h}, {}, dst));
        }
        settleSoon(); // apps resize themselves when they change monitor DPI
        return;
    }

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
        workspaceOf(*it).layout.toggleSplit(hwnd);
        arrange(it->monitor);
    }
}

void WindowManager::resizeActive(int dx, int dy)
{
    const quintptr hwnd = win::foreground();
    if (const auto it = m_managed.constFind(hwnd); it != m_managed.cend()) {
        // Don't shrink the tile below what the window will go to: the window
        // wouldn't follow, and growing back would take extra presses.
        const QSize minimum = m_minimumSize.value(hwnd, QSize(0, 0));
        const Rect tile = m_tiles.value(hwnd);
        if (dx < 0 && minimum.width() > 0)
            dx = std::min(0, std::max(dx, minimum.width() - tile.width()));
        if (dy < 0 && minimum.height() > 0)
            dy = std::min(0, std::max(dy, minimum.height() - tile.height()));
        if (!dx && !dy)
            return;
        if (workspaceOf(*it).layout.resize(hwnd, dx, dy))
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
        const auto managed = m_managed.constFind(w);
        if (w == exclude || managed == m_managed.cend() || managed->desktop != current() || win::isMinimized(w))
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
