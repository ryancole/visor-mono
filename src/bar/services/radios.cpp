// C++/WinRT first: Qt's keyword macros must not leak into the projection.
#include <unknwn.h>
#include <winrt/base.h>
#include <winrt/Windows.Devices.Radios.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

#include "services/radios.h"

#include <QDebug>
#include <QMetaObject>

#include <mutex>
#include <vector>

namespace wdr = winrt::Windows::Devices::Radios;
using winrt::Windows::Foundation::AsyncStatus;

// Completions and StateChanged events arrive on other threads; they post to
// the GUI thread through this, which Radios clears on destruction.
struct RadiosBridge
{
    std::mutex mutex;
    Radios *target = nullptr;

    template<typename F>
    void post(F fn)
    {
        std::scoped_lock lock(mutex);
        if (target)
            QMetaObject::invokeMethod(target, [t = target, fn] { fn(t); }, Qt::QueuedConnection);
    }
};

struct Radios::Impl
{
    std::shared_ptr<RadiosBridge> bridge = std::make_shared<RadiosBridge>();
    wdr::Radio wifi{nullptr};
    wdr::Radio bluetooth{nullptr};
    std::vector<wdr::Radio::StateChanged_revoker> revokers;

    static bool isOn(const wdr::Radio &radio)
    {
        return radio && radio.State() == wdr::RadioState::On;
    }

    void setState(const wdr::Radio &radio, bool on)
    {
        if (!radio)
            return;
        try {
            auto b = bridge;
            radio.SetStateAsync(on ? wdr::RadioState::On : wdr::RadioState::Off)
                .Completed([b](const auto &op, AsyncStatus status) {
                    if (status != AsyncStatus::Completed)
                        return;
                    const auto result = op.GetResults();
                    b->post([result](Radios *r) {
                        if (result != wdr::RadioAccessStatus::Allowed)
                            qWarning() << "Radios: change refused, status" << int(result);
                        r->readStates();
                    });
                });
        } catch (const winrt::hresult_error &e) {
            qWarning() << "Radios: cannot set state, hr" << Qt::hex << ulong(e.code());
        }
    }
};

Radios::Radios(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Impl>())
{
    d->bridge->target = this;
    start();
}

Radios::~Radios()
{
    std::scoped_lock lock(d->bridge->mutex);
    d->bridge->target = nullptr;
}

void Radios::start()
{
    // Ask first (a desktop app is allowed unless Settings > Privacy says
    // otherwise), then list the radios and follow each one's state.
    try {
        auto bridge = d->bridge;
        wdr::Radio::RequestAccessAsync().Completed([bridge](const auto &op, AsyncStatus status) {
            const auto access = status == AsyncStatus::Completed ? op.GetResults() : wdr::RadioAccessStatus::Unspecified;
            bridge->post([access](Radios *r) {
                r->m_access = access == wdr::RadioAccessStatus::Allowed ? QStringLiteral("allowed")
                                                                         : QStringLiteral("denied");
                emit r->changed();
            });
            if (access != wdr::RadioAccessStatus::Allowed) {
                qWarning() << "Radios: access not allowed, status" << int(access);
                return;
            }
            wdr::Radio::GetRadiosAsync().Completed([bridge](const auto &op, AsyncStatus status) {
                if (status != AsyncStatus::Completed)
                    return;
                const auto radios = op.GetResults();
                bridge->post([radios](Radios *r) {
                    for (const wdr::Radio &radio : radios) {
                        if (radio.Kind() == wdr::RadioKind::WiFi && !r->d->wifi)
                            r->d->wifi = radio;
                        else if (radio.Kind() == wdr::RadioKind::Bluetooth && !r->d->bluetooth)
                            r->d->bluetooth = radio;
                        else
                            continue;
                        auto bridge = r->d->bridge;
                        r->d->revokers.push_back(radio.StateChanged(
                            winrt::auto_revoke, [bridge](const wdr::Radio &, const winrt::Windows::Foundation::IInspectable &) {
                                bridge->post([](Radios *r) { r->readStates(); });
                            }));
                    }
                    r->readStates();
                });
            });
        });
    } catch (const winrt::hresult_error &e) {
        qWarning() << "Radios: unavailable, hr" << Qt::hex << ulong(e.code());
    }
}

void Radios::readStates()
{
    bool wifiAvailable = false, wifiOn = false, bluetoothAvailable = false, bluetoothOn = false;
    try {
        wifiAvailable = d->wifi != nullptr;
        wifiOn = Impl::isOn(d->wifi);
        bluetoothAvailable = d->bluetooth != nullptr;
        bluetoothOn = Impl::isOn(d->bluetooth);
    } catch (const winrt::hresult_error &e) {
        qWarning() << "Radios: cannot read state, hr" << Qt::hex << ulong(e.code());
    }
    if (wifiAvailable == m_wifiAvailable && wifiOn == m_wifiOn && bluetoothAvailable == m_bluetoothAvailable
        && bluetoothOn == m_bluetoothOn)
        return;
    m_wifiAvailable = wifiAvailable;
    m_wifiOn = wifiOn;
    m_bluetoothAvailable = bluetoothAvailable;
    m_bluetoothOn = bluetoothOn;
    qInfo() << "radios: wifi" << (wifiAvailable ? (wifiOn ? "on" : "off") : "none") << "bluetooth"
            << (bluetoothAvailable ? (bluetoothOn ? "on" : "off") : "none");
    emit changed();
}

void Radios::setWifiOn(bool on)
{
    d->setState(d->wifi, on);
}

void Radios::setBluetoothOn(bool on)
{
    d->setState(d->bluetooth, on);
}
