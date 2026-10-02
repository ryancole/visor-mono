#include "wm/keyhook.h"

#include <QDebug>
#include <QSet>

#include <windows.h>

#include <atomic>

namespace visor::wm {

namespace {

// An unassigned virtual key. Pressing it while Win is down makes the Win
// release not count as "Win pressed alone", which would open the Start menu
// (the same trick AutoHotkey uses).
constexpr WORD kMaskKey = 0xE8;
// Tags our own injected keys so the hook lets them through. Other injected
// input (remapping tools, automation) is matched like real keys.
constexpr ULONG_PTR kOwnInput = 0x56574D31; // 'VWM1'

// The hook procedure has no context argument; one KeyHook runs at a time.
struct HookState
{
    HWND target = nullptr;
    UINT message = 0;
    QList<KeyHook::Key> keys;
    QSet<DWORD> swallowed; // keys whose key-down we ate: eat their key-up too
};
HookState *g_state = nullptr;

bool isDown(int vk)
{
    return GetAsyncKeyState(vk) & 0x8000;
}

bool isModifier(DWORD vk)
{
    switch (vk) {
    case VK_LWIN: case VK_RWIN:
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_MENU: case VK_LMENU: case VK_RMENU:
        return true;
    default:
        return false;
    }
}

// Modifiers pressed before this key. Inside a low-level hook the async key
// state already reflects earlier keys (just not the one being reported).
quint32 currentModifiers()
{
    quint32 mods = 0;
    if (isDown(VK_LWIN) || isDown(VK_RWIN))
        mods |= MOD_WIN;
    if (isDown(VK_SHIFT))
        mods |= MOD_SHIFT;
    if (isDown(VK_CONTROL))
        mods |= MOD_CONTROL;
    if (isDown(VK_MENU))
        mods |= MOD_ALT;
    return mods;
}

void maskWinRelease()
{
    INPUT inputs[2]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = kMaskKey;
    inputs[0].ki.dwExtraInfo = kOwnInput;
    inputs[1] = inputs[0];
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

LRESULT CALLBACK hookProc(int code, WPARAM wParam, LPARAM lParam)
{
    const auto *event = reinterpret_cast<const KBDLLHOOKSTRUCT *>(lParam);
    HookState *state = g_state;
    if (code != HC_ACTION || !state || event->dwExtraInfo == kOwnInput || isModifier(event->vkCode))
        return CallNextHookEx(nullptr, code, wParam, lParam);

    const DWORD vk = event->vkCode;
    const bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
    if (!down) {
        if (state->swallowed.remove(vk))
            return 1;
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    const quint32 mods = currentModifiers();
    for (const KeyHook::Key &key : std::as_const(state->keys)) {
        if (key.vk != vk || key.modifiers != mods)
            continue;
        const bool repeat = state->swallowed.contains(vk);
        state->swallowed.insert(vk);
        if (!repeat || key.repeat)
            PostMessageW(state->target, state->message, WPARAM(key.id), 0);
        if (mods & MOD_WIN)
            maskWinRelease();
        return 1;
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

} // namespace

KeyHook::KeyHook(void *target, unsigned message)
    : m_target(target)
    , m_message(message)
{
}

KeyHook::~KeyHook()
{
    stop();
}

void KeyHook::setKeys(const QList<Key> &keys)
{
    stop();
    m_keys = keys;
    if (!m_keys.isEmpty())
        start();
}

void KeyHook::start()
{
    std::atomic<DWORD> threadId = 0;
    m_thread = std::thread([this, &threadId] {
        // A message queue must exist before anyone can PostThreadMessage to it.
        MSG msg;
        PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        threadId = GetCurrentThreadId();
        threadId.notify_one();
        run();
    });
    threadId.wait(0);
    m_threadId = threadId;
}

void KeyHook::stop()
{
    if (!m_thread.joinable())
        return;
    PostThreadMessageW(m_threadId, WM_QUIT, 0, 0);
    m_thread.join();
    m_threadId = 0;
}

void KeyHook::run()
{
    HookState state;
    state.target = static_cast<HWND>(m_target);
    state.message = m_message;
    state.keys = m_keys;
    g_state = &state;

    HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, hookProc, GetModuleHandleW(nullptr), 0);
    if (!hook)
        qWarning() << "keyboard hook failed, error" << GetLastError();

    // The hook is called on this thread, from inside GetMessage.
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hook)
        UnhookWindowsHookEx(hook);
    g_state = nullptr;
}

} // namespace visor::wm
