#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <memory>

// Windows Update's "restart required" state, the one Explorer's taskbar
// shows an icon for once updates have installed. Read from the registry
// keys Windows Update and the servicing stack set, and watched, so nothing
// polls. A bar shows a badge while it's true.
//
//   Icon { visible: Updates.restartRequired; glyph: "" }
class Updates : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool restartRequired READ restartRequired NOTIFY changed)

public:
    explicit Updates(QObject *parent = nullptr);
    ~Updates() override;

    bool restartRequired() const { return m_restartRequired; }

    Q_INVOKABLE void refresh();

signals:
    void changed();

private:
    struct Impl;

    std::unique_ptr<Impl> d;
    bool m_restartRequired = false;
};
