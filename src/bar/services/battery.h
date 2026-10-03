#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <memory>

// The battery, as the taskbar shows it on a laptop: level, charging, time
// left and battery saver. On a desktop there is none and `present` is
// false, so a bar shows nothing, as Windows' does. From
// GetSystemPowerStatus, re-read on the power-setting notifications Windows
// sends when the level, the power source or battery saver change.
//
//   Text { visible: Battery.present; text: Battery.percent + "%" }
class Battery : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool present READ present NOTIFY changed)
    Q_PROPERTY(int percent READ percent NOTIFY changed) // 0-100
    Q_PROPERTY(bool charging READ charging NOTIFY changed)
    Q_PROPERTY(bool pluggedIn READ pluggedIn NOTIFY changed) // on mains power
    Q_PROPERTY(bool saver READ saver NOTIFY changed)         // battery saver is on
    Q_PROPERTY(int secondsLeft READ secondsLeft NOTIFY changed) // -1 when unknown

public:
    explicit Battery(QObject *parent = nullptr);
    ~Battery() override;

    bool present() const { return m_present; }
    int percent() const { return m_percent; }
    bool charging() const { return m_charging; }
    bool pluggedIn() const { return m_pluggedIn; }
    bool saver() const { return m_saver; }
    int secondsLeft() const { return m_secondsLeft; }

    Q_INVOKABLE void refresh();

signals:
    void changed();

private:
    struct Impl;

    std::unique_ptr<Impl> d;
    bool m_present = false;
    int m_percent = 0;
    bool m_charging = false;
    bool m_pluggedIn = true;
    bool m_saver = false;
    int m_secondsLeft = -1;
};
