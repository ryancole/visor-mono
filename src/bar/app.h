#pragma once

#include "settings.h"
#include "shell.h"

#include <QObject>
#include <QStringList>

#include <memory>

class TrayIcon;

// Process-wide controller: owns the QML shell and the tray icon, and applies
// app-level settings. QML reaches it through the `Visor` singleton.
class App : public QObject
{
    Q_OBJECT

public:
    App(QString configPath, Settings::Renderer activeRenderer, QObject *parent = nullptr);
    ~App() override;

    static App *instance();

    void start();

    QString configPath() const { return m_configPath; }

    // The renderer this process is using; fixed for the process lifetime.
    Settings::Renderer activeRenderer() const { return m_activeRenderer; }
    // The saved preference. Changing it saves and, if it differs from the
    // active renderer, restarts visor to apply it.
    Settings::Renderer preferredRenderer() const { return m_preferredRenderer; }
    void setPreferredRenderer(Settings::Renderer renderer);

    // All of these are deferred to the event loop: they tear down the QML
    // engine, which may be the caller.
    void reload();
    void restart();
    void quit();

    void openConfigFolder() const;

signals:
    void preferredRendererChanged();

private:
    QString m_configPath;
    Settings::Renderer m_activeRenderer;
    Settings::Renderer m_preferredRenderer;
    Shell m_shell;
    std::unique_ptr<TrayIcon> m_tray;
};
