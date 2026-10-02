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
    DWORD alone = 0;       // a release key that is down with nothing pressed since
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

const KeyHook::Key *find(const HookState *state, DWORD vk, quint32 mods, bool release)
{
    for (const KeyHook::Key &key : std::as_const(state->keys)) {
        if (key.vk == vk && key.modifiers == mods && key.release == release)
            return &key;
    }
    return nullptr;
}

LRESULT CALLBACK hookProc(int code, WPARAM wParam, LPARAM lParam)
{
    const auto *event = reinterpret_cast<const KBDLLHOOKSTRUCT *>(lParam);
    HookState *state = g_state;
    if (code != HC_ACTION || !state || event->dwExtraInfo == kOwnInput)
        return CallNextHookEx(nullptr, code, wParam, lParam);

    const DWORD vk = event->vkCode;
    const bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
    const bool modifier = isModifier(vk);

    if (!down) {
        if (vk == state->alone) {
            // Released with nothing pressed in between: fire. Modifiers are
            // passed on so Windows sees them come up; the mask key stops the
            // Win release from meaning "Start" should Explorer be around.
            state->alone = 0;
            if (const KeyHook::Key *key = find(state, vk, currentModifiers(), true)) {
                if (vk == VK_LWIN || vk == VK_RWIN)
                    maskWinRelease();
                PostMessageW(state->target, state->message, WPARAM(key->id), 0);
            }
        }
        if (state->swallowed.remove(vk))
            return 1;
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    // Any other key pressed while a release key is down cancels it (a
    // held key auto-repeats; those don't count).
    if (vk != state->alone && !(state->alone && isDown(int(vk))))
        state->alone = 0;
    const quint32 mods = currentModifiers();
    if (modifier) {
        // A modifier counts as "pressed alone" with itself held, e.g. VK_LWIN
        // with MOD_WIN, which is what its release will report.
        const quint32 asHeld = mods | (vk == VK_LWIN || vk == VK_RWIN ? MOD_WIN
                               : vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_SHIFT ? MOD_SHIFT
                               : vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_CONTROL ? MOD_CONTROL
                                                                                            : MOD_ALT);
        if (!isDown(int(vk)) && find(state, vk, asHeld, true))
            state->alone = vk;
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    if (const KeyHook::Key *key = find(state, vk, mods, false)) {
        const bool repeat = state->swallowed.contains(vk);
        state->swallowed.insert(vk);
        if (!repeat || key->repeat)
            PostMessageW(state->target, state->message, WPARAM(key->id), 0);
        if (mods & MOD_WIN)
            maskWinRelease();
        return 1;
    }
    if (find(state, vk, mods, true)) {
        // A release binding on an ordinary key: swallow it, fire on key-up.
        if (!state->swallowed.contains(vk))
            state->alone = vk;
        state->swallowed.insert(vk);
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
