#pragma once

#include <QObject>

#include <cstdint>

namespace visor {

// The desktop: a window covering the virtual screen, pinned to the bottom of
// the z-order, registered as the session's shell window (GetShellWindow).
// Only created when visor-shell is the shell; under Explorer, Progman is the
// desktop.
class DesktopWindow : public QObject
{
    Q_OBJECT

public:
    explicit DesktopWindow(QObject *parent = nullptr);
    ~DesktopWindow() override;

    void *hwnd() const { return m_hwnd; }

    // Shows the window and registers it with SetShellWindow. Returns false if
    // that fails, e.g. because another shell window already exists.
    bool show();

    // Window procedure body; called from the Win32 window procedure.
    std::intptr_t handleMessage(unsigned msg, std::uintptr_t wParam, std::intptr_t lParam);

signals:
    // Monitors were added, removed, moved or resized.
    void displayChanged();

private:
    void fitToVirtualScreen();
    void paint();

    void *m_hwnd = nullptr;
};

} // namespace visor
