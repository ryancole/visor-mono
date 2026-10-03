#include "services/registrywatch.h"

#include <QDebug>
#include <QMetaObject>

#include <windows.h>

RegistryWatch::RegistryWatch(Root root, const wchar_t *path, bool subtree, std::function<void()> onChange,
                             QObject *parent)
    : QObject(parent)
    , m_subtree(subtree)
    , m_path(QString::fromWCharArray(path))
    , m_onChange(std::move(onChange))
{
    HKEY key = nullptr;
    const LSTATUS status =
        RegOpenKeyExW(root == CurrentUser ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE, path, 0, KEY_NOTIFY, &key);
    if (status != ERROR_SUCCESS) {
        qInfo().noquote() << "RegistryWatch: cannot open" << m_path << "error" << status;
        return;
    }
    m_key = key;
    m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    m_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    arm();
    m_thread = std::thread([this] { wait(); });
}

RegistryWatch::~RegistryWatch()
{
    if (m_thread.joinable()) {
        SetEvent(m_stop);
        m_thread.join();
    }
    // Closing the key cancels the pending notification.
    if (m_key)
        RegCloseKey(static_cast<HKEY>(m_key));
    if (m_event)
        CloseHandle(m_event);
    if (m_stop)
        CloseHandle(m_stop);
}

void RegistryWatch::arm()
{
    // Thread-agnostic: the registration outlives the calling thread's
    // state, and the event is signalled wherever it is waited on.
    const DWORD filter = REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET | REG_NOTIFY_THREAD_AGNOSTIC;
    const LSTATUS status =
        RegNotifyChangeKeyValue(static_cast<HKEY>(m_key), m_subtree ? TRUE : FALSE, filter, m_event, TRUE);
    if (status != ERROR_SUCCESS)
        qWarning().noquote() << "RegistryWatch: RegNotifyChangeKeyValue failed for" << m_path << "error" << status;
}

// The waiting thread: each change is handed to the GUI thread, which re-arms
// (before the callback, so a change the callback causes is seen too).
void RegistryWatch::wait()
{
    const HANDLE handles[] = {m_event, m_stop};
    for (;;) {
        const DWORD result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (result != WAIT_OBJECT_0)
            return; // stop, or an error
        QMetaObject::invokeMethod(
            this,
            [this] {
                qInfo().noquote() << "RegistryWatch: change under" << m_path;
                arm();
                m_onChange();
            },
            Qt::QueuedConnection);
    }
}
