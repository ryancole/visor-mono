#include "services/privacy.h"

#include "services/registrywatch.h"

#include <QDebug>

#include <windows.h>

namespace {

// Per capability, a key per packaged app (by package family name), and a
// NonPackaged key with one per desktop app (its path, with # for \).
// LastUsedTimeStart and LastUsedTimeStop are FILETIMEs; in use means
// started and not stopped since.
constexpr wchar_t kStore[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\";

quint64 qword(HKEY key, const wchar_t *name)
{
    quint64 value = 0;
    DWORD size = sizeof(value);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_QWORD, nullptr, &value, &size) != ERROR_SUCCESS)
        return 0;
    return value;
}

bool inUse(HKEY key)
{
    const quint64 start = qword(key, L"LastUsedTimeStart");
    const quint64 stop = qword(key, L"LastUsedTimeStop");
    return start != 0 && (stop == 0 || stop < start);
}

// "C:#Program Files#App#app.exe" -> "app"; "Publisher.App_hash" -> "App".
QString appName(const QString &entry, bool packaged)
{
    QString name = entry;
    if (packaged) {
        name = name.section(QLatin1Char('_'), 0, 0);
        name = name.section(QLatin1Char('.'), -1);
    } else {
        name = name.section(QLatin1Char('#'), -1);
        if (name.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
            name.chop(4);
    }
    return name;
}

template<typename F>
void forEachSubkey(HKEY key, F fn)
{
    wchar_t name[256];
    for (DWORD i = 0;; ++i) {
        DWORD len = DWORD(std::size(name));
        if (RegEnumKeyExW(key, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;
        HKEY sub = nullptr;
        if (RegOpenKeyExW(key, name, 0, KEY_READ, &sub) == ERROR_SUCCESS) {
            fn(QString::fromWCharArray(name, int(len)), sub);
            RegCloseKey(sub);
        }
    }
}

QStringList appsUsing(const wchar_t *capability)
{
    QStringList apps;
    HKEY store = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, (QString::fromWCharArray(kStore) + QString::fromWCharArray(capability)).toStdWString().c_str(),
                      0, KEY_READ, &store)
        != ERROR_SUCCESS)
        return apps;
    forEachSubkey(store, [&](const QString &name, HKEY key) {
        if (name == QLatin1String("NonPackaged")) {
            forEachSubkey(key, [&](const QString &entry, HKEY app) {
                if (inUse(app))
                    apps << appName(entry, false);
            });
        } else if (inUse(key)) {
            apps << appName(name, true);
        }
    });
    RegCloseKey(store);
    apps.removeDuplicates();
    return apps;
}

} // namespace

struct Privacy::Impl
{
    std::unique_ptr<RegistryWatch> microphone;
    std::unique_ptr<RegistryWatch> camera;
    std::unique_ptr<RegistryWatch> location;
};

Privacy::Privacy(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Impl>())
{
    const auto changed = [this] { refresh(); };
    const auto watch = [&](const wchar_t *capability) {
        const std::wstring path = std::wstring(kStore) + capability;
        return std::make_unique<RegistryWatch>(RegistryWatch::CurrentUser, path.c_str(), true, changed);
    };
    d->microphone = watch(L"microphone");
    d->camera = watch(L"webcam");
    d->location = watch(L"location");
    refresh();
}

Privacy::~Privacy() = default;

void Privacy::refresh()
{
    const QStringList microphone = appsUsing(L"microphone");
    const QStringList camera = appsUsing(L"webcam");
    const QStringList location = appsUsing(L"location");
    if (microphone == m_microphone && camera == m_camera && location == m_location)
        return;
    m_microphone = microphone;
    m_camera = camera;
    m_location = location;
    qInfo().noquote() << "privacy: microphone" << microphone.join(QLatin1Char(',')) << "camera"
                      << camera.join(QLatin1Char(',')) << "location" << location.join(QLatin1Char(','));
    emit changed();
}
