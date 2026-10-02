#pragma once

#include <QList>
#include <QtGlobal>

#include <thread>

namespace visor::wm {

// Key bindings Windows won't give to RegisterHotKey because it reserves them
// itself (Win+arrows for snapping, Win+Shift+arrows, Win+Return, Win+= ...),
// caught with a low-level keyboard hook instead. Matching keys are swallowed
// so Windows never acts on them, and reported by posting `message` (wParam =
// the key's id) to `target`.
//
// The hook runs on its own thread: Windows silently removes a low-level hook
// whose thread is slow to answer, and visor-wm's main thread may be busy.
// Like any non-elevated hook it gets no keys while an elevated window (e.g.
// Task Manager) has focus; RegisterHotKey bindings still work then.
class KeyHook
{
public:
    struct Key
    {
        quint32 modifiers = 0; // MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN
        quint32 vk = 0;
        bool repeat = false;   // report auto-repeats too
        int id = 0;
    };

    KeyHook(void *target, unsigned message);
    ~KeyHook();
    KeyHook(const KeyHook &) = delete;
    KeyHook &operator=(const KeyHook &) = delete;

    // Replaces the keys (an empty list removes the hook).
    void setKeys(const QList<Key> &keys);

private:
    void start();
    void stop();
    void run();

    void *m_target;
    unsigned m_message;
    QList<Key> m_keys;
    std::thread m_thread;
    unsigned long m_threadId = 0;
};

} // namespace visor::wm
