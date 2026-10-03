#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <memory>

// The internet connection, as the taskbar's network icon shows it: whatever
// Windows calls the internet connection profile (NetworkInformation), with
// its kind, name and signal. Updates come from NetworkStatusChanged.
//
//   Icon { glyph: Network.kind === "wifi" ? "" : Network.kind === "ethernet" ? "" : "" }
//   Text { text: Network.connected ? Network.name : "No internet" }
class Network : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // "wifi", "ethernet", "cellular", or "none" when nothing is connected.
    Q_PROPERTY(QString kind READ kind NOTIFY changed)
    // Internet access, as opposed to a local-only or captive-portal connection.
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    // The Wi-Fi network's name (SSID), or the connection's profile name.
    Q_PROPERTY(QString name READ name NOTIFY changed)
    // Wi-Fi and cellular signal, 0-5 bars; 5 for a wired connection.
    Q_PROPERTY(int signal READ signal NOTIFY changed)
    Q_PROPERTY(bool metered READ metered NOTIFY changed)

public:
    explicit Network(QObject *parent = nullptr);
    ~Network() override;

    QString kind() const { return m_kind; }
    bool connected() const { return m_connected; }
    QString name() const { return m_name; }
    int signal() const { return m_signal; }
    bool metered() const { return m_metered; }

    Q_INVOKABLE void refresh();

signals:
    void changed();

private:
    struct Impl;

    std::unique_ptr<Impl> d;
    QString m_kind = QStringLiteral("none");
    bool m_connected = false;
    QString m_name;
    int m_signal = 0;
    bool m_metered = false;
};
