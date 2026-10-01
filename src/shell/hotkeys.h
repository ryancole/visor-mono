#pragma once

#include <QObject>

#include <cstdint>

namespace visor {

// Global hotkeys (RegisterHotKey on a message-only window). Phase 0 uses
// Ctrl+Alt combinations, which nothing in Windows reserves; Win-key bindings
// come with the window manager.
class Hotkeys : public QObject
{
    Q_OBJECT

public:
    enum Action {
        OpenFileExplorer = 1,
        OpenTerminal,
        ShowRun,
        QuitToExplorer,
    };
    Q_ENUM(Action)

    explicit Hotkeys(QObject *parent = nullptr);
    ~Hotkeys() override;

    // Window procedure body; called from the Win32 window procedure.
    std::intptr_t handleMessage(void *hwnd, unsigned msg, std::uintptr_t wParam, std::intptr_t lParam);

signals:
    void triggered(visor::Hotkeys::Action action);

private:
    void *m_hwnd = nullptr;
};

} // namespace visor
