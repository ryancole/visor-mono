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

// The installed apps, as the Start menu would list them: shell:AppsFolder,
// which merges the Start Menu shortcuts (both users' and the machine's) and
// the packaged apps. Process-wide (owned by App) so it survives config
// reloads; QML sees it through the `Apps` model.
//
// Indexing runs on a worker thread, first a few seconds after start (or at
// once if something asks), then again whenever a Start Menu folder or a
// package changes. Each app keeps its shell item's PIDL, which is what the
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
        QString id;         // AppUserModelID or the AppsFolder parsing name
        QByteArray pidl;    // absolute ITEMIDLIST, for SHCreateItemFromIDList
        bool packaged = false;
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
    // True once the first index has been built.
    bool ready() const { return m_ready; }
    // Builds the index now if it hasn't been yet.
    void ensureIndexed();
    void refresh();

    // Opens the app on its own thread (a UWP launch can hang for a long
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
    void applyIndex(QList<App> apps);
    void loadHistory();
    void saveHistory(const App &app) const;
    void watchFolders();

    QList<App> m_apps;
    bool m_ready = false;
    bool m_indexing = false;
    bool m_indexAgain = false;
    QTimer m_firstIndex;
    QTimer m_refreshTimer;
    QList<QWinEventNotifier *> m_folderNotifiers;
    QHash<QString, QPair<int, qint64>> m_history; // id -> launches, last launch
    struct PackageWatch;
    std::unique_ptr<PackageWatch> m_packages;
};
