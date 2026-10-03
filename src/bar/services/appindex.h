#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include <atomic>
#include <memory>

class QWinEventNotifier;
class RegistryWatch;

// The installed apps, as the Start menu would list them: shell:AppsFolder,
// which merges the Start Menu shortcuts (both users' and the machine's) and
// the packaged apps. Also what Explorer would show on the desktop: the
// icons switched on in Desktop icon settings, then the user's and the
// public Desktop folders. Process-wide (owned by App) so it survives config
// reloads; QML sees it through the `Apps` and `DesktopItems` models.
//
// Indexing runs on a worker thread, first a few seconds after start (or at
// once if something asks), then again whenever a Start Menu or Desktop
// folder, the desktop icon settings or a package changes. Each app keeps its shell item's PIDL, which is what the
// icon provider and launches use; the item itself is re-created on whatever
// thread needs it.
class AppIndex : public QObject
{
    Q_OBJECT

public:
    struct App
    {
        int key = 0;        // stable for the process; image://visor-app-icon/<key>
        QString name;
        // AppUserModelID or the AppsFolder parsing name; for a desktop item,
        // its path or ::{CLSID}.
        QString id;
        QByteArray pidl;    // absolute ITEMIDLIST, for SHCreateItemFromIDList
        bool packaged = false;
        bool desktop = false; // in desktop(), not apps()
        // Packaged apps can't be started through the shell without Explorer
        // ("class not registered"). Those that run as ordinary processes
        // (Terminal, Store Notepad) can be run by their execution alias or
        // executable instead, which this is; empty for UWP apps (Settings,
        // Calculator), which also need Explorer's immersive shell for their
        // window. Used in replace mode only.
        QString launchPath;
        int launches = 0;
        qint64 lastLaunch = 0; // ms since the epoch
    };

    explicit AppIndex(QObject *parent = nullptr);
    ~AppIndex() override;

    static AppIndex *instance();

    const QList<App> &apps() const { return m_apps; }
    // The desktop's items, in Explorer's order: the desktop icons, then
    // folders, then files, each A-Z.
    const QList<App> &desktop() const { return m_desktop; }
    // True once the first index has been built.
    bool ready() const { return m_ready; }
    // Builds the index now if it hasn't been yet.
    void ensureIndexed();
    void refresh();

    // Opens the app or desktop item on its own thread (a UWP launch can hang for a long
    // time; nothing waits for it). asAdmin: the "Run as administrator" verb,
    // like Ctrl+Shift+Enter in Start.
    void launch(int key, bool asAdmin = false);

    // The app's PIDL, from any thread; empty if unknown.
    static QByteArray pidlFor(int key);

signals:
    // The list was (re)built, or launch history changed.
    void changed();

private:
    void index();
    void applyIndex(QList<App> apps, QList<App> desktop);
    void loadHistory();
    void saveHistory(const App &app) const;
    void watchFolders();

    QList<App> m_apps;
    QList<App> m_desktop;
    bool m_ready = false;
    bool m_indexing = false;
    bool m_indexAgain = false;
    QTimer m_firstIndex;
    QTimer m_refreshTimer;
    QList<QWinEventNotifier *> m_folderNotifiers;
    RegistryWatch *m_desktopIconsWatch = nullptr;
    QHash<QString, QPair<int, qint64>> m_history; // id -> launches, last launch
    struct PackageWatch;
    std::unique_ptr<PackageWatch> m_packages;
};
