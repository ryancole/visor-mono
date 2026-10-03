#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <thread>

// Calls back (on the GUI thread) when a registry key, or its subtree,
// changes: the way to follow a Windows setting without polling it.
// RegNotifyChangeKeyValue signals an event, which a thread of its own waits
// on; re-armed after each change. Not a QML type; services use it.
//
//   RegistryWatch watch(RegistryWatch::CurrentUser, L"Software\\...", false, [this] { refresh(); });
class RegistryWatch : public QObject
{
    Q_OBJECT

public:
    enum Root { CurrentUser, LocalMachine };

    RegistryWatch(Root root, const wchar_t *path, bool subtree, std::function<void()> onChange,
                  QObject *parent = nullptr);
    ~RegistryWatch() override;

    // False when the key couldn't be opened (it doesn't exist, or no access).
    bool valid() const { return m_key != nullptr; }

private:
    void arm();
    void wait();

    void *m_key = nullptr;   // HKEY
    void *m_event = nullptr; // HANDLE: signalled by the registry
    void *m_stop = nullptr;  // HANDLE: signalled by the destructor
    bool m_subtree = false;
    QString m_path;
    std::thread m_thread;
    std::function<void()> m_onChange;
};
