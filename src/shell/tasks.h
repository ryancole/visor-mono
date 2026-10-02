#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <cstdint>

namespace visor {

// The windows that would get a taskbar button, tracked from shell hook
// messages (RegisterShellHookWindow) and cloak changes. Event-driven; the only
// full scan is at startup.
class Tasks : public QObject
{
    Q_OBJECT

public:
    struct Task
    {
        quintptr hwnd = 0;
        QString title;
        quint32 pid = 0;
        QString path;
        bool flashing = false;
    };

    // asShell: also register as the task manager window (replace mode).
    explicit Tasks(bool asShell, QObject *parent = nullptr);
    ~Tasks() override;

    QList<Task> tasks() const;
    quintptr active() const { return m_active; }

    // Window procedure body; called from the Win32 window procedure.
    std::intptr_t handleMessage(void *window, unsigned msg, std::uintptr_t wParam, std::intptr_t lParam);
    // Called from the WinEvent hook when a window is cloaked or uncloaked.
    void reevaluate(quintptr hwnd);
    // Called from the WinEvent hook when the foreground window moved or resized.
    void foregroundMoved() { emit fullscreenChanged(); }

signals:
    void added(const visor::Tasks::Task &task);
    void changed(const visor::Tasks::Task &task);
    void removed(quintptr hwnd);
    void activated(quintptr hwnd);
    // A window may have entered or left fullscreen: HSHELL_FULLSCREENENTER/
    // EXIT, or the foreground window moved or resized (e.g. visor-wm's
    // fullscreen, or a borderless game resizing itself).
    void fullscreenChanged();

private:
    void add(quintptr hwnd);
    void remove(quintptr hwnd);
    void refreshTitle(quintptr hwnd);

    void *m_hwnd = nullptr;
    void *m_cloakHook = nullptr;
    void *m_locationHook = nullptr;
    unsigned m_shellHookMessage = 0;
    QList<Task> m_tasks; // in the order windows appeared
    quintptr m_active = 0;
};

} // namespace visor
