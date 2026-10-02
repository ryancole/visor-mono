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

// image://visor-tray-icon/<hicon>: a notification-area icon. The HICON is
// visor-shell's copy; icons are session-wide, so it can be drawn from here.
class TrayIconProvider : public QQuickImageProvider
{
public:
    TrayIconProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
