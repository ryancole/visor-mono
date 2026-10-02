#include "app.h"

#include "services/shelllink.h"
#include "tray.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>
#include <QUrl>

namespace {

App *s_instance = nullptr;

// Arguments for a relaunch, minus any --renderer override so the saved
// preference takes effect.
QStringList relaunchArguments()
{
    QStringList args = QCoreApplication::arguments().mid(1);
    for (int i = 0; i < args.size();) {
        const QString &a = args.at(i);
        if (a == "--renderer" || a == "-renderer") {
            args.remove(i, qMin(2, int(args.size() - i)));
        } else if (a.startsWith("--renderer=")) {
            args.remove(i);
        } else {
            ++i;
        }
    }
    return args;
}

} // namespace

App::App(QString configPath, Settings::Renderer activeRenderer, QObject *parent)
    : QObject(parent)
    , m_configPath(std::move(configPath))
    , m_activeRenderer(activeRenderer)
    , m_preferredRenderer(Settings::renderer())
    , m_shell(m_configPath)
{
    s_instance = this;
}

App::~App()
{
    s_instance = nullptr;
}

App *App::instance()
{
    return s_instance;
}

void App::start()
{
    m_tray = std::make_unique<TrayIcon>(this);
    // Before the config loads, so QML types built on it start populated.
    m_shellLink = std::make_unique<ShellLink>();
    connect(m_shellLink.get(), &ShellLink::quitRequested, this, &App::quit);
    m_shell.load();
}

void App::setPreferredRenderer(Settings::Renderer renderer)
{
    if (renderer == m_preferredRenderer)
        return;
    m_preferredRenderer = renderer;
    Settings::setRenderer(renderer);
    emit preferredRendererChanged();
    if (renderer != m_activeRenderer)
        restart();
}

void App::reload()
{
    QTimer::singleShot(0, &m_shell, &Shell::load);
}

void App::restart()
{
    QTimer::singleShot(0, this, [this] {
        // Close our bars first so their reserved screen space is released
        // before the new process claims it.
        m_shell.unload();
        m_tray.reset();
        QProcess::startDetached(QCoreApplication::applicationFilePath(), relaunchArguments());
        QCoreApplication::quit();
    });
}

void App::quit()
{
    QTimer::singleShot(0, this, [this] {
        m_shell.unload();
        m_tray.reset();
        QCoreApplication::quit();
    });
}

void App::openConfigFolder() const
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_configPath).absolutePath()));
}
