#include "shell.h"

#include "services/windowicons.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QUrl>

struct Shell::Generation
{
    std::unique_ptr<QQmlEngine> engine;
    std::unique_ptr<QObject> root;

    ~Generation()
    {
        // Objects must go before the engine that created them.
        root.reset();
        engine.reset();
    }
};

Shell::Shell(QString configPath, QObject *parent)
    : QObject(parent)
    , m_configPath(std::move(configPath))
    , m_configDir(QFileInfo(m_configPath).absolutePath())
{
    // Editors often write a file as several events (truncate, write, rename);
    // coalesce them into one reload.
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(150);
    connect(&m_reloadTimer, &QTimer::timeout, this, &Shell::load);

    const auto changed = [this] { m_reloadTimer.start(); };
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, changed);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, changed);
}

Shell::~Shell()
{
    unload();
}

void Shell::unload()
{
    m_current.reset();
}

void Shell::load()
{
    // Re-arm the watcher first: atomic saves replace the file, which drops it
    // from the watch list.
    watchConfigDir();

    QElapsedTimer timer;
    timer.start();
    const int number = ++m_generationCount;

    auto next = std::make_unique<Generation>();
    next->engine = std::make_unique<QQmlEngine>();
    next->engine->addImportPath(m_configDir);
    next->engine->addImageProvider(QStringLiteral("visor-window-icon"), new WindowIconProvider);
    connect(next->engine.get(), &QQmlEngine::quit, QCoreApplication::instance(), &QCoreApplication::quit,
            Qt::QueuedConnection);
    connect(next->engine.get(), &QQmlEngine::exit, QCoreApplication::instance(), &QCoreApplication::exit,
            Qt::QueuedConnection);

    QQmlComponent component(next->engine.get(), QUrl::fromLocalFile(m_configPath),
                            QQmlComponent::PreferSynchronous);
    if (component.isError()) {
        qWarning().noquote() << "visor: config failed to compile; keeping the running config.\n"
                             << component.errorString().trimmed();
        return;
    }

    // Swap generations. The old one is destroyed before the new one is
    // created so docked bars release their reserved screen space first.
    unload();

    next->root.reset(component.create());
    if (!next->root) {
        qWarning().noquote() << "visor: config failed to load.\n" << component.errorString().trimmed();
        return;
    }

    m_current = std::move(next);
    qInfo().noquote() << QStringLiteral("visor: loaded %1 (generation %2, %3 ms)")
                             .arg(QDir::toNativeSeparators(m_configPath))
                             .arg(number)
                             .arg(timer.elapsed());
}

void Shell::watchConfigDir()
{
    const QStringList files = m_watcher.files();
    const QStringList dirs = m_watcher.directories();
    if (!files.isEmpty())
        m_watcher.removePaths(files);
    if (!dirs.isEmpty())
        m_watcher.removePaths(dirs);

    QStringList paths{m_configDir};
    QDirIterator it(m_configDir, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info = it.nextFileInfo();
        if (info.isDir() || info.suffix().compare("qml", Qt::CaseInsensitive) == 0
            || info.suffix().compare("js", Qt::CaseInsensitive) == 0
            || info.suffix().compare("mjs", Qt::CaseInsensitive) == 0
            || info.fileName().compare("qmldir", Qt::CaseInsensitive) == 0) {
            paths << info.absoluteFilePath();
        }
    }
    m_watcher.addPaths(paths);
}
