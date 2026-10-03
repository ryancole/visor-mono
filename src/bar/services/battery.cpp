#include "services/battery.h"

#include <QDebug>

// initguid: define the power-setting GUIDs in this TU.
#include <initguid.h>
#include <windows.h>

namespace {

// A message-only window for the power-setting notifications: Windows sends
// WM_POWERBROADCAST to the window that registered, so no broadcast is
// needed.
LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE) {
        const auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    if (msg == WM_POWERBROADCAST) {
        if (auto *battery = reinterpret_cast<Battery *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
            battery->refresh();
        return TRUE;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

const wchar_t *windowClass()
{
    static const ATOM atom = [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = windowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"VisorBattery";
        return RegisterClassExW(&wc);
    }();
    return atom ? L"VisorBattery" : nullptr;
}

} // namespace

struct Battery::Impl
{
    HWND hwnd = nullptr;
    HPOWERNOTIFY notifications[3]{};
};

Battery::Battery(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Impl>())
{
    d->hwnd = CreateWindowExW(0, windowClass(), L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr),
                              this);
    if (d->hwnd) {
        const GUID *settings[] = {&GUID_BATTERY_PERCENTAGE_REMAINING, &GUID_ACDC_POWER_SOURCE,
                                  &GUID_POWER_SAVING_STATUS};
        for (size_t i = 0; i < std::size(settings); ++i)
            d->notifications[i] = RegisterPowerSettingNotification(d->hwnd, settings[i], DEVICE_NOTIFY_WINDOW_HANDLE);
    } else {
        qWarning() << "Battery: no window for power notifications, error" << GetLastError();
    }
    refresh();
}

Battery::~Battery()
{
    for (HPOWERNOTIFY n : d->notifications) {
        if (n)
            UnregisterPowerSettingNotification(n);
    }
    if (d->hwnd) {
        SetWindowLongPtrW(d->hwnd, GWLP_USERDATA, 0);
        DestroyWindow(d->hwnd);
    }
}

void Battery::refresh()
{
    SYSTEM_POWER_STATUS status{};
    if (!GetSystemPowerStatus(&status))
        return;
    const bool present = status.BatteryFlag != 255 && !(status.BatteryFlag & 128);
    const int percent = status.BatteryLifePercent == 255 ? 0 : int(status.BatteryLifePercent);
    const bool charging = (status.BatteryFlag & 8) != 0;
    const bool pluggedIn = status.ACLineStatus == 1;
    const bool saver = (status.SystemStatusFlag & 1) != 0;
    const int secondsLeft = status.BatteryLifeTime == DWORD(-1) ? -1 : int(status.BatteryLifeTime);

    if (present == m_present && percent == m_percent && charging == m_charging && pluggedIn == m_pluggedIn
        && saver == m_saver && secondsLeft == m_secondsLeft)
        return;
    m_present = present;
    m_percent = percent;
    m_charging = charging;
    m_pluggedIn = pluggedIn;
    m_saver = saver;
    m_secondsLeft = secondsLeft;
    emit changed();
}
