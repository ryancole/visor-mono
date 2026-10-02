#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

// visor itself, as seen from QML. Lets a config build its own settings UI or
// controls on top of the same actions as the tray menu.
//
//   MouseArea { onClicked: Visor.renderer = Visor.Gpu }   // saves + restarts
//   Text { text: "v" + Visor.version }
class VisorApi : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Visor)
    QML_SINGLETON

    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString configPath READ configPath CONSTANT)
    // Saved renderer preference. Setting it to something other than
    // activeRenderer restarts visor.
    Q_PROPERTY(Renderer renderer READ renderer WRITE setRenderer NOTIFY rendererChanged)
    Q_PROPERTY(Renderer activeRenderer READ activeRenderer CONSTANT)

public:
    enum Renderer { Cpu, Gpu };
    Q_ENUM(Renderer)

    explicit VisorApi(QObject *parent = nullptr);

    QString version() const;
    QString configPath() const;
    Renderer renderer() const;
    void setRenderer(Renderer renderer);
    Renderer activeRenderer() const;

    Q_INVOKABLE void reload();
    Q_INVOKABLE void restart();
    Q_INVOKABLE void quit();
    Q_INVOKABLE void openConfigFolder();

signals:
    void rendererChanged();
};
