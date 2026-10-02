// <unknwn.h> must precede winrt/base.h so C++/WinRT can implement classic COM
// interfaces (the WMI event sink).
#include <unknwn.h>
#include <winrt/base.h>

#include <wbemidl.h>

#include "services/brightness.h"

#include <QDebug>
#include <QMetaObject>

#include <mutex>
#include <string>

namespace {

// WMI delivers events on its own threads; the sink posts to the GUI thread
// through this, and Brightness clears it on destruction so late events are
// no-ops.
struct BrightnessBridge
{
    std::mutex mutex;
    Brightness *target = nullptr;

    template<typename F>
    void post(F fn)
    {
        std::scoped_lock lock(mutex);
        if (target)
            QMetaObject::invokeMethod(target, [t = target, fn] { fn(t); }, Qt::QueuedConnection);
    }
};

struct Bstr
{
    BSTR s;
    explicit Bstr(const wchar_t *text) : s(SysAllocString(text)) {}
    ~Bstr() { SysFreeString(s); }
    operator BSTR() const { return s; }
};

} // namespace

// Receives WmiMonitorBrightnessEvent (the level changed, by whoever).
struct BrightnessSink : winrt::implements<BrightnessSink, IWbemObjectSink>
{
    std::shared_ptr<BrightnessBridge> bridge;

    HRESULT STDMETHODCALLTYPE Indicate(long count, IWbemClassObject **objects) override
    {
        for (long i = 0; i < count; ++i) {
            VARIANT v;
            VariantInit(&v);
            if (SUCCEEDED(objects[i]->Get(L"Brightness", 0, &v, nullptr, nullptr)) && v.vt == VT_UI1) {
                const int percent = v.bVal;
                bridge->post([percent](Brightness *b) { b->applyLevel(percent); });
            }
            VariantClear(&v);
        }
        return WBEM_S_NO_ERROR;
    }

    HRESULT STDMETHODCALLTYPE SetStatus(long, HRESULT, BSTR, IWbemClassObject *) override { return WBEM_S_NO_ERROR; }
};

struct Brightness::Impl
{
    std::shared_ptr<BrightnessBridge> bridge = std::make_shared<BrightnessBridge>();
    winrt::com_ptr<IWbemServices> services;
    winrt::com_ptr<BrightnessSink> sink;
    winrt::com_ptr<IWbemObjectSink> stub; // the sink, as WMI calls it
    std::wstring instance;                // InstanceName of the active display
};

Brightness::Brightness(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Impl>())
{
    d->bridge->target = this;

    winrt::com_ptr<IWbemLocator> locator;
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(locator.put())))
        || FAILED(locator->ConnectServer(Bstr(L"ROOT\\WMI"), nullptr, nullptr, nullptr, 0, nullptr, nullptr,
                                         d->services.put()))) {
        qWarning("Brightness: WMI is not available");
        return;
    }
    CoSetProxyBlanket(d->services.get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                      RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

    // The active display, if its brightness can be set at all.
    winrt::com_ptr<IEnumWbemClassObject> rows;
    if (SUCCEEDED(d->services->ExecQuery(
            Bstr(L"WQL"), Bstr(L"SELECT InstanceName, CurrentBrightness FROM WmiMonitorBrightness WHERE Active = TRUE"),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, rows.put()))) {
        winrt::com_ptr<IWbemClassObject> row;
        ULONG n = 0;
        if (rows->Next(WBEM_INFINITE, 1, row.put(), &n) == WBEM_S_NO_ERROR && n == 1) {
            VARIANT v;
            VariantInit(&v);
            if (SUCCEEDED(row->Get(L"InstanceName", 0, &v, nullptr, nullptr)) && v.vt == VT_BSTR)
                d->instance = v.bstrVal;
            VariantClear(&v);
            if (SUCCEEDED(row->Get(L"CurrentBrightness", 0, &v, nullptr, nullptr)) && v.vt == VT_UI1)
                m_percent = v.bVal;
            VariantClear(&v);
        }
    }
    if (d->instance.empty()) {
        qInfo("Brightness: no display with brightness control");
        return;
    }

    // Changes, from the keys or anything else. WMI calls the sink from its
    // own threads through a stub from an unsecured apartment, which is how a
    // plain client receives async events without a security negotiation.
    d->sink = winrt::make_self<BrightnessSink>();
    d->sink->bridge = d->bridge;
    winrt::com_ptr<IUnsecuredApartment> apartment;
    winrt::com_ptr<IUnknown> stub;
    if (SUCCEEDED(CoCreateInstance(CLSID_UnsecuredApartment, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(apartment.put())))
        && SUCCEEDED(apartment->CreateObjectStub(d->sink.get(), stub.put())))
        d->stub = stub.try_as<IWbemObjectSink>();
    if (!d->stub
        || FAILED(d->services->ExecNotificationQueryAsync(Bstr(L"WQL"), Bstr(L"SELECT * FROM WmiMonitorBrightnessEvent"),
                                                           WBEM_FLAG_SEND_STATUS, nullptr, d->stub.get()))) {
        qWarning("Brightness: not notified of changes");
        d->stub = nullptr;
    }
}

Brightness::~Brightness()
{
    {
        std::scoped_lock lock(d->bridge->mutex);
        d->bridge->target = nullptr;
    }
    if (d->services && d->stub)
        d->services->CancelAsyncCall(d->stub.get());
}

bool Brightness::available() const
{
    return !d->instance.empty();
}

void Brightness::setLevel(qreal level)
{
    const int percent = qBound(0, qRound(level * 100), 100);
    if (!available())
        return;
    winrt::com_ptr<IWbemClassObject> cls, method, params;
    if (FAILED(d->services->GetObject(Bstr(L"WmiMonitorBrightnessMethods"), 0, nullptr, cls.put(), nullptr))
        || FAILED(cls->GetMethod(L"WmiSetBrightness", 0, method.put(), nullptr))
        || FAILED(method->SpawnInstance(0, params.put()))) {
        qWarning("Brightness: WmiSetBrightness is not available");
        return;
    }
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_I4; // uint32 Timeout
    v.lVal = 0;
    params->Put(L"Timeout", 0, &v, 0);
    v.vt = VT_UI1; // uint8 Brightness
    v.bVal = BYTE(percent);
    params->Put(L"Brightness", 0, &v, 0);
    // The instance's object path, with its backslashes escaped.
    std::wstring path = L"WmiMonitorBrightnessMethods.InstanceName=\"";
    for (wchar_t c : d->instance) {
        if (c == L'\\' || c == L'"')
            path += L'\\';
        path += c;
    }
    path += L'"';
    if (FAILED(d->services->ExecMethod(Bstr(path.c_str()), Bstr(L"WmiSetBrightness"), 0, nullptr, params.get(),
                                       nullptr, nullptr))) {
        qWarning("Brightness: WmiSetBrightness failed");
        return;
    }
    applyLevel(percent);
}

void Brightness::applyLevel(int percent)
{
    if (percent == m_percent)
        return;
    m_percent = percent;
    emit levelChanged();
}
