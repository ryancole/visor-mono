#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <memory>

// Now-playing info from the system media transport controls (the same source
// as the Windows volume flyout): Spotify, browsers, media players, etc.
// Follows the session Windows considers current; updates are event-driven.
//
//   Text { visible: Media.available; text: Media.artist + " - " + Media.title }
//   MouseArea { onClicked: Media.playPause() }
class Media : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool available READ available NOTIFY sessionChanged)
    // App user model ID of the source app, e.g. "Spotify.exe".
    Q_PROPERTY(QString appId READ appId NOTIFY sessionChanged)
    Q_PROPERTY(QString title READ title NOTIFY metadataChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY metadataChanged)
    Q_PROPERTY(QString album READ album NOTIFY metadataChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playbackChanged)
    Q_PROPERTY(bool canPlayPause READ canPlayPause NOTIFY playbackChanged)
    Q_PROPERTY(bool canNext READ canNext NOTIFY playbackChanged)
    Q_PROPERTY(bool canPrevious READ canPrevious NOTIFY playbackChanged)

public:
    explicit Media(QObject *parent = nullptr);
    ~Media() override;

    bool available() const { return m_available; }
    QString appId() const { return m_appId; }
    QString title() const { return m_title; }
    QString artist() const { return m_artist; }
    QString album() const { return m_album; }
    bool playing() const { return m_playing; }
    bool canPlayPause() const { return m_canPlayPause; }
    bool canNext() const { return m_canNext; }
    bool canPrevious() const { return m_canPrevious; }

    Q_INVOKABLE void playPause();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();

signals:
    void sessionChanged();
    void metadataChanged();
    void playbackChanged();

private:
    struct Impl;
    friend struct Impl;

    void bindSession();
    void refreshPlayback();
    void refreshMetadata();
    void setMetadata(int generation, const QString &title, const QString &artist, const QString &album);

    std::unique_ptr<Impl> d;
    bool m_available = false;
    QString m_appId;
    QString m_title;
    QString m_artist;
    QString m_album;
    bool m_playing = false;
    bool m_canPlayPause = false;
    bool m_canNext = false;
    bool m_canPrevious = false;
    int m_metadataGeneration = 0;
};
