#include "windowthumbnail.h"

#include <QQuickWindow>

#include <windows.h>
#include <dwmapi.h>

#include <algorithm>
#include <cmath>

WindowThumbnail::WindowThumbnail(QQuickItem *parent)
    : QQuickItem(parent)
{
}

WindowThumbnail::~WindowThumbnail()
{
    unregister();
}

void WindowThumbnail::setHwnd(double hwnd)
{
    if (m_hwnd == hwnd)
        return;
    unregister();
    m_hwnd = hwnd;
    m_sourceSize = {};
    emit hwndChanged();
    emit sourceSizeChanged();
    updateThumbnail();
}

void WindowThumbnail::itemChange(ItemChange change, const ItemChangeData &data)
{
    QQuickItem::itemChange(change, data);
    if (change == ItemSceneChange) {
        // The thumbnail belongs to a window; follow the item from one to another.
        if (m_windowConnection)
            disconnect(m_windowConnection);
        unregister();
        if (data.window)
            m_windowConnection = connect(data.window, &QWindow::visibleChanged, this, &WindowThumbnail::updateThumbnail);
        updateThumbnail();
    } else if (change == ItemVisibleHasChanged) {
        updateThumbnail();
    }
}

void WindowThumbnail::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    updateThumbnail();
}

void WindowThumbnail::updateThumbnail()
{
    QQuickWindow *w = window();
    const auto source = reinterpret_cast<HWND>(quintptr(m_hwnd));
    if (!w || !w->isVisible() || !w->handle() || !source || !IsWindow(source) || !isVisible() || width() <= 0
        || height() <= 0) {
        unregister();
        return;
    }
    if (!m_thumbnail) {
        HTHUMBNAIL thumbnail = nullptr;
        if (FAILED(DwmRegisterThumbnail(reinterpret_cast<HWND>(w->winId()), source, &thumbnail)))
            return;
        m_thumbnail = thumbnail;
        SIZE size{};
        if (SUCCEEDED(DwmQueryThumbnailSourceSize(thumbnail, &size)) && size.cx > 0 && size.cy > 0) {
            m_sourceSize = QSize(int(size.cx), int(size.cy));
            emit sourceSizeChanged();
        }
    }

    // The window fitted into the item, centred, at its own aspect.
    QRectF rect(0, 0, width(), height());
    if (m_sourceSize.isValid() && !m_sourceSize.isEmpty()) {
        const qreal scale = std::min(width() / m_sourceSize.width(), height() / m_sourceSize.height());
        const QSizeF fitted(m_sourceSize.width() * scale, m_sourceSize.height() * scale);
        rect = QRectF(QPointF((width() - fitted.width()) / 2, (height() - fitted.height()) / 2), fitted);
    }
    // DWM wants the destination in the window's physical pixels.
    const QPointF topLeft = mapToScene(rect.topLeft());
    const qreal dpr = w->devicePixelRatio();
    const auto px = [dpr](qreal v) { return LONG(std::lround(v * dpr)); };
    DWM_THUMBNAIL_PROPERTIES props{};
    props.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
    props.rcDestination = {px(topLeft.x()), px(topLeft.y()), px(topLeft.x() + rect.width()),
                           px(topLeft.y() + rect.height())};
    props.fVisible = TRUE;
    props.opacity = 255;
    props.fSourceClientAreaOnly = FALSE;
    DwmUpdateThumbnailProperties(static_cast<HTHUMBNAIL>(m_thumbnail), &props);
}

void WindowThumbnail::unregister()
{
    if (m_thumbnail)
        DwmUnregisterThumbnail(static_cast<HTHUMBNAIL>(m_thumbnail));
    m_thumbnail = nullptr;
}
