#include "screens.h"

#include <QGuiApplication>
#include <QScreen>
#include <QtGui/qscreen_platform.h>

#include <windows.h>

QScreen *screenForObject(QObject *object)
{
    if (!object)
        return nullptr;
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

QScreen *screenOfFocusedWindow()
{
    const HWND foreground = GetForegroundWindow();
    const HMONITOR monitor =
        MonitorFromWindow(foreground ? foreground : GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
    const auto screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        if (auto *native = screen->nativeInterface<QNativeInterface::QWindowsScreen>()) {
            if (native->handle() == monitor)
                return screen;
        }
    }
    return QGuiApplication::primaryScreen();
}
