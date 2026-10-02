#pragma once

class QObject;
class QScreen;

// The QScreen for a QML screen object (one of Qt.application.screens, or a
// QScreen), or null.
QScreen *screenForObject(QObject *object);
// The monitor of the window that has focus: where the user is working. The
// primary one if there's no such window.
QScreen *screenOfFocusedWindow();
