#include "popupwindow.h"

#include "services/shelllink.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QScreen>
#include <QtGui/qscreen_platform.h>

#include <windows.h>
#include <dwmapi.h>

#include <algorithm>

namespace {

QScreen *screenOf(HMONITOR monitor)
{
    const auto screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        if (auto *native = screen->nativeInterface<QNativeInterface::QWindowsScreen>()) {
            if (native->handle() == monitor)
                return screen;
        }
    }
    return QGuiApplication::primaryScreen();
}

// A QScreen, or a QML screen object with a matching `name`.
QScreen *screenOf(QObject *object)
{
    if (auto *screen = qobject_cast<QScreen *>(object))
        return screen;
    const QString name = object->property("name").toString();
    const auto screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        if (screen->name() == name)
            return screen;
    }
    return nullptr;
}

} // namespace

PopupWindow::PopupWindow(QWindow *parent)
    : QQuickWindow(parent)
{
    // Tool: no taskbar button / Alt+Tab entry, and visor-wm leaves it alone.
    setFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
    setColor(Qt::black);
    // Content often sizes itself after the first show (a list filling, a
    // Flow laying out): keep the window where its placement says.
    const auto replace = [this] {
        if (isVisible())
            place();
    };
    connect(this, &QWindow::widthChanged, this, replace);
    connect(this, &QWindow::heightChanged, this, replace);
}

void PopupWindow::setPlacement(Placement placement)
{
    if (m_placement == placement)
        return;
    m_placement = placement;
    if (isVisible())
        place();
    emit placementChanged();
}

void PopupWindow::setMargin(int margin)
{
    if (m_margin == margin)
        return;
    m_margin = margin;
    if (isVisible())
        place();
    emit marginChanged();
}

void PopupWindow::setCloseOnDeactivate(bool close)
{
    if (m_closeOnDeactivate == close)
        return;
    m_closeOnDeactivate = close;
    emit closeOnDeactivateChanged();
}

void PopupWindow::open(QObject *screenObject)
{
    if (isVisible()) {
        requestActivate();
        return;
    }
    const HWND foreground = GetForegroundWindow();
    m_previous = foreground;
    // Where the user is working: the focused window's monitor.
    QScreen *target = screenObject ? screenOf(screenObject) : nullptr;
    if (!target)
        target = screenOf(MonitorFromWindow(foreground ? foreground : GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY));
    setScreen(target);
    create();
    const auto hwnd = reinterpret_cast<HWND>(winId());
    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    const COLORREF border = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
    place();
    show();
    // Opened from a key in visor-wm or a click on the bar: neither gives this
    // process the foreground by itself.
    ShellLink::takeForeground(hwnd);
    requestActivate();
    emit opened();
}

void PopupWindow::close()
{
    if (!isVisible())
        return;
    // Back to the window the user was in, rather than whatever Windows
    // picks; unless focus already went somewhere else (another pop-up, a
    // click on an app), which is why we're closing.
    const bool ours = GetForegroundWindow() == reinterpret_cast<HWND>(winId());
    hide();
    const auto previous = static_cast<HWND>(m_previous);
    m_previous = nullptr;
    if (ours && previous && IsWindow(previous) && IsWindowVisible(previous))
        SetForegroundWindow(previous);
    emit closed();
}

void PopupWindow::toggle(QObject *screenObject)
{
    if (isVisible())
        close();
    else
        open(screenObject);
}

void PopupWindow::place()
{
    QScreen *s = screen();
    if (!s)
        return;
    const QRect work = s->availableGeometry();
    const int x = m_placement == BelowLeft ? work.left() + m_margin : work.left() + (work.width() - width()) / 2;
    const int y = m_placement == Center ? work.top() + (work.height() - height()) / 2 : work.top() + m_margin;
    // Never over the bar, whatever the size.
    setPosition(std::max(x, work.left()), std::max(y, work.top()));
}

void PopupWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QQuickWindow::keyPressEvent(event);
}

bool PopupWindow::event(QEvent *event)
{
    if (event->type() == QEvent::FocusOut && m_closeOnDeactivate && isVisible()) {
        // Focus went to another window (not to a child of ours).
        const HWND foreground = GetForegroundWindow();
        if (foreground != reinterpret_cast<HWND>(winId()))
            close();
    }
    return QQuickWindow::event(event);
}
