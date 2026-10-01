#include "services/shelllink.h"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QWinEventNotifier>

#include <windows.h>

namespace {

// Keep in sync with visor-shell/src/common/linkprotocol.h.
constexpr ULONG_PTR kLinkMagic = 0x56534C31; // 'VSL1'
constexpr wchar_t kShellLinkClass[] = L"VisorShellLink";
const UINT kShellCreated = RegisterWindowMessageW(L"VisorShellCreated");

ShellLink *s_instance = nullptr;

LRESULT CALLBACK linkWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (auto *self = reinterpret_cast<ShellLink *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        if (self->handleMessage(msg, wParam, lParam))
            return msg == WM_COPYDATA ? TRUE : 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

const wchar_t *windowClass()
{
    static const wchar_t *name = [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = linkWindowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"visor.ShellLink";
        RegisterClassExW(&wc);
        return wc.lpszClassName;
    }();
    return name;
}

ShellLink::Task taskFromJson(const QJsonObject &o)
{
    ShellLink::Task t;
    t.hwnd = quintptr(o.value("hwnd").toInteger());
    t.title = o.value("title").toString();
    t.pid = quint32(o.value("pid").toInteger());
    t.path = o.value("path").toString();
    t.flashing = o.value("flashing").toBool();
    return t;
}

} // namespace

ShellLink::ShellLink(QObject *parent)
    : QObject(parent)
{
    s_instance = this;

    // A hidden top-level window rather than a message-only one: message-only
    // windows don't receive the VisorShellCreated broadcast.
    const HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, windowClass(), L"visor link", WS_POPUP, 0, 0, 0, 0, nullptr,
                                      nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    m_hwnd = hwnd;
    hello();
}

ShellLink::~ShellLink()
{
    delete m_shellExit;
    if (m_shellProcess)
        CloseHandle(static_cast<HANDLE>(m_shellProcess));
    DestroyWindow(static_cast<HWND>(m_hwnd));
    s_instance = nullptr;
}

ShellLink *ShellLink::instance()
{
    return s_instance;
}

void ShellLink::hello()
{
    const HWND shell = FindWindowW(kShellLinkClass, nullptr);
    if (!shell)
        return;

    const QByteArray payload =
        QJsonDocument(QJsonObject{{"type", "hello"}, {"version", QCoreApplication::applicationVersion()}})
            .toJson(QJsonDocument::Compact);
    COPYDATASTRUCT cds{};
    cds.dwData = kLinkMagic;
    cds.cbData = DWORD(payload.size());
    cds.lpData = const_cast<char *>(payload.constData());
    DWORD_PTR result = 0;
    if (!SendMessageTimeoutW(shell, WM_COPYDATA, WPARAM(m_hwnd), LPARAM(&cds), SMTO_ABORTIFHUNG, 1000, &result)
        || !result) {
        qWarning() << "visor: visor-shell did not answer hello";
        return;
    }

    // Watch the shell process so a crash or exit doesn't leave stale tasks.
    disconnect();
    DWORD pid = 0;
    GetWindowThreadProcessId(shell, &pid);
    m_shellProcess = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (m_shellProcess) {
        m_shellExit = new QWinEventNotifier(m_shellProcess, this);
        connect(m_shellExit, &QWinEventNotifier::activated, this, [this] {
            qInfo() << "visor: visor-shell went away";
            disconnect();
        });
    }
    m_shell = shell;
    qInfo() << "visor: connected to visor-shell";
    emit connectedChanged();
}

void ShellLink::disconnect()
{
    // May run inside the notifier's own signal, so it is deleted later; its
    // thread-pool wait references the handle until then, so close the handle
    // only once the notifier is gone.
    if (m_shellExit) {
        const HANDLE process = static_cast<HANDLE>(m_shellProcess);
        m_shellExit->setEnabled(false);
        connect(m_shellExit, &QObject::destroyed, [process] { CloseHandle(process); });
        m_shellExit->deleteLater();
        m_shellExit = nullptr;
    } else if (m_shellProcess) {
        CloseHandle(static_cast<HANDLE>(m_shellProcess));
    }
    m_shellProcess = nullptr;
    if (!m_shell)
        return;
    m_shell = nullptr;
    m_tasks.clear();
    m_active = 0;
    emit tasksReset();
    emit activeTaskChanged();
    emit connectedChanged();
}

bool ShellLink::handleMessage(unsigned msg, unsigned long long wParam, long long lParam)
{
    if (msg == kShellCreated) {
        hello(); // visor-shell (re)started
        return true;
    }
    if (msg != WM_COPYDATA)
        return false;
    const auto *cds = reinterpret_cast<const COPYDATASTRUCT *>(lParam);
    if (!cds || cds->dwData != kLinkMagic)
        return false;
    // Only the shell we said hello to.
    if (reinterpret_cast<HWND>(wParam) != static_cast<HWND>(m_shell) && m_shell)
        return false;
    onMessage(QByteArray(static_cast<const char *>(cds->lpData), int(cds->cbData)));
    return true;
}

void ShellLink::onMessage(const QByteArray &json)
{
    const QJsonObject m = QJsonDocument::fromJson(json).object();
    const QString type = m.value("type").toString();

    if (type == "tasks.reset") {
        m_tasks.clear();
        for (const QJsonValue &v : m.value("tasks").toArray())
            m_tasks.append(taskFromJson(v.toObject()));
        m_active = quintptr(m.value("active").toInteger());
        emit tasksReset();
        emit activeTaskChanged();
    } else if (type == "task.added") {
        const Task t = taskFromJson(m.value("task").toObject());
        m_tasks.append(t);
        emit taskAdded(t);
    } else if (type == "task.changed") {
        const Task t = taskFromJson(m.value("task").toObject());
        for (Task &existing : m_tasks) {
            if (existing.hwnd == t.hwnd) {
                existing = t;
                emit taskChanged(t);
                break;
            }
        }
    } else if (type == "task.removed") {
        const auto hwnd = quintptr(m.value("hwnd").toInteger());
        for (qsizetype i = 0; i < m_tasks.size(); ++i) {
            if (m_tasks[i].hwnd == hwnd) {
                m_tasks.removeAt(i);
                emit taskRemoved(hwnd);
                break;
            }
        }
    } else if (type == "task.activated") {
        m_active = quintptr(m.value("hwnd").toInteger());
        emit activeTaskChanged();
    } else if (type == "quit") {
        emit quitRequested();
    }
}
