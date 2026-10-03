#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <memory>

// The Wi-Fi and Bluetooth radios, as Quick Settings' two buttons: whether
// each exists and is on, and turning it on or off. Windows' Radio API; the
// state follows its StateChanged events, so switching from Settings shows
// here too. Everything is asynchronous: the properties fill in shortly
// after start, and a change shows once Windows has made it.
//
//   Rectangle { visible: Radios.wifiAvailable; color: Radios.wifiOn ? Theme.accent : Theme.surface
//               MouseArea { anchors.fill: parent; onClicked: Radios.wifiOn = !Radios.wifiOn } }
class Radios : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool wifiAvailable READ wifiAvailable NOTIFY changed)
    Q_PROPERTY(bool wifiOn READ wifiOn WRITE setWifiOn NOTIFY changed)
    Q_PROPERTY(bool bluetoothAvailable READ bluetoothAvailable NOTIFY changed)
    Q_PROPERTY(bool bluetoothOn READ bluetoothOn WRITE setBluetoothOn NOTIFY changed)
    // "allowed" or "denied" once Windows has answered, "" before.
    Q_PROPERTY(QString access READ access NOTIFY changed)

public:
    explicit Radios(QObject *parent = nullptr);
    ~Radios() override;

    bool wifiAvailable() const { return m_wifiAvailable; }
    bool wifiOn() const { return m_wifiOn; }
    void setWifiOn(bool on);
    bool bluetoothAvailable() const { return m_bluetoothAvailable; }
    bool bluetoothOn() const { return m_bluetoothOn; }
    void setBluetoothOn(bool on);
    QString access() const { return m_access; }

signals:
    void changed();

private:
    struct Impl;
    friend struct RadiosBridge;

    void start();
    void readStates();

    std::unique_ptr<Impl> d;
    bool m_wifiAvailable = false;
    bool m_wifiOn = false;
    bool m_bluetoothAvailable = false;
    bool m_bluetoothOn = false;
    QString m_access;
};
