#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <memory>

// Which apps are using the microphone, camera or location right now, as the
// taskbar's indicators show. Read from the capability access store Windows
// keeps for its own indicators (per app, when it last started and stopped
// using each), and watched, so nothing polls.
//
//   Icon { visible: Privacy.microphone; glyph: "" }
class Privacy : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool microphone READ microphone NOTIFY changed)
    Q_PROPERTY(bool camera READ camera NOTIFY changed)
    Q_PROPERTY(bool location READ location NOTIFY changed)
    // The apps, by name ("Teams", "msedge").
    Q_PROPERTY(QStringList microphoneApps READ microphoneApps NOTIFY changed)
    Q_PROPERTY(QStringList cameraApps READ cameraApps NOTIFY changed)
    Q_PROPERTY(QStringList locationApps READ locationApps NOTIFY changed)

public:
    explicit Privacy(QObject *parent = nullptr);
    ~Privacy() override;

    bool microphone() const { return !m_microphone.isEmpty(); }
    bool camera() const { return !m_camera.isEmpty(); }
    bool location() const { return !m_location.isEmpty(); }
    QStringList microphoneApps() const { return m_microphone; }
    QStringList cameraApps() const { return m_camera; }
    QStringList locationApps() const { return m_location; }

    Q_INVOKABLE void refresh();

signals:
    void changed();

private:
    struct Impl;

    std::unique_ptr<Impl> d;
    QStringList m_microphone;
    QStringList m_camera;
    QStringList m_location;
};
