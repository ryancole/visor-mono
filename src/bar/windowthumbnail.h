#pragma once

#include <QQuickItem>
#include <QSize>
#include <QtQml/qqmlregistration.h>

// A live preview of another window, drawn by DWM (DwmRegisterThumbnail)
// into this item's rectangle, fitted and centred with the window's aspect.
// DWM composes it over whatever Qt draws here, so the item itself paints
// nothing. It appears once the item's window is on screen and goes with the
// item. A minimised window has no live content; its thumbnail stays blank.
//
//   WindowThumbnail { hwnd: task.hwnd; width: 220; height: 124 }
class WindowThumbnail : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    // The window to preview, as `Tasks` gives it.
    Q_PROPERTY(double hwnd READ hwnd WRITE setHwnd NOTIFY hwndChanged)
    // The window's size in physical pixels, once known (for aspect ratios).
    Q_PROPERTY(QSize sourceSize READ sourceSize NOTIFY sourceSizeChanged)

public:
    explicit WindowThumbnail(QQuickItem *parent = nullptr);
    ~WindowThumbnail() override;

    double hwnd() const { return m_hwnd; }
    void setHwnd(double hwnd);
    QSize sourceSize() const { return m_sourceSize; }

signals:
    void hwndChanged();
    void sourceSizeChanged();

protected:
    void itemChange(ItemChange change, const ItemChangeData &data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void updateThumbnail();
    void unregister();

    double m_hwnd = 0;
    QSize m_sourceSize;
    void *m_thumbnail = nullptr; // HTHUMBNAIL
    QMetaObject::Connection m_windowConnection;
};
