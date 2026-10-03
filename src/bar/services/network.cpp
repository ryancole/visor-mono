// C++/WinRT first: Qt's keyword macros must not leak into the projection.
#include <unknwn.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Networking.Connectivity.h>

#include "services/network.h"

#include <QDebug>
#include <QMetaObject>

#include <mutex>

namespace wnc = winrt::Windows::Networking::Connectivity;

namespace {

QString toQString(const winrt::hstring &s)
{
    return QString::fromWCharArray(s.c_str(), int(s.size()));
}

} // namespace

// NetworkStatusChanged arrives on a thread-pool thread; it posts a refresh to
// the GUI thread through this, which the Network object clears on
// destruction so a late event becomes a no-op.
struct NetworkBridge
{
    std::mutex mutex;
    Network *target = nullptr;

    template<typename F>
    void post(F fn)
    {
        std::scoped_lock lock(mutex);
        if (target)
            QMetaObject::invokeMethod(target, [t = target, fn] { fn(t); }, Qt::QueuedConnection);
    }
};

struct Network::Impl
{
    std::shared_ptr<NetworkBridge> bridge = std::make_shared<NetworkBridge>();
    winrt::event_token token{};
    bool subscribed = false;
};

Network::Network(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Impl>())
{
    d->bridge->target = this;
    try {
        auto bridge = d->bridge;
        d->token = wnc::NetworkInformation::NetworkStatusChanged(
            [bridge](const winrt::Windows::Foundation::IInspectable &) {
                bridge->post([](Network *n) { n->refresh(); });
            });
        d->subscribed = true;
    } catch (const winrt::hresult_error &e) {
        qWarning() << "Network: no status events, hr" << Qt::hex << ulong(e.code());
    }
    refresh();
}

Network::~Network()
{
    {
        std::scoped_lock lock(d->bridge->mutex);
        d->bridge->target = nullptr;
    }
    if (d->subscribed) {
        try {
            wnc::NetworkInformation::NetworkStatusChanged(d->token);
        } catch (const winrt::hresult_error &) {
        }
    }
}

void Network::refresh()
{
    QString kind = QStringLiteral("none");
    bool connected = false;
    QString name;
    int signal = 0;
    bool metered = false;
    try {
        if (const auto profile = wnc::NetworkInformation::GetInternetConnectionProfile()) {
            connected = profile.GetNetworkConnectivityLevel() == wnc::NetworkConnectivityLevel::InternetAccess;
            name = toQString(profile.ProfileName());
            if (profile.IsWlanConnectionProfile()) {
                kind = QStringLiteral("wifi");
                try {
                    name = toQString(profile.WlanConnectionProfileDetails().GetConnectedSsid());
                } catch (const winrt::hresult_error &) {
                }
            } else if (profile.IsWwanConnectionProfile()) {
                kind = QStringLiteral("cellular");
            } else {
                kind = QStringLiteral("ethernet");
            }
            signal = 5;
            if (const auto bars = profile.GetSignalBars())
                signal = int(bars.Value());
            try {
                metered = profile.GetConnectionCost().NetworkCostType() != wnc::NetworkCostType::Unrestricted;
            } catch (const winrt::hresult_error &) {
            }
        }
    } catch (const winrt::hresult_error &e) {
        qWarning() << "Network: query failed, hr" << Qt::hex << ulong(e.code());
    }

    if (kind == m_kind && connected == m_connected && name == m_name && signal == m_signal && metered == m_metered)
        return;
    m_kind = kind;
    m_connected = connected;
    m_name = name;
    m_signal = signal;
    m_metered = metered;
    qInfo().noquote() << "network:" << kind << (name.isEmpty() ? QString() : QLatin1Char('"') + name + QLatin1Char('"'))
                      << (connected ? "internet" : "no internet") << signal << "bars" << (metered ? "metered" : "");
    emit changed();
}
