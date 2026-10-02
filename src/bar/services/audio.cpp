// <unknwn.h> must precede winrt/base.h so C++/WinRT can implement classic COM
// interfaces.
#include <unknwn.h>
#include <winrt/base.h>

#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <propsys.h>
// initguid: define (not just declare) PKEY_Device_FriendlyName in this TU.
#include <initguid.h>
#include <functiondiscoverykeys_devpkey.h>

#include "services/audio.h"

#include <QMetaObject>

#include <mutex>

namespace {

// Tags volume changes we make ourselves so their echo notifications can be
// ignored (otherwise fast scrolling jitters as stale echoes arrive).
constexpr GUID kOwnChange = {0x6f1b1f5e, 0x8a7c, 0x4b8e, {0x9d, 0x31, 0x2b, 0x7f, 0x0c, 0x55, 0x1a, 0x42}};

} // namespace

// Receives Core Audio callbacks on an arbitrary COM thread and forwards them to
// the Audio object on the GUI thread.
struct AudioCallbacks : winrt::implements<AudioCallbacks, IAudioEndpointVolumeCallback, IMMNotificationClient>
{
    std::mutex mutex;
    Audio *target = nullptr;

    void detach()
    {
        std::scoped_lock lock(mutex);
        target = nullptr;
    }

    // Runs fn(target) on the GUI thread, unless the Audio object is gone.
    template<typename F>
    void post(F fn)
    {
        std::scoped_lock lock(mutex);
        if (target)
            QMetaObject::invokeMethod(target, [t = target, fn] { fn(t); }, Qt::QueuedConnection);
    }

    // IAudioEndpointVolumeCallback
    HRESULT __stdcall OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA data) override;

    // IMMNotificationClient
    HRESULT __stdcall OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override;
    HRESULT __stdcall OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    HRESULT __stdcall OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT __stdcall OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT __stdcall OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }
};

struct Audio::Impl
{
    winrt::com_ptr<IMMDeviceEnumerator> enumerator;
    winrt::com_ptr<IAudioEndpointVolume> endpoint;
    winrt::com_ptr<AudioCallbacks> callbacks;
};

HRESULT AudioCallbacks::OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA data)
{
    if (!data || data->guidEventContext == kOwnChange)
        return S_OK;
    const qreal volume = data->fMasterVolume;
    const bool muted = data->bMuted != FALSE;
    post([volume, muted](Audio *t) { t->applyState(volume, muted); });
    return S_OK;
}

HRESULT AudioCallbacks::OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR)
{
    if (flow == eRender && role == eConsole)
        post([](Audio *t) { t->bindDefaultDevice(); });
    return S_OK;
}

Audio::Audio(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Impl>())
{
    d->callbacks = winrt::make_self<AudioCallbacks>();
    d->callbacks->target = this;

    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(d->enumerator.put())))) {
        qWarning("Audio: could not create the Core Audio device enumerator");
        return;
    }
    d->enumerator->RegisterEndpointNotificationCallback(d->callbacks.get());
    bindDefaultDevice();
}

Audio::~Audio()
{
    d->callbacks->detach();
    if (d->endpoint)
        d->endpoint->UnregisterControlChangeNotify(d->callbacks.get());
    if (d->enumerator)
        d->enumerator->UnregisterEndpointNotificationCallback(d->callbacks.get());
}

bool Audio::available() const
{
    return d->endpoint != nullptr;
}

void Audio::bindDefaultDevice()
{
    if (d->endpoint) {
        d->endpoint->UnregisterControlChangeNotify(d->callbacks.get());
        d->endpoint = nullptr;
    }

    QString name;
    winrt::com_ptr<IMMDevice> device;
    if (d->enumerator && SUCCEEDED(d->enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.put()))) {
        device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, d->endpoint.put_void());

        winrt::com_ptr<IPropertyStore> store;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, store.put()))) {
            PROPVARIANT value;
            PropVariantInit(&value);
            if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR)
                name = QString::fromWCharArray(value.pwszVal);
            PropVariantClear(&value);
        }
    }

    float volume = 0;
    BOOL muted = FALSE;
    if (d->endpoint) {
        d->endpoint->RegisterControlChangeNotify(d->callbacks.get());
        d->endpoint->GetMasterVolumeLevelScalar(&volume);
        d->endpoint->GetMute(&muted);
    }

    m_deviceName = name;
    emit deviceChanged();
    applyState(volume, muted != FALSE);
}

void Audio::applyState(qreal volume, bool muted)
{
    if (!qFuzzyCompare(1 + volume, 1 + m_volume)) {
        m_volume = volume;
        emit volumeChanged();
    }
    if (muted != m_muted) {
        m_muted = muted;
        emit mutedChanged();
    }
}

void Audio::setVolume(qreal volume)
{
    volume = qBound(0.0, volume, 1.0);
    if (!d->endpoint)
        return;
    d->endpoint->SetMasterVolumeLevelScalar(float(volume), &kOwnChange);
    applyState(volume, m_muted);
}

void Audio::setMuted(bool muted)
{
    if (!d->endpoint)
        return;
    d->endpoint->SetMute(muted, &kOwnChange);
    applyState(m_volume, muted);
}
