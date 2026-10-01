// C++/WinRT first: Qt's keyword macros (signals, slots, emit) must not leak
// into the WinRT projection headers.
#include <unknwn.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>

#include "services/media.h"

#include <QMetaObject>

#include <mutex>

namespace wmc = winrt::Windows::Media::Control;
using winrt::Windows::Foundation::AsyncStatus;
using winrt::Windows::Foundation::IAsyncOperation;

namespace {

QString toQString(const winrt::hstring &s)
{
    return QString::fromWCharArray(s.c_str(), int(s.size()));
}

} // namespace

// WinRT events and async completions arrive on thread-pool threads. Handlers
// hold a shared Bridge and post work to the GUI thread through it; the Media
// object clears it on destruction so late callbacks become no-ops.
struct MediaBridge
{
    std::mutex mutex;
    Media *target = nullptr;

    template<typename F>
    void post(F fn)
    {
        std::scoped_lock lock(mutex);
        if (target)
            QMetaObject::invokeMethod(target, [t = target, fn] { fn(t); }, Qt::QueuedConnection);
    }
};

struct Media::Impl
{
    std::shared_ptr<MediaBridge> bridge = std::make_shared<MediaBridge>();
    wmc::GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
    wmc::GlobalSystemMediaTransportControlsSessionManager::CurrentSessionChanged_revoker sessionChangedToken;
    wmc::GlobalSystemMediaTransportControlsSession session{nullptr};
    wmc::GlobalSystemMediaTransportControlsSession::MediaPropertiesChanged_revoker propertiesChangedToken;
    wmc::GlobalSystemMediaTransportControlsSession::PlaybackInfoChanged_revoker playbackChangedToken;
};

Media::Media(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Impl>())
{
    d->bridge->target = this;

    try {
        auto op = wmc::GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
        op.Completed([bridge = d->bridge](const auto &op, AsyncStatus status) {
            if (status != AsyncStatus::Completed)
                return;
            auto manager = op.GetResults();
            bridge->post([manager](Media *m) {
                try {
                    m->d->manager = manager;
                    m->d->sessionChangedToken = manager.CurrentSessionChanged(
                        winrt::auto_revoke, [bridge = m->d->bridge](const auto &, const auto &) {
                            bridge->post([](Media *m) { m->bindSession(); });
                        });
                    m->bindSession();
                } catch (const winrt::hresult_error &e) {
                    qWarning("Media: %ls", e.message().c_str());
                }
            });
        });
    } catch (const winrt::hresult_error &e) {
        qWarning("Media: media session manager unavailable: %ls", e.message().c_str());
    }
}

Media::~Media()
{
    {
        std::scoped_lock lock(d->bridge->mutex);
        d->bridge->target = nullptr;
    }
    // Revokers unregister the WinRT event handlers when d is destroyed.
}

void Media::bindSession()
{
    d->propertiesChangedToken.revoke();
    d->playbackChangedToken.revoke();
    d->session = nullptr;

    QString appId;
    try {
        if (d->manager)
            d->session = d->manager.GetCurrentSession();
        if (d->session) {
            appId = toQString(d->session.SourceAppUserModelId());
            auto bridge = d->bridge;
            d->propertiesChangedToken = d->session.MediaPropertiesChanged(
                winrt::auto_revoke,
                [bridge](const auto &, const auto &) { bridge->post([](Media *m) { m->refreshMetadata(); }); });
            d->playbackChangedToken = d->session.PlaybackInfoChanged(
                winrt::auto_revoke,
                [bridge](const auto &, const auto &) { bridge->post([](Media *m) { m->refreshPlayback(); }); });
        }
    } catch (const winrt::hresult_error &e) {
        qWarning("Media: %ls", e.message().c_str());
        d->session = nullptr;
    }

    const bool available = d->session != nullptr;
    if (available != m_available || appId != m_appId) {
        m_available = available;
        m_appId = appId;
        emit sessionChanged();
    }
    refreshPlayback();
    refreshMetadata();
}

void Media::refreshPlayback()
{
    bool playing = false, canPlayPause = false, canNext = false, canPrevious = false;
    if (d->session) {
        try {
            const auto info = d->session.GetPlaybackInfo();
            playing = info.PlaybackStatus() == wmc::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
            const auto controls = info.Controls();
            canPlayPause = controls.IsPlayPauseToggleEnabled();
            canNext = controls.IsNextEnabled();
            canPrevious = controls.IsPreviousEnabled();
        } catch (const winrt::hresult_error &) {
        }
    }

    if (playing != m_playing || canPlayPause != m_canPlayPause || canNext != m_canNext
        || canPrevious != m_canPrevious) {
        m_playing = playing;
        m_canPlayPause = canPlayPause;
        m_canNext = canNext;
        m_canPrevious = canPrevious;
        emit playbackChanged();
    }
}

void Media::refreshMetadata()
{
    // Each request gets a generation number so a slow, stale response can't
    // overwrite a newer one.
    const int generation = ++m_metadataGeneration;
    if (!d->session) {
        setMetadata(generation, {}, {}, {});
        return;
    }

    try {
        auto op = d->session.TryGetMediaPropertiesAsync();
        op.Completed([bridge = d->bridge, generation](const auto &op, AsyncStatus status) {
            QString title, artist, album;
            if (status == AsyncStatus::Completed) {
                try {
                    const auto props = op.GetResults();
                    title = toQString(props.Title());
                    artist = toQString(props.Artist());
                    album = toQString(props.AlbumTitle());
                } catch (const winrt::hresult_error &) {
                }
            }
            bridge->post([=](Media *m) { m->setMetadata(generation, title, artist, album); });
        });
    } catch (const winrt::hresult_error &) {
        setMetadata(generation, {}, {}, {});
    }
}

void Media::setMetadata(int generation, const QString &title, const QString &artist, const QString &album)
{
    if (generation != m_metadataGeneration)
        return;
    if (title == m_title && artist == m_artist && album == m_album)
        return;
    m_title = title;
    m_artist = artist;
    m_album = album;
    emit metadataChanged();
}

void Media::playPause()
{
    if (d->session) {
        try {
            d->session.TryTogglePlayPauseAsync();
        } catch (const winrt::hresult_error &) {
        }
    }
}

void Media::next()
{
    if (d->session) {
        try {
            d->session.TrySkipNextAsync();
        } catch (const winrt::hresult_error &) {
        }
    }
}

void Media::previous()
{
    if (d->session) {
        try {
            d->session.TrySkipPreviousAsync();
        } catch (const winrt::hresult_error &) {
        }
    }
}
