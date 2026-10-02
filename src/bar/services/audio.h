#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <memory>

// Volume and mute state of the default playback device. Updates come from
// Core Audio change notifications (IAudioEndpointVolumeCallback) and follow
// the default device when it changes.
//
//   Text { text: Audio.muted ? "muted" : Math.round(Audio.volume * 100) + "%" }
//   MouseArea { onWheel: e => Audio.volume += e.angleDelta.y > 0 ? 0.02 : -0.02 }
class Audio : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool available READ available NOTIFY deviceChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY deviceChanged)
    // 0.0 - 1.0, the same scale as the Windows volume slider.
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)

public:
    explicit Audio(QObject *parent = nullptr);
    ~Audio() override;

    bool available() const;
    QString deviceName() const { return m_deviceName; }
    qreal volume() const { return m_volume; }
    void setVolume(qreal volume);
    bool muted() const { return m_muted; }
    void setMuted(bool muted);

    Q_INVOKABLE void toggleMute() { setMuted(!m_muted); }

signals:
    void deviceChanged();
    void volumeChanged();
    void mutedChanged();

private:
    struct Impl;
    friend struct AudioCallbacks;

    void bindDefaultDevice();
    void applyState(qreal volume, bool muted);

    std::unique_ptr<Impl> d;
    QString m_deviceName;
    qreal m_volume = 0;
    bool m_muted = false;
};
