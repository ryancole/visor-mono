#pragma once

#include <QQuickImageProvider>

// image://visor-window-icon/<hwnd>: a top-level window's icon, as the taskbar
// would show it. Falls back to the class icon, then the executable's icon.
class WindowIconProvider : public QQuickImageProvider
{
public:
    WindowIconProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
