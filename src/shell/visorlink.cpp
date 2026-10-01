#include "shell/visorlink.h"

#include "common/linkprotocol.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QWinEventNotifier>

#include <windows.h>

namespace visor {

namespace {

constexpr int kMaxCrashes = 5;
constexpr qint64 kCrashWindowMs = 120'000;
constexpr int kRestartDelayMs = 1'000;
constexpr int kSuccessorWaitMs = 5'000;
constexpr int kConnectWaitMs = 1'500;
constexpr UINT kSendTimeoutMs = 1'000;

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE) {
        const auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    if (auto *self = reinterpret_cast<VisorLink *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
        return self->handleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

const wchar_t *windowClass()
{
    static const ATOM atom = [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = windowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = link::kShellLinkClass;
        return RegisterClassExW(&wc);
    }();
    return atom ? link::kShellLinkClass : nullptr;
}

QString visorPath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("visor/visor.exe"));
}

} // namespace

VisorLink::VisorLink(QObject *parent)
    : QObject(parent)
{
    m_clock.start();
    m_successorTimer.setSingleShot(true);
    m_successorTimer.setInterval(kSuccessorWaitMs);
    connect(&m_successorTimer, &QTimer::timeout, this,
            [] { qInfo() << "Visor quit and no new instance connected; not restarting it"; });

    // Top-level (hidden) so Visor can find it with FindWindow.
    const HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, windowClass(), L"visor-shell link", WS_POPUP, 0, 0, 0, 0,
                                      nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd)
        qCritical() << "failed to create link window, error" << GetLastError();
    m_hwnd = hwnd;
}

VisorLink::~VisorLink()
{
    delete m_notifier; // before its handle (see onProcessExited)
    if (m_process)
        CloseHandle(static_cast<HANDLE>(m_process));
    if (m_hwnd)
        DestroyWindow(static_cast<HWND>(m_hwnd));
}

void VisorLink::start(bool launchVisor)
{
    m_supervise = launchVisor;
    // A Visor that is already running reconnects on this broadcast.
    SendNotifyMessageW(HWND_BROADCAST, RegisterWindowMessageW(link::kShellCreatedMessage), 0, 0);
    if (!launchVisor)
        return;
    QTimer::singleShot(kConnectWaitMs, this, [this] {
        if (!m_client && !m_process)
            launch();
    });
}

void VisorLink::stopVisor()
{
    m_stopping = true;
    m_successorTimer.stop();
    if (m_client)
        send({{QStringLiteral("type"), QStringLiteral("quit")}});
}

void VisorLink::launch()
{
    const QString path = visorPath();
    if (!QFileInfo::exists(path)) {
        qInfo() << "Visor not deployed at" << path;
        return;
    }
    qint64 pid = 0;
    if (!QProcess::startDetached(path, {}, QFileInfo(path).absolutePath(), &pid)) {
        qWarning() << "failed to start Visor at" << path;
        return;
    }
    qInfo() << "started Visor, pid" << pid;
    watch(DWORD(pid));
}

void VisorLink::watch(unsigned long pid)
{
    if (pid == m_pid && m_process)
        return;
    delete m_notifier;
    m_notifier = nullptr;
    if (m_process)
        CloseHandle(static_cast<HANDLE>(m_process));

    m_pid = pid;
    m_process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!m_process) {
        qWarning() << "cannot watch Visor process" << pid << "error" << GetLastError();
        return;
    }
    m_notifier = new QWinEventNotifier(m_process, this);
    connect(m_notifier, &QWinEventNotifier::activated, this, &VisorLink::onProcessExited);
}

void VisorLink::onProcessExited()
{
    DWORD code = 0;
    GetExitCodeProcess(static_cast<HANDLE>(m_process), &code);
    qInfo() << "Visor (pid" << m_pid << ") exited with code" << code;

    // We're inside the notifier's own signal: it can only be deleted later,
    // and its thread-pool wait still references the handle until then, so
    // the handle must outlive it (closing it first crashes with
    // STATUS_THREADPOOL_HANDLE_EXCEPTION).
    const HANDLE process = static_cast<HANDLE>(m_process);
    m_notifier->setEnabled(false);
    connect(m_notifier, &QObject::destroyed, [process] { CloseHandle(process); });
    m_notifier->deleteLater();
    m_notifier = nullptr;
    m_process = nullptr;
    m_pid = 0;
    if (m_client) {
        m_client = nullptr;
        emit clientDisconnected();
    }

    if (m_stopping || !m_supervise)
        return;
    if (code == 0) {
        // Quit, or restarting itself: give a successor time to connect.
        m_successorTimer.start();
        return;
    }

    const qint64 now = m_clock.elapsed();
    m_crashTimes.append(now);
    while (!m_crashTimes.isEmpty() && now - m_crashTimes.first() > kCrashWindowMs)
        m_crashTimes.removeFirst();
    if (m_crashTimes.size() >= kMaxCrashes) {
        qWarning() << "Visor crashed" << m_crashTimes.size() << "times in 2 minutes; giving up";
        return;
    }
    QTimer::singleShot(kRestartDelayMs, this, [this] {
        if (!m_client && !m_process && !m_stopping)
            launch();
    });
}

void VisorLink::send(const QJsonObject &message)
{
    if (!m_client)
        return;
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    COPYDATASTRUCT cds{};
    cds.dwData = link::kLinkMagic;
    cds.cbData = DWORD(payload.size());
    cds.lpData = const_cast<char *>(payload.constData());
    DWORD_PTR result = 0;
    if (!SendMessageTimeoutW(static_cast<HWND>(m_client), WM_COPYDATA, WPARAM(m_hwnd), LPARAM(&cds),
                             SMTO_ABORTIFHUNG | SMTO_BLOCK, kSendTimeoutMs, &result)) {
        qWarning() << "Visor did not take" << message.value(QStringLiteral("type")).toString() << "error"
                   << GetLastError();
    }
}

std::intptr_t VisorLink::handleMessage(void *window, unsigned msg, std::uintptr_t wParam, std::intptr_t lParam)
{
    const auto hwnd = static_cast<HWND>(window);
    if (msg != WM_COPYDATA)
        return DefWindowProcW(hwnd, msg, WPARAM(wParam), LPARAM(lParam));

    const auto *cds = reinterpret_cast<const COPYDATASTRUCT *>(lParam);
    if (!cds || cds->dwData != link::kLinkMagic)
        return FALSE;
    const QJsonObject message =
        QJsonDocument::fromJson(QByteArray(static_cast<const char *>(cds->lpData), int(cds->cbData))).object();
    const QString type = message.value(QStringLiteral("type")).toString();

    if (type == QLatin1String("hello")) {
        const auto client = reinterpret_cast<HWND>(wParam);
        DWORD pid = 0;
        GetWindowThreadProcessId(client, &pid);
        qInfo() << "Visor connected, pid" << pid << "version" << message.value(QStringLiteral("version")).toString();
        m_client = client;
        m_successorTimer.stop();
        watch(pid);
        // Reply from the event loop, not from inside Visor's SendMessage.
        QMetaObject::invokeMethod(this, &VisorLink::clientConnected, Qt::QueuedConnection);
        return TRUE;
    }
    return FALSE;
}

} // namespace visor
