#include "shell/trayhost.h"

#include "shell/appbars.h"

#include <QDebug>

#include <windows.h>
#include <docobj.h>
#include <shellapi.h>
#include <shlguid.h>

namespace visor {

namespace {

// Shell_NotifyIcon's wire format (shell32 -> Shell_TrayWnd, WM_COPYDATA
// dwData 1). Handles travel as 32-bit values even from 64-bit processes.
// Older callers send a shorter NOTIFYICONDATA; missing fields read as zero.
struct NotifyIconData32
{
    DWORD cbSize;
    DWORD hWnd;
    DWORD uID;
    DWORD uFlags;
    DWORD uCallbackMessage;
    DWORD hIcon;
    WCHAR szTip[128];
    DWORD dwState;
    DWORD dwStateMask;
    WCHAR szInfo[256];
    DWORD uVersion; // union with uTimeout
    WCHAR szInfoTitle[64];
    DWORD dwInfoFlags;
    GUID guidItem;
    DWORD hBalloonIcon;
};

struct ShellTrayData
{
    DWORD dwSignature;
    DWORD dwMessage; // NIM_*
    NotifyIconData32 nid;
};

// Shell_NotifyIconGetRect (dwData 3).
struct NotifyIconIdentifier
{
    DWORD dwMagic;
    DWORD dwMessage; // 1: top-left, 2: bottom-right
    DWORD cbSize;
    DWORD dwPadding;
    DWORD hWnd;
    DWORD uID;
    GUID guidItem;
};

constexpr int kIconRectSize = 24; // what we report for an icon's rect

// Sign-extends a 32-bit wire handle (HandleToLong's inverse).
quintptr wireHandle(DWORD value)
{
    return reinterpret_cast<quintptr>(LongToHandle(LONG(value)));
}

QString wideString(const WCHAR *text, size_t capacity)
{
    size_t len = 0;
    while (len < capacity && text[len])
        ++len;
    return QString::fromWCharArray(text, qsizetype(len));
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE) {
        const auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    if (auto *self = reinterpret_cast<TrayHost *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
        return self->handleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool registerClass(const wchar_t *name)
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = name;
    return RegisterClassExW(&wc) != 0;
}

} // namespace

TrayHost::TrayHost(AppBars *appBars, QObject *parent)
    : QObject(parent)
    , m_appBars(appBars)
{
    if (FindWindowW(L"Shell_TrayWnd", nullptr)) {
        qWarning() << "another Shell_TrayWnd exists; not hosting the tray";
        return;
    }
    registerClass(L"Shell_TrayWnd");
    registerClass(L"TrayNotifyWnd");

    // Never shown: Visor draws the tray. It is still positioned where the
    // tray UI is, since apps read its rect (ABM_GETTASKBARPOS, GetRect).
    const HWND tray = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"Shell_TrayWnd", L"",
                                      WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0, nullptr, nullptr,
                                      GetModuleHandleW(nullptr), this);
    if (!tray) {
        qCritical() << "failed to create Shell_TrayWnd, error" << GetLastError();
        return;
    }
    m_hwnd = tray;
    m_notifyHwnd = CreateWindowExW(0, L"TrayNotifyWnd", L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0,
                                   0, 0, tray, nullptr, GetModuleHandleW(nullptr), this);
    placeWindow();
    connect(m_appBars, &AppBars::trayRectChanged, this, &TrayHost::placeWindow);

    // Apps (re-)add their icons when they see this, as after an Explorer
    // restart.
    SendNotifyMessageW(HWND_BROADCAST, RegisterWindowMessageW(L"TaskbarCreated"), 0, 0);
    qInfo() << "hosting the notification area";

    loadShellServiceObjects();
}

TrayHost::~TrayHost()
{
    for (void *p : m_serviceObjects) {
        auto *target = static_cast<IOleCommandTarget *>(p);
        target->Exec(&CGID_ShellServiceObject, OLECMDID_SAVE, OLECMDEXECOPT_DODEFAULT, nullptr, nullptr);
        target->Release();
    }
    for (const Icon &icon : m_icons) {
        if (icon.icon)
            DestroyIcon(reinterpret_cast<HICON>(icon.icon));
    }
    if (m_hwnd)
        DestroyWindow(static_cast<HWND>(m_hwnd));
}

// The system icons (volume, network, power) came from shell service objects
// that Explorer loads at startup. On Windows 11 these may no longer add
// anything; loading them is cheap to try.
void TrayHost::loadShellServiceObjects()
{
    static const CLSID kSysTray = {0x35CEC8A3, 0x2BE6, 0x11D2, {0x87, 0x73, 0x92, 0xE2, 0x20, 0x52, 0x41, 0x53}};
    IOleCommandTarget *target = nullptr;
    HRESULT hr = CoCreateInstance(kSysTray, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&target));
    if (FAILED(hr)) {
        qInfo() << "SysTray service object unavailable" << Qt::hex << hr;
        return;
    }
    hr = target->Exec(&CGID_ShellServiceObject, OLECMDID_NEW, OLECMDEXECOPT_DODEFAULT, nullptr, nullptr);
    qInfo() << "SysTray service object started" << Qt::hex << hr;
    m_serviceObjects.append(target);
}

void TrayHost::placeWindow()
{
    if (!m_hwnd)
        return;
    const AppBars::Rect r = m_appBars->trayRect();
    SetWindowPos(static_cast<HWND>(m_hwnd), HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOACTIVATE);
    SetWindowPos(static_cast<HWND>(m_notifyHwnd), nullptr, 0, 0, r.right - r.left, r.bottom - r.top,
                 SWP_NOACTIVATE | SWP_NOZORDER);
}

QList<TrayHost::Icon> TrayHost::icons() const
{
    return m_icons;
}

int TrayHost::find(quintptr hwnd, quint32 uid, const QUuid &guid) const
{
    for (qsizetype i = 0; i < m_icons.size(); ++i) {
        const Icon &icon = m_icons[i];
        if (!guid.isNull() ? icon.guid == guid : (icon.hwnd == hwnd && icon.uid == uid))
            return int(i);
    }
    return -1;
}

void TrayHost::remove(int index)
{
    const Icon icon = m_icons.takeAt(index);
    if (icon.icon)
        DestroyIcon(reinterpret_cast<HICON>(icon.icon));
    emit iconRemoved(icon.id);
}

// Apps that crash never delete their icons; drop them once their window is
// gone (Explorer does the same, lazily).
void TrayHost::pruneDeadOwners()
{
    for (qsizetype i = m_icons.size() - 1; i >= 0; --i) {
        if (!IsWindow(reinterpret_cast<HWND>(m_icons[i].hwnd)))
            remove(int(i));
    }
}

bool TrayHost::notifyIcon(unsigned message, const void *data, unsigned long size)
{
    if (size < offsetof(ShellTrayData, nid) + sizeof(DWORD) * 3)
        return false;
    ShellTrayData tray{};
    memcpy(&tray, data, std::min<size_t>(size, sizeof(tray)));
    const NotifyIconData32 &nid = tray.nid;

    const quintptr hwnd = wireHandle(nid.hWnd);
    const QUuid guid = (nid.uFlags & NIF_GUID) ? QUuid(nid.guidItem) : QUuid();
    int index = find(hwnd, nid.uID, guid);

    switch (message) {
    case NIM_ADD:
    case NIM_MODIFY: {
        if (message == NIM_ADD && index >= 0)
            return false;
        const bool isNew = index < 0;
        if (isNew) {
            pruneDeadOwners();
            Icon icon;
            icon.id = m_nextId++;
            icon.hwnd = hwnd;
            icon.uid = nid.uID;
            icon.guid = guid;
            DWORD pid = 0;
            GetWindowThreadProcessId(reinterpret_cast<HWND>(hwnd), &pid);
            icon.pid = pid;
            m_icons.append(icon);
            index = int(m_icons.size() - 1);
        }
        Icon &icon = m_icons[index];
        if (nid.uFlags & NIF_MESSAGE)
            icon.callback = nid.uCallbackMessage;
        if (nid.uFlags & NIF_ICON) {
            if (icon.icon)
                DestroyIcon(reinterpret_cast<HICON>(icon.icon));
            // Our own copy: the app may destroy its handle after the call.
            const HICON source = reinterpret_cast<HICON>(wireHandle(nid.hIcon));
            icon.icon = source ? reinterpret_cast<quintptr>(CopyIcon(source)) : 0;
        }
        if (nid.uFlags & NIF_TIP)
            icon.tip = wideString(nid.szTip, std::size(nid.szTip));
        if ((nid.uFlags & NIF_STATE) && (nid.dwStateMask & NIS_HIDDEN))
            icon.hidden = (nid.dwState & NIS_HIDDEN) != 0;
        if (nid.uFlags & NIF_INFO) {
            // Balloons become notifications in a later phase.
            qInfo().noquote() << "balloon from pid" << icon.pid << ":"
                              << wideString(nid.szInfoTitle, std::size(nid.szInfoTitle)) << "-"
                              << wideString(nid.szInfo, std::size(nid.szInfo));
        }
        if (isNew)
            emit iconAdded(icon);
        else
            emit iconChanged(icon);
        // A MODIFY for an unknown icon is added, but still reports failure,
        // matching Explorer.
        return !(isNew && message == NIM_MODIFY);
    }
    case NIM_DELETE:
        if (index < 0)
            return false;
        remove(index);
        return true;
    case NIM_SETVERSION:
        if (index < 0)
            return false;
        m_icons[index].version = nid.uVersion;
        return true;
    case NIM_SETFOCUS:
        return true;
    default:
        return false;
    }
}

std::intptr_t TrayHost::iconRect(const void *data, unsigned long size)
{
    if (size < sizeof(NotifyIconIdentifier))
        return 0;
    const auto &id = *static_cast<const NotifyIconIdentifier *>(data);
    const QUuid guid(id.guidItem);
    const int index = find(wireHandle(id.hWnd), id.uID, guid);
    if (index < 0)
        return 0;

    // We don't know where Visor drew the icon, but we know where it was last
    // clicked; otherwise use the tray's right end.
    RECT rc;
    if (m_icons[index].id == m_lastClickId) {
        rc = {m_lastClickX - kIconRectSize / 2, m_lastClickY - kIconRectSize / 2,
              m_lastClickX + kIconRectSize / 2, m_lastClickY + kIconRectSize / 2};
    } else {
        const AppBars::Rect t = m_appBars->trayRect();
        rc = {t.right - kIconRectSize, t.top, t.right, t.top + kIconRectSize};
    }
    return id.dwMessage == 1 ? MAKELONG(rc.left, rc.top) : MAKELONG(rc.right, rc.bottom);
}

void TrayHost::click(int id, const QString &button, int x, int y)
{
    pruneDeadOwners();
    const Icon *icon = nullptr;
    for (const Icon &i : m_icons) {
        if (i.id == id)
            icon = &i;
    }
    if (!icon)
        return;
    m_lastClickId = id;
    m_lastClickX = x;
    m_lastClickY = y;

    const auto hwnd = reinterpret_cast<HWND>(icon->hwnd);
    const quint32 version = icon->version;
    const quint32 uid = icon->uid;
    const quint32 callback = icon->callback;
    // Version 4 apps get the anchor point in wParam and the icon id in the
    // high word of lParam; older ones get the id in wParam.
    const auto send = [&](UINT message) {
        if (version >= NOTIFYICON_VERSION_4)
            SendNotifyMessageW(hwnd, callback, MAKEWPARAM(x, y), MAKELPARAM(message, uid));
        else
            SendNotifyMessageW(hwnd, callback, uid, message);
    };

    // Explorer also sends NIN_SELECT / WM_CONTEXTMENU to version 3 icons.
    if (button == QLatin1String("left")) {
        send(WM_LBUTTONDOWN);
        send(WM_LBUTTONUP);
        if (version >= NOTIFYICON_VERSION)
            send(NIN_SELECT);
    } else if (button == QLatin1String("double")) {
        send(WM_LBUTTONDBLCLK);
        send(WM_LBUTTONUP);
    } else if (button == QLatin1String("right")) {
        send(WM_RBUTTONDOWN);
        send(WM_RBUTTONUP);
        if (version >= NOTIFYICON_VERSION)
            send(WM_CONTEXTMENU);
    } else if (button == QLatin1String("middle")) {
        send(WM_MBUTTONDOWN);
        send(WM_MBUTTONUP);
    }
}

std::intptr_t TrayHost::handleMessage(void *window, unsigned msg, std::uintptr_t wParam, std::intptr_t lParam)
{
    const auto hwnd = static_cast<HWND>(window);
    if (msg == WM_COPYDATA && hwnd == m_hwnd) {
        const auto *cds = reinterpret_cast<const COPYDATASTRUCT *>(lParam);
        if (!cds || !cds->lpData)
            return 0;
        switch (cds->dwData) {
        case 0:
            return m_appBars->handle(cds->lpData, cds->cbData);
        case 1: {
            if (cds->cbData < sizeof(DWORD) * 2)
                return 0;
            const DWORD message = static_cast<const DWORD *>(cds->lpData)[1];
            return notifyIcon(message, cds->lpData, cds->cbData) ? 1 : 0;
        }
        case 3:
            return iconRect(cds->lpData, cds->cbData);
        default:
            return 0;
        }
    }
    if (msg == WM_WINDOWPOSCHANGED && hwnd == m_hwnd) {
        // Some apps show the taskbar window; keep it hidden.
        const auto *pos = reinterpret_cast<const WINDOWPOS *>(lParam);
        if (pos->flags & SWP_SHOWWINDOW)
            SetWindowLongPtrW(hwnd, GWL_STYLE, GetWindowLongPtrW(hwnd, GWL_STYLE) & ~WS_VISIBLE);
    }
    return DefWindowProcW(hwnd, msg, WPARAM(wParam), LPARAM(lParam));
}

} // namespace visor
