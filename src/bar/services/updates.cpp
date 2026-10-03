#include "services/updates.h"

#include "services/registrywatch.h"

#include <QDebug>

#include <windows.h>

namespace {

// Windows Update marks a pending restart with a subkey; so does the
// servicing stack (CBS) for its own. Explorer's icon reads the same.
constexpr wchar_t kAutoUpdate[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update";
constexpr wchar_t kServicing[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing";

bool keyExists(const wchar_t *path)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;
    RegCloseKey(key);
    return true;
}

} // namespace

struct Updates::Impl
{
    std::unique_ptr<RegistryWatch> autoUpdate;
    std::unique_ptr<RegistryWatch> servicing;
};

Updates::Updates(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Impl>())
{
    const auto changed = [this] { refresh(); };
    d->autoUpdate = std::make_unique<RegistryWatch>(RegistryWatch::LocalMachine, kAutoUpdate, true, changed);
    d->servicing = std::make_unique<RegistryWatch>(RegistryWatch::LocalMachine, kServicing, false, changed);
    refresh();
}

Updates::~Updates() = default;

void Updates::refresh()
{
    const bool required = keyExists(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update\\RebootRequired")
                          || keyExists(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing\\RebootPending");
    if (required == m_restartRequired)
        return;
    m_restartRequired = required;
    qInfo() << "updates: restart" << (required ? "required" : "not required");
    emit changed();
}
