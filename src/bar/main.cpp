#include "app.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQuickWindow>

#include <windows.h>

#include <cstdio>

namespace {

// visor is a GUI-subsystem app, so it has no console of its own. When launched
// from a terminal, attach to it so qDebug/QML warnings show up there.
void attachParentConsole()
{
    // Leave already-redirected output (pipes, files) alone.
    const HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    if (err && err != INVALID_HANDLE_VALUE)
        return;
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        return;
    FILE *f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
}

// Config lookup order: --config, %VISOR_CONFIG%, ~/.config/visor/shell.qml,
// the in-repo config (debug builds), then the config shipped next to the exe.
QString resolveConfig(const QString &explicitPath)
{
    if (!explicitPath.isEmpty())
        return QFileInfo(explicitPath).absoluteFilePath();

    const QString env = qEnvironmentVariable("VISOR_CONFIG");
    if (!env.isEmpty())
        return QFileInfo(env).absoluteFilePath();

    QStringList candidates{QDir::home().filePath(".config/visor/shell.qml")};
#ifdef VISOR_DEV_CONFIG_DIR
    candidates << QStringLiteral(VISOR_DEV_CONFIG_DIR "/shell.qml");
#endif
    candidates << QDir(QCoreApplication::applicationDirPath()).filePath("config/shell.qml");

    for (const QString &path : candidates) {
        if (QFileInfo::exists(path))
            return QFileInfo(path).absoluteFilePath();
    }
    return {};
}

} // namespace

int main(int argc, char *argv[])
{
    attachParentConsole();

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("visor");
    QGuiApplication::setApplicationVersion(VISOR_VERSION);
    // Bars are windows, but closing one shouldn't end the process; quitting is
    // explicit (Qt.quit() from QML, or the process being terminated).
    QGuiApplication::setQuitOnLastWindowClosed(false);

    // Allow transparent bar backgrounds.
    QQuickWindow::setDefaultAlphaBuffer(true);

    QCommandLineParser parser;
    parser.setApplicationDescription("A QML status bar for Windows.");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption configOpt({"c", "config"}, "Path to the root QML file.", "path");
    QCommandLineOption rendererOpt("renderer",
                                   "Override the saved renderer for this run: cpu (lowest memory) or gpu "
                                   "(Direct3D 11, needed for shader effects).",
                                   "cpu|gpu");
    parser.addOption(configOpt);
    parser.addOption(rendererOpt);
    parser.process(app);

    // Renderer precedence: --renderer, then Qt's own env vars, then the saved
    // setting (default CPU). A bar is mostly static text redrawn a few times
    // a minute, which the software rasterizer handles trivially; skipping the
    // GPU path avoids loading the D3D/driver stack (~85 MB -> ~27 MB private
    // memory, 70 -> 13 threads). It must be chosen before any window exists,
    // which is why changing it restarts visor.
    Settings::Renderer renderer = Settings::renderer();
    if (parser.isSet(rendererOpt)) {
        if (!Settings::parseRenderer(parser.value(rendererOpt), &renderer)) {
            qCritical("visor: --renderer must be 'cpu' or 'gpu'");
            return 1;
        }
        QQuickWindow::setGraphicsApi(renderer == Settings::Renderer::Gpu ? QSGRendererInterface::Direct3D11
                                                                         : QSGRendererInterface::Software);
    } else if (!qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND") || !qEnvironmentVariableIsEmpty("QSG_RHI_BACKEND")) {
        renderer = qEnvironmentVariable("QT_QUICK_BACKEND") == "software" ? Settings::Renderer::Software
                                                                          : Settings::Renderer::Gpu;
    } else {
        QQuickWindow::setGraphicsApi(renderer == Settings::Renderer::Gpu ? QSGRendererInterface::Direct3D11
                                                                         : QSGRendererInterface::Software);
    }

    const QString config = resolveConfig(parser.value(configOpt));
    if (config.isEmpty() || !QFileInfo::exists(config)) {
        qCritical("visor: no config found (looked for ~/.config/visor/shell.qml). "
                  "Pass --config <file> or set VISOR_CONFIG.");
        return 1;
    }

    App visor(config, renderer);
    visor.start();
    return app.exec();
}