// WinRT first: its headers don't take kindly to following windows.h.
#include <unknwn.h>
#include <winrt/base.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>

#include "services/appindex.h"

#include "common/launch.h"
#include "services/registrywatch.h"
#include "services/shelllink.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QMutex>
#include <QRegularExpression>
#include <QSettings>
#include <QWinEventNotifier>

#include <windows.h>
#include <initguid.h> // defines the GUIDs below (FOLDERID_*, BHID_*, PKEY_*) in this file
#include <appmodel.h>
#include <knownfolders.h>
#include <propkey.h>
#include <shellapi.h>
#include <shlguid.h>
#include <shlobj_core.h>
#include <shobjidl_core.h>

// System.AppUserModel.PackageFullName: not in the SDK's propkey.h.
DEFINE_PROPERTYKEY(PKEY_AppUserModel_PackageFullName, 0x9F4C2855, 0x9F79, 0x4B39, 0xA8, 0xD0, 0xE1, 0xD4, 0x2D,
                   0xE1, 0xD5, 0xF3, 21);

#include <algorithm>
#include <thread>

namespace {

constexpr int kFirstIndexDelayMs = 3'000;
constexpr int kRefreshDelayMs = 2'000; // installs touch many files; index once

AppIndex *s_instance = nullptr;

// Shared with the icon provider's threads.
QMutex s_pidlMutex;
QHash<int, QByteArray> s_pidls;
std::atomic<int> s_nextKey = 1;

QByteArray pidlBytes(IShellItem *item)
{
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHGetIDListFromObject(item, &pidl)) || !pidl)
        return {};
    const QByteArray bytes(reinterpret_cast<const char *>(pidl), int(ILGetSize(pidl)));
    CoTaskMemFree(pidl);
    return bytes;
}

QString itemString(IShellItem2 *item, const PROPERTYKEY &key)
{
    LPWSTR value = nullptr;
    if (FAILED(item->GetString(key, &value)) || !value)
        return {};
    const QString s = QString::fromWCharArray(value);
    CoTaskMemFree(value);
    return s;
}

QString displayName(IShellItem *item, SIGDN which)
{
    LPWSTR value = nullptr;
    if (FAILED(item->GetDisplayName(which, &value)) || !value)
        return {};
    const QString s = QString::fromWCharArray(value);
    CoTaskMemFree(value);
    return s;
}

// How to run application `appId` of the package without the shell's help,
// from the package's manifest: by its execution alias (the reparse point
// in %LOCALAPPDATA%\Microsoft\WindowsApps, which gives it its package
// identity) if it declares one, otherwise its executable, if it is a
// full-trust (desktop) app. Empty for UWP apps.
QString packagedLaunchPath(const QString &packageFullName, const QString &appId)
{
    UINT32 length = 0;
    GetPackagePathByFullName(reinterpret_cast<const wchar_t *>(packageFullName.utf16()), &length, nullptr);
    if (!length)
        return {};
    std::wstring buffer(length, L'\0');
    if (GetPackagePathByFullName(reinterpret_cast<const wchar_t *>(packageFullName.utf16()), &length, buffer.data())
        != ERROR_SUCCESS)
        return {};
    const QDir package(QString::fromWCharArray(buffer.c_str()));
    QFile manifest(package.filePath(QStringLiteral("AppxManifest.xml")));
    if (!manifest.open(QIODevice::ReadOnly))
        return {};
    const QString xml = QString::fromUtf8(manifest.readAll());

    // The <Application Id="..."> element for this app.
    const QRegularExpression application(
        QStringLiteral("<Application\\s[^>]*\\bId=\"%1\"[^>]*>(.*?)</Application>").arg(QRegularExpression::escape(appId)),
        QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch m = application.match(xml);
    if (!m.hasMatch())
        return {};
    const QString tag = m.captured(0).left(m.captured(0).indexOf(QLatin1Char('>')) + 1);
    const QString body = m.captured(1);

    // Family name: the full name without its version and architecture.
    const QString family = packageFullName.section(QLatin1Char('_'), 0, 0) + QLatin1Char('_')
                           + packageFullName.section(QLatin1Char('_'), -1);
    const QDir aliases(QDir(qEnvironmentVariable("LOCALAPPDATA")).filePath(QStringLiteral("Microsoft/WindowsApps")));
    static const QRegularExpression alias(QStringLiteral("ExecutionAlias\\s+Alias=\"([^\"]+)\""));
    for (auto it = alias.globalMatch(body); it.hasNext();) {
        const QString path = aliases.filePath(family + QLatin1Char('/') + it.next().captured(1));
        if (QFileInfo::exists(path))
            return QDir::toNativeSeparators(path);
    }

    const bool fullTrust = tag.contains(QLatin1String("Windows.FullTrustApplication"))
                           || tag.contains(QLatin1String("RuntimeBehavior=\"packagedClassicApp\""))
                           || tag.contains(QLatin1String("RuntimeBehavior=\"win32App\""));
    static const QRegularExpression executable(QStringLiteral("\\bExecutable=\"([^\"]+)\""));
    const QRegularExpressionMatch exe = executable.match(tag);
    if (fullTrust && exe.hasMatch())
        return QDir::toNativeSeparators(package.filePath(exe.captured(1)));
    return {};
}

// Everything in shell:AppsFolder. Runs on a worker (STA) thread.
QList<AppIndex::App> enumerateApps()
{
    QList<AppIndex::App> apps;
    IShellItem *folder = nullptr;
    if (FAILED(SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&folder)))) {
        qWarning() << "cannot open shell:AppsFolder";
        return apps;
    }
    IEnumShellItems *items = nullptr;
    if (SUCCEEDED(folder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items)))) {
        IShellItem *item = nullptr;
        while (items->Next(1, &item, nullptr) == S_OK) {
            AppIndex::App app;
            app.name = displayName(item, SIGDN_NORMALDISPLAY);
            app.id = displayName(item, SIGDN_PARENTRELATIVEPARSING);
            app.pidl = pidlBytes(item);
            IShellItem2 *item2 = nullptr;
            if (SUCCEEDED(item->QueryInterface(IID_PPV_ARGS(&item2)))) {
                const QString package = itemString(item2, PKEY_AppUserModel_PackageFullName);
                if (!package.isEmpty()) {
                    app.packaged = true;
                    app.launchPath = packagedLaunchPath(package, app.id.section(QLatin1Char('!'), -1));
                }
                item2->Release();
            }
            item->Release();
            if (!app.name.isEmpty() && !app.pidl.isEmpty())
                apps.append(app);
        }
        items->Release();
    }
    folder->Release();
    return apps;
}

// The icons Desktop icon settings switches, in its order, and whether
// Explorer shows each when the user hasn't chosen (only the Recycle Bin).
struct DesktopIcon
{
    const wchar_t *clsid;
    bool shownByDefault;
};
constexpr DesktopIcon kDesktopIcons[] = {
    {L"{20D04FE0-3AEA-1069-A2D8-08002B30309D}", false}, // This PC
    {L"{59031a47-3f72-44a7-89c5-5595fe6b30ee}", false}, // the user's files
    {L"{F02C1A0D-BE21-4350-88B0-7367FC96EF3C}", false}, // Network
    {L"{645FF040-5081-101B-9F08-00AA002F954E}", true},  // Recycle Bin
    {L"{5399E694-6CE5-4D6C-8FCE-1D8870FDCBA0}", false}, // Control Panel
};
constexpr wchar_t kHideDesktopIcons[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons";

// Where Desktop icon settings keeps its choices: a DWORD per icon, 1 to hide.
bool desktopIconShown(const DesktopIcon &icon)
{
    const std::wstring key = std::wstring(kHideDesktopIcons) + L"\\NewStartPanel";
    DWORD hidden = 0;
    DWORD size = sizeof(hidden);
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), icon.clsid, RRF_RT_REG_DWORD, nullptr, &hidden, &size)
        != ERROR_SUCCESS)
        return icon.shownByDefault;
    return hidden == 0;
}

AppIndex::App desktopItem(IShellItem *item)
{
    AppIndex::App app;
    app.name = displayName(item, SIGDN_NORMALDISPLAY);
    app.id = displayName(item, SIGDN_DESKTOPABSOLUTEPARSING);
    app.pidl = pidlBytes(item);
    app.desktop = true;
    return app;
}

// What Explorer would show on the desktop. Not the shell's Desktop root,
// which also holds the navigation pane's items (Libraries, Home, Gallery):
// the icons switched on, then both Desktop folders merged, folders first.
// Runs on a worker (STA) thread.
QList<AppIndex::App> enumerateDesktop()
{
    QList<AppIndex::App> icons;
    for (const DesktopIcon &icon : kDesktopIcons) {
        if (!desktopIconShown(icon))
            continue;
        const std::wstring path = std::wstring(L"::") + icon.clsid;
        IShellItem *item = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
            icons.append(desktopItem(item));
            item->Release();
        }
    }

    QList<AppIndex::App> folders;
    QList<AppIndex::App> files;
    for (const KNOWNFOLDERID &id : {FOLDERID_Desktop, FOLDERID_PublicDesktop}) {
        // By path: FOLDERID_Desktop's shell item is the namespace root.
        PWSTR path = nullptr;
        if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &path)))
            continue;
        IShellItem *folder = nullptr;
        const HRESULT hr = SHCreateItemFromParsingName(path, nullptr, IID_PPV_ARGS(&folder));
        CoTaskMemFree(path);
        if (FAILED(hr))
            continue;
        IEnumShellItems *items = nullptr;
        if (SUCCEEDED(folder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items)))) {
            IShellItem *item = nullptr;
            while (items->Next(1, &item, nullptr) == S_OK) {
                SFGAOF attributes = 0;
                item->GetAttributes(SFGAO_HIDDEN | SFGAO_FOLDER | SFGAO_STREAM, &attributes);
                if (!(attributes & SFGAO_HIDDEN)) { // desktop.ini
                    // A .zip is a folder to the shell, and a file here.
                    const bool isFolder = (attributes & SFGAO_FOLDER) && !(attributes & SFGAO_STREAM);
                    AppIndex::App app = desktopItem(item);
                    if (!app.name.isEmpty() && !app.pidl.isEmpty())
                        (isFolder ? folders : files).append(app);
                }
                item->Release();
            }
            items->Release();
        }
        folder->Release();
    }
    const auto byName = [](const AppIndex::App &a, const AppIndex::App &b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    };
    std::sort(folders.begin(), folders.end(), byName);
    std::sort(files.begin(), files.end(), byName);
    return icons + folders + files;
}

QSettings historyStore()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("visor"), QStringLiteral("apps"));
}

// QSettings keys can't contain '/'.
QString historyKey(const QString &id)
{
    return QString(id).replace(QLatin1Char('/'), QLatin1Char('|'));
}

} // namespace

// Packages being installed, updated or removed (the Store, winget, Add-AppxPackage).
struct AppIndex::PackageWatch
{
    winrt::Windows::ApplicationModel::PackageCatalog catalog{nullptr};
    winrt::event_token installing;
    winrt::event_token uninstalling;
    winrt::event_token updating;

    ~PackageWatch()
    {
        if (!catalog)
            return;
        catalog.PackageInstalling(installing);
        catalog.PackageUninstalling(uninstalling);
        catalog.PackageUpdating(updating);
    }
};

AppIndex::AppIndex(QObject *parent)
    : QObject(parent)
{
    s_instance = this;
    loadHistory();

    // Not in the way of the bar coming up.
    m_firstIndex.setSingleShot(true);
    m_firstIndex.setInterval(kFirstIndexDelayMs);
    connect(&m_firstIndex, &QTimer::timeout, this, &AppIndex::index);
    m_firstIndex.start();

    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(kRefreshDelayMs);
    connect(&m_refreshTimer, &QTimer::timeout, this, &AppIndex::index);

    watchFolders();
}

AppIndex::~AppIndex()
{
    s_instance = nullptr;
    for (QWinEventNotifier *n : std::as_const(m_folderNotifiers)) {
        const HANDLE handle = n->handle();
        delete n;
        FindCloseChangeNotification(handle);
    }
}

AppIndex *AppIndex::instance()
{
    return s_instance;
}

void AppIndex::ensureIndexed()
{
    if (!m_ready && !m_indexing)
        index();
}

void AppIndex::refresh()
{
    m_refreshTimer.start();
}

void AppIndex::index()
{
    m_firstIndex.stop();
    if (m_indexing) {
        m_indexAgain = true; // once more when this one is done
        return;
    }
    m_indexing = true;
    std::thread([this] {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        QList<App> apps = enumerateApps();
        QList<App> desktop = enumerateDesktop();
        if (SUCCEEDED(hr))
            CoUninitialize();
        QMetaObject::invokeMethod(
            this,
            [this, apps = std::move(apps), desktop = std::move(desktop)]() mutable {
                applyIndex(std::move(apps), std::move(desktop));
            },
            Qt::QueuedConnection);
    }).detach();
}

void AppIndex::applyIndex(QList<App> apps, QList<App> desktop)
{
    // Keep each item's key across rebuilds, so icons already shown stay valid.
    const auto keyOf = [](const App &a) { return (a.desktop ? QStringLiteral("desktop:") : QString()) + a.id; };
    QHash<QString, int> keys;
    for (const QList<App> *list : {&m_apps, &m_desktop}) {
        for (const App &a : *list)
            keys.insert(keyOf(a), a.key);
    }
    QHash<int, QByteArray> pidls;
    for (QList<App> *list : {&apps, &desktop}) {
        for (App &a : *list) {
            a.key = keys.value(keyOf(a), 0);
            if (!a.key)
                a.key = s_nextKey++;
            pidls.insert(a.key, a.pidl);
            const auto h = m_history.constFind(a.id);
            if (h != m_history.cend()) {
                a.launches = h->first;
                a.lastLaunch = h->second;
            }
        }
    }
    std::sort(apps.begin(), apps.end(),
              [](const App &a, const App &b) { return QString::localeAwareCompare(a.name, b.name) < 0; });
    {
        const QMutexLocker lock(&s_pidlMutex);
        s_pidls = pidls;
    }
    m_apps = std::move(apps);
    m_desktop = std::move(desktop);
    m_ready = true;
    m_indexing = false;
    qInfo() << "indexed" << m_apps.size() << "apps and" << m_desktop.size() << "desktop items";
    emit changed();
    if (m_indexAgain) {
        m_indexAgain = false;
        index();
    }
}

void AppIndex::launch(int key, bool asAdmin)
{
    App *app = nullptr;
    for (QList<App> *list : {&m_apps, &m_desktop}) {
        for (App &a : *list) {
            if (a.key == key)
                app = &a;
        }
    }
    if (!app)
        return;
    qInfo().noquote() << "launching" << app->name << (asAdmin ? "as administrator" : "");
    app->launches++;
    app->lastLaunch = QDateTime::currentMSecsSinceEpoch();
    m_history.insert(app->id, {app->launches, app->lastLaunch});
    saveHistory(*app);
    emit changed();

    // Without Explorer the shell can't activate packaged apps; run the
    // app's alias or executable instead (see launchPath).
    const ShellLink *link = ShellLink::instance();
    if (app->packaged && link && link->mode() == QLatin1String("replace")) {
        if (app->launchPath.isEmpty())
            qInfo().noquote() << app->name << "is a UWP app; it needs Explorer";
        else
            visor::shellExecute(app->launchPath, {}, asAdmin);
        return;
    }

    const QByteArray pidl = app->pidl;
    const QString name = app->name;
    // By name, if the item fails: an app the way `explorer
    // shell:AppsFolder\<id>` does it, a desktop item by its path (or
    // shell:::{CLSID}).
    const QString file = !app->desktop                           ? QStringLiteral("shell:AppsFolder\\") + app->id
                         : app->id.startsWith(QLatin1String("::")) ? QStringLiteral("shell:") + app->id
                                                                   : app->id;
    visor::runDetached([pidl, name, file, asAdmin] {
        SHELLEXECUTEINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_IDLIST | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        info.lpIDList = const_cast<char *>(pidl.constData());
        info.lpVerb = asAdmin ? L"runas" : nullptr;
        info.nShow = SW_SHOWNORMAL;
        if (ShellExecuteExW(&info))
            return;
        const DWORD error = GetLastError();
        info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        info.lpIDList = nullptr;
        info.lpFile = reinterpret_cast<const wchar_t *>(file.utf16());
        if (ShellExecuteExW(&info))
            qInfo().noquote() << "launched" << name << "by name (by item: error" << error << ")";
        else
            qWarning().noquote() << "failed to launch" << name << "error" << error << "/" << GetLastError();
    });
}

QByteArray AppIndex::pidlFor(int key)
{
    const QMutexLocker lock(&s_pidlMutex);
    return s_pidls.value(key);
}

void AppIndex::loadHistory()
{
    QSettings s = historyStore();
    s.beginGroup(QStringLiteral("history"));
    const QStringList ids = s.childKeys();
    for (const QString &id : ids) {
        // "launches,last"
        const QStringList v = s.value(id).toString().split(QLatin1Char(','));
        if (v.size() == 2)
            m_history.insert(QString(id).replace(QLatin1Char('|'), QLatin1Char('/')), {v[0].toInt(), v[1].toLongLong()});
    }
}

void AppIndex::saveHistory(const App &app) const
{
    QSettings s = historyStore();
    s.beginGroup(QStringLiteral("history"));
    s.setValue(historyKey(app.id), QStringLiteral("%1,%2").arg(app.launches).arg(app.lastLaunch));
}

void AppIndex::watchFolders()
{
    // Start Menu shortcuts: the user's and the machine's.
    for (const KNOWNFOLDERID &id : {FOLDERID_Programs, FOLDERID_CommonPrograms}) {
        PWSTR path = nullptr;
        if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &path)))
            continue;
        const HANDLE handle = FindFirstChangeNotificationW(
            path, TRUE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
        CoTaskMemFree(path);
        if (handle == INVALID_HANDLE_VALUE)
            continue;
        auto *notifier = new QWinEventNotifier(handle, this);
        connect(notifier, &QWinEventNotifier::activated, this, [this](HANDLE h) {
            FindNextChangeNotification(h);
            refresh();
        });
        m_folderNotifiers.append(notifier);
    }

    // The desktop: both Desktop folders (their top level, which is what
    // the desktop shows) and Desktop icon settings' choices.
    for (const KNOWNFOLDERID &id : {FOLDERID_Desktop, FOLDERID_PublicDesktop}) {
        PWSTR path = nullptr;
        if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &path)))
            continue;
        const HANDLE handle =
            FindFirstChangeNotificationW(path, FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME);
        CoTaskMemFree(path);
        if (handle == INVALID_HANDLE_VALUE)
            continue;
        auto *notifier = new QWinEventNotifier(handle, this);
        connect(notifier, &QWinEventNotifier::activated, this, [this](HANDLE h) {
            FindNextChangeNotification(h);
            refresh();
        });
        m_folderNotifiers.append(notifier);
    }
    m_desktopIconsWatch =
        new RegistryWatch(RegistryWatch::CurrentUser, kHideDesktopIcons, true, [this] { refresh(); }, this);

    // Packaged apps. Each change raises several events as it progresses;
    // the refresh timer folds them into one rebuild.
    try {
        m_packages = std::make_unique<PackageWatch>();
        m_packages->catalog = winrt::Windows::ApplicationModel::PackageCatalog::OpenForCurrentUser();
        const auto changed = [this](auto &&...) {
            QMetaObject::invokeMethod(this, &AppIndex::refresh, Qt::QueuedConnection);
        };
        m_packages->installing = m_packages->catalog.PackageInstalling(changed);
        m_packages->uninstalling = m_packages->catalog.PackageUninstalling(changed);
        m_packages->updating = m_packages->catalog.PackageUpdating(changed);
    } catch (const winrt::hresult_error &e) {
        qWarning() << "package catalog unavailable:" << QString::fromWCharArray(e.message().c_str());
        m_packages.reset();
    }
}
