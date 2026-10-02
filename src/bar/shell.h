#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

#include <memory>

class QQmlEngine;

// Loads the user's root QML file and hot-reloads it when a QML file in the
// config directory (.qml, .js, .mjs, qmldir) changes, appears or goes away.
// Other files there, such as visor-wm's wm.conf, don't reload the bar.
//
// Each load is a "generation" with its own QQmlEngine. The new generation is
// compiled while the old one keeps running; only if it compiles cleanly is the
// old one torn down and the new one instantiated. A broken edit therefore
// leaves the current bar on screen instead of blanking it.
class Shell : public QObject
{
    Q_OBJECT

public:
    explicit Shell(QString configPath, QObject *parent = nullptr);
    ~Shell() override;

    void load();
    // Destroys the running config (closing its windows).
    void unload();

private:
    struct Generation;

    // Called (debounced) on any watcher event: reloads only if the QML files
    // differ from the last load.
    void reloadIfChanged();
    void watchConfigDir();
    QStringList qmlFiles() const;
    QStringList snapshot(const QStringList &files) const;

    QString m_configPath;
    QString m_configDir;
    std::unique_ptr<Generation> m_current;
    QFileSystemWatcher m_watcher;
    QTimer m_reloadTimer;
    QStringList m_snapshot; // "path|mtime|size" of the QML files, sorted
    int m_generationCount = 0;
};
