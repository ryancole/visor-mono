#include "panelwindow.h"

#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <QtGui/qguiapplication_platform.h>
#include <QtGui/qscreen_platform.h>

#include <windows.h>
#include <shellapi.h>

namespace {

const UINT kAppBarCallback = RegisterWindowMessageW(L"visor.AppBarCallback");
// Broadcast when Explorer (re)starts; app bars must re-register after it.
const UINT kTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

UINT toAbEdge(PanelWindow::Edge edge)
{
    switch (edge) {
    case PanelWindow::Top: return ABE_TOP;
    case PanelWindow::Bottom: return ABE_BOTTOM;
    case PanelWindow::Left: return ABE_LEFT;
    case PanelWindow::Right: return ABE_RIGHT;
    }
    return ABE_TOP;
}

// Shrink `rc` to a strip of `px` pixels along `edge`.
void clampToEdge(RECT &rc, PanelWindow::Edge edge, int px)
{
    switch (edge) {
    case PanelWindow::Top: rc.bottom = rc.top + px; break;
    case PanelWindow::Bottom: rc.top = rc.bottom - px; break;
    case PanelWindow::Left: rc.right = rc.left + px; break;
    case PanelWindow::Right: rc.left = rc.right - px; break;
    }
}

HMONITOR monitorFor(QScreen *screen)
{
    if (auto *native = screen->nativeInterface<QNativeInterface::QWindowsScreen>())
        return native->handle();
    return nullptr;
}

} // namespace

PanelWindow::PanelWindow(QWindow *parent)
    : QQuickWindow(parent)
{
    // Tool: no taskbar button / Alt+Tab entry. DoesNotAcceptFocus: clicking the
    // bar never steals focus from the app you're using.
    setFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    setColor(Qt::black);

    connect(qGuiApp, &QGuiApplication::screenAdded, this, &PanelWindow::scheduleLayout);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &PanelWindow::scheduleLayout);
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, &PanelWindow::scheduleLayout);
}

PanelWindow::~PanelWindow()
{
    unregisterAppBar();
}

void PanelWindow::componentComplete()
{
    m_complete = true;
    if (!m_wantVisible)
        return;
    // Dock before the first show so the window never appears at a default
    // position/size.
    create();
    registerAppBar();
    layout();
    show();
}

void PanelWindow::setPanelVisible(bool visible)
{
    if (m_wantVisible == visible)
        return;
    m_wantVisible = visible;
    if (m_complete)
        QQuickWindow::setVisible(visible);
    emit panelVisibleChanged();
}

void PanelWindow::setEdge(Edge edge)
{
    if (m_edge == edge)
        return;
    m_edge = edge;
    scheduleLayout();
    emit edgeChanged();
}

void PanelWindow::setThickness(int thickness)
{
    thickness = qMax(1, thickness);
    if (m_thickness == thickness)
        return;
    m_thickness = thickness;
    scheduleLayout();
    emit thicknessChanged();
}

void PanelWindow::setExclusive(bool exclusive)
{
    if (m_exclusive == exclusive)
        return;
    m_exclusive = exclusive;
    if (m_appBarRegistered) {
        // Re-register so a previously reserved area is released.
        unregisterAppBar();
        registerAppBar();
    }
    scheduleLayout();
    emit exclusiveChanged();
}

void PanelWindow::setHideOnFullscreen(bool hide)
{
    if (m_hideOnFullscreen == hide)
        return;
    m_hideOnFullscreen = hide;
    applyZOrder();
    emit hideOnFullscreenChanged();
}

void PanelWindow::setScreenObject(QObject *screen)
{
    if (m_screenObject == screen)
        return;
    m_screenObject = screen;
    scheduleLayout();
    emit screenObjectChanged();
}

QScreen *PanelWindow::targetScreen() const
{
    if (m_screenObject) {
        if (auto *screen = qobject_cast<QScreen *>(m_screenObject.data()))
            return screen;
        const QString name = m_screenObject->property("name").toString();
        const auto screens = QGuiApplication::screens();
        for (QScreen *screen : screens) {
            if (screen->name() == name)
                return screen;
        }
    }
    return QGuiApplication::primaryScreen();
}

void PanelWindow::scheduleLayout()
{
    if (m_layoutPending)
        return;
    m_layoutPending = true;
    QTimer::singleShot(0, this, [this] {
        m_layoutPending = false;
        layout();
    });
}

void PanelWindow::layout()
{
    if (!m_complete || !handle() || (!isVisible() && !m_wantVisible))
        return;

    QScreen *screen = targetScreen();
    if (!screen)
        return;

    if (m_trackedScreen != screen) {
        if (m_trackedScreen)
            disconnect(m_trackedScreen, nullptr, this, nullptr);
        m_trackedScreen = screen;
        connect(screen, &QScreen::geometryChanged, this, &PanelWindow::scheduleLayout);
        connect(screen, &QScreen::physicalDotsPerInchChanged, this, &PanelWindow::scheduleLayout);
    }
    if (QWindow::screen() != screen)
        QWindow::setScreen(screen);

    const HMONITOR monitor = monitorFor(screen);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!monitor || !GetMonitorInfoW(monitor, &mi))
        return;

    const int px = qRound(m_thickness * screen->devicePixelRatio());
    const auto hwnd = reinterpret_cast<HWND>(winId());

    RECT rc;
    if (m_exclusive && m_appBarRegistered) {
        // Negotiate with the shell: start from the whole monitor, let it move
        // us off other app bars (e.g. the taskbar on the same edge), then
        // reserve the final rect.
        APPBARDATA abd{};
        abd.cbSize = sizeof(abd);
        abd.hWnd = hwnd;
        abd.uEdge = toAbEdge(m_edge);
        abd.rc = mi.rcMonitor;
        clampToEdge(abd.rc, m_edge, px);
        SHAppBarMessage(ABM_QUERYPOS, &abd);
        clampToEdge(abd.rc, m_edge, px);
        // SETPOS notifies every app bar (including us) with ABN_POSCHANGED;
        // skip it when nothing changed so we don't ping-pong.
        const QRect reserved(QPoint(abd.rc.left, abd.rc.top), QPoint(abd.rc.right - 1, abd.rc.bottom - 1));
        if (reserved != m_reservedRect) {
            SHAppBarMessage(ABM_SETPOS, &abd);
            m_reservedRect = reserved;
        }
        rc = abd.rc;
    } else {
        // Not reserving space: sit inside the current work area so we don't
        // land on top of the taskbar.
        rc = mi.rcWork;
        clampToEdge(rc, m_edge, px);
    }

    SetWindowPos(hwnd, nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void PanelWindow::registerAppBar()
{
    if (m_appBarRegistered)
        return;
    // Registered even when not exclusive: that's how we receive fullscreen-app
    // notifications. Space is only reserved by ABM_SETPOS in layout().
    APPBARDATA abd{};
    abd.cbSize = sizeof(abd);
    abd.hWnd = reinterpret_cast<HWND>(winId());
    abd.uCallbackMessage = kAppBarCallback;
    m_appBarRegistered = SHAppBarMessage(ABM_NEW, &abd) != FALSE;
}

void PanelWindow::unregisterAppBar()
{
    if (!m_appBarRegistered)
        return;
    APPBARDATA abd{};
    abd.cbSize = sizeof(abd);
    abd.hWnd = reinterpret_cast<HWND>(winId());
    SHAppBarMessage(ABM_REMOVE, &abd);
    m_appBarRegistered = false;
    m_reservedRect = {};
}

void PanelWindow::showEvent(QShowEvent *event)
{
    QQuickWindow::showEvent(event);
    registerAppBar();
    layout();
}

void PanelWindow::hideEvent(QHideEvent *event)
{
    unregisterAppBar();
    QQuickWindow::hideEvent(event);
}

void PanelWindow::setFullscreenApp(bool active)
{
    if (m_fullscreenApp == active)
        return;
    m_fullscreenApp = active;
    applyZOrder();
    emit fullscreenAppActiveChanged();
}

void PanelWindow::applyZOrder()
{
    if (!m_complete)
        return;
    const auto hwnd = reinterpret_cast<HWND>(winId());
    constexpr UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;
    if (m_fullscreenApp && m_hideOnFullscreen) {
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, flags);
        SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, flags);
    } else {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, flags);
    }
}

bool PanelWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    const auto *msg = static_cast<const MSG *>(message);

    if (msg->message == kAppBarCallback) {
        switch (msg->wParam) {
        case ABN_POSCHANGED:
            scheduleLayout();
            break;
        case ABN_FULLSCREENAPP: {
            // Sent to every app bar; only react if it's on our monitor.
            if (msg->lParam) {
                const HMONITOR fg = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTONULL);
                if (fg && fg == monitorFor(targetScreen()))
                    setFullscreenApp(true);
            } else {
                setFullscreenApp(false);
            }
            break;
        }
        }
        *result = 0;
        return true;
    }

    if (msg->message == kTaskbarCreated) {
        m_appBarRegistered = false;
        m_reservedRect = {};
        if (isVisible()) {
            registerAppBar();
            scheduleLayout();
        }
    } else if (msg->message == WM_DPICHANGED || msg->message == WM_DISPLAYCHANGE
               || (!m_exclusive && msg->message == WM_SETTINGCHANGE && msg->wParam == SPI_SETWORKAREA)) {
        // Let Qt handle it first, then snap back to our edge.
        scheduleLayout();
    }

    return QQuickWindow::nativeEvent(eventType, message, result);
}
