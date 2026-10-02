#include "osdwindow.h"

#include "screens.h"

#include <QGuiApplication>
#include <QScreen>
#include <QSurfaceFormat>

#include <windows.h>

#include <algorithm>

OsdWindow::OsdWindow(QWindow *parent)
    : QQuickWindow(parent)
{
    // Tool: no taskbar button / Alt+Tab entry, and visor-wm leaves it alone.
    // DoesNotAcceptFocus: WS_EX_NOACTIVATE, shown with SW_SHOWNOACTIVATE.
    setFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    // Transparent unless the config says otherwise: the shape is drawn in QML.
    QSurfaceFormat format = requestedFormat();
    format.setAlphaBufferSize(8);
    setFormat(format);
    setColor(Qt::transparent);

    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] { setShown(false); });
    // Content often sizes itself after the first show (a toast stack
    // growing): keep the window where its placement says.
    const auto replace = [this] {
        if (isVisible())
            place();
    };
    connect(this, &QWindow::widthChanged, this, replace);
    connect(this, &QWindow::heightChanged, this, replace);
}

void OsdWindow::setPlacement(Placement placement)
{
    if (m_placement == placement)
        return;
    m_placement = placement;
    if (isVisible())
        place();
    emit placementChanged();
}

void OsdWindow::setMargin(int margin)
{
    if (m_margin == margin)
        return;
    m_margin = margin;
    if (isVisible())
        place();
    emit marginChanged();
}

void OsdWindow::setClickThrough(bool through)
{
    if (m_clickThrough == through)
        return;
    m_clickThrough = through;
    setFlag(Qt::WindowTransparentForInput, through); // WS_EX_TRANSPARENT
    emit clickThroughChanged();
}

void OsdWindow::setTimeout(int ms)
{
    ms = std::max(0, ms);
    if (m_timeout == ms)
        return;
    m_timeout = ms;
    emit timeoutChanged();
}

void OsdWindow::setShown(bool shown)
{
    if (m_shown == shown)
        return;
    m_shown = shown;
    emit shownChanged();
}

void OsdWindow::open(QObject *screenObject)
{
    QScreen *target = screenObject ? screenForObject(screenObject) : nullptr;
    if (!target)
        target = m_placement == BottomRight ? QGuiApplication::primaryScreen() : screenOfFocusedWindow();
    if (target && target != screen())
        setScreen(target);
    create();
    place();
    if (!isVisible())
        show();
    // Stay on top of whatever has focus, without taking it.
    SetWindowPos(reinterpret_cast<HWND>(winId()), HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    setShown(true);
    if (m_timeout > 0)
        m_timer.start(m_timeout);
    else
        m_timer.stop();
}

void OsdWindow::close()
{
    m_timer.stop();
    setShown(false);
    if (isVisible())
        hide();
}

void OsdWindow::place()
{
    QScreen *s = screen();
    if (!s)
        return;
    const QRect work = s->availableGeometry();
    int x = work.left() + (work.width() - width()) / 2;
    int y = work.top() + m_margin;
    switch (m_placement) {
    case Bottom:
        y = work.top() + work.height() - height() - m_margin;
        break;
    case BottomRight:
        x = work.left() + work.width() - width() - m_margin;
        y = work.top() + work.height() - height() - m_margin;
        break;
    case Center:
        y = work.top() + (work.height() - height()) / 2;
        break;
    case Top:
        break;
    }
    // Inside the work area, whatever the size.
    setPosition(std::max(x, work.left()), std::max(y, work.top()));
}
