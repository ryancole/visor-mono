#include "shell/wallpaper.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QString>

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace visor {

namespace {

// WallpaperStyle values as written by Settings (TileWallpaper=1 with Center
// means Tile).
enum class Fit { Center, Tile, Stretch, Fit, Fill, Span };

struct Settings
{
    QString path;
    Fit fit = Fit::Fill;
    COLORREF background = RGB(0, 0, 0);
};

QString expand(const QString &text)
{
    wchar_t out[MAX_PATH * 2] = {};
    ExpandEnvironmentStringsW(text.toStdWString().c_str(), out, DWORD(std::size(out)));
    return QString::fromWCharArray(out);
}

// Windows Spotlight (the default theme on a fresh Windows 11) names a
// 280x175 placeholder as its wallpaper and relies on Explorer to swap in the
// day's picture. Without Explorer that never happens, so while Spotlight is
// the background show Windows' own wallpaper for the mode (what the
// "Windows (light)" and "Windows (dark)" themes use) instead of a blown-up
// thumbnail. Spotlight is recognised by its theme file (WindowsSpotlight=1)
// or by the wallpaper itself coming from the Spotlight folder: Settings
// saves the live state as Custom.theme without that key when its Themes
// page is visited, and the placeholder is never worth showing. Returns an
// empty path otherwise.
QString spotlightFallback(const QString &wallpaper)
{
    bool spotlight = wallpaper.startsWith(expand(QStringLiteral("%SystemRoot%\\Web\\Wallpaper\\Spotlight\\")),
                                          Qt::CaseInsensitive);
    if (!spotlight) {
        const QSettings themes(
            QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes"),
            QSettings::NativeFormat);
        const QString current = expand(themes.value(QStringLiteral("CurrentTheme")).toString());
        wchar_t value[8] = {};
        if (!current.isEmpty()) {
            GetPrivateProfileStringW(L"Control Panel\\Desktop", L"WindowsSpotlight", L"", value, DWORD(std::size(value)),
                                     QDir::toNativeSeparators(current).toStdWString().c_str());
        }
        spotlight = wcscmp(value, L"1") == 0;
    }
    if (!spotlight)
        return {};
    const QSettings personalize(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat);
    const bool light = personalize.value(QStringLiteral("SystemUsesLightTheme"), 1).toInt() != 0;
    const QString path = expand(QStringLiteral("%SystemRoot%\\Web\\Wallpaper\\Windows\\")) + (light ? QStringLiteral("img0.jpg") : QStringLiteral("img19.jpg"));
    return QFileInfo::exists(path) ? path : QString();
}

Settings readSettings()
{
    Settings s;

    wchar_t path[MAX_PATH * 2] = {};
    if (SystemParametersInfoW(SPI_GETDESKWALLPAPER, DWORD(std::size(path)), path, 0))
        s.path = QString::fromWCharArray(path);
    if (const QString fallback = spotlightFallback(s.path); !fallback.isEmpty()) {
        static bool logged = false;
        if (!logged) {
            qInfo() << "Windows Spotlight is the background, which needs Explorer; showing" << fallback;
            logged = true;
        }
        s.path = fallback;
    }

    const QSettings desktop(QStringLiteral("HKEY_CURRENT_USER\\Control Panel\\Desktop"), QSettings::NativeFormat);
    const int style = desktop.value(QStringLiteral("WallpaperStyle"), 10).toString().toInt();
    const bool tile = desktop.value(QStringLiteral("TileWallpaper"), 0).toString().toInt() != 0;
    switch (style) {
    case 0: s.fit = tile ? Fit::Tile : Fit::Center; break;
    case 2: s.fit = Fit::Stretch; break;
    case 6: s.fit = Fit::Fit; break;
    case 22: s.fit = Fit::Span; break;
    default: s.fit = Fit::Fill; break;
    }

    // "R G B", as Explorer stores it.
    const QSettings colors(QStringLiteral("HKEY_CURRENT_USER\\Control Panel\\Colors"), QSettings::NativeFormat);
    const QStringList rgb = colors.value(QStringLiteral("Background")).toString().split(u' ', Qt::SkipEmptyParts);
    if (rgb.size() == 3)
        s.background = RGB(rgb[0].toInt(), rgb[1].toInt(), rgb[2].toInt());
    return s;
}

IWICImagingFactory *wicFactory()
{
    static ComPtr<IWICImagingFactory> factory = [] {
        ComPtr<IWICImagingFactory> f;
        const HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
        if (FAILED(hr))
            qWarning() << "WIC unavailable" << Qt::hex << hr;
        return f;
    }();
    return factory.Get();
}

// Copies `source` (any WIC source, already at its final size) into the DC
// with its top-left at `dest`.
void blit(HDC dc, IWICBitmapSource *source, POINT dest)
{
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(wicFactory()->CreateFormatConverter(&converter))
        || FAILED(converter->Initialize(source, GUID_WICPixelFormat32bppBGR, WICBitmapDitherTypeNone, nullptr, 0,
                                        WICBitmapPaletteTypeCustom))) {
        return;
    }
    UINT w = 0, h = 0;
    converter->GetSize(&w, &h);
    if (!w || !h)
        return;

    std::vector<BYTE> pixels(size_t(w) * h * 4);
    if (FAILED(converter->CopyPixels(nullptr, w * 4, UINT(pixels.size()), pixels.data())))
        return;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = LONG(w);
    bmi.bmiHeader.biHeight = -LONG(h); // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(dc, dest.x, dest.y, w, h, 0, 0, 0, h, pixels.data(), &bmi, DIB_RGB_COLORS);
}

// Crops `frame` to `crop` (source pixels), scales the result to `size`, and
// draws it at `dest`.
void drawScaled(HDC dc, IWICBitmapSource *frame, WICRect crop, SIZE size, POINT dest)
{
    if (size.cx <= 0 || size.cy <= 0 || crop.Width <= 0 || crop.Height <= 0)
        return;
    ComPtr<IWICBitmapClipper> clipper;
    ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(wicFactory()->CreateBitmapClipper(&clipper)) || FAILED(clipper->Initialize(frame, &crop))
        || FAILED(wicFactory()->CreateBitmapScaler(&scaler))
        || FAILED(scaler->Initialize(clipper.Get(), UINT(size.cx), UINT(size.cy),
                                     WICBitmapInterpolationModeHighQualityCubic))) {
        return;
    }
    blit(dc, scaler.Get(), dest);
}

// Draws the image into one target rectangle (a monitor, or the whole virtual
// screen for Span), in client coordinates.
void drawInto(HDC dc, IWICBitmapSource *frame, UINT iw, UINT ih, const RECT &target, Fit fit)
{
    const int tw = target.right - target.left;
    const int th = target.bottom - target.top;
    if (tw <= 0 || th <= 0)
        return;

    const int saved = SaveDC(dc);
    IntersectClipRect(dc, target.left, target.top, target.right, target.bottom);

    switch (fit) {
    case Fit::Fill:
    case Fit::Span: {
        // Scale to cover, crop the overflow evenly from both sides.
        const double scale = std::max(double(tw) / iw, double(th) / ih);
        const int cw = std::clamp(int(tw / scale), 1, int(iw));
        const int ch = std::clamp(int(th / scale), 1, int(ih));
        const WICRect crop{(int(iw) - cw) / 2, (int(ih) - ch) / 2, cw, ch};
        drawScaled(dc, frame, crop, {tw, th}, {target.left, target.top});
        break;
    }
    case Fit::Fit: {
        const double scale = std::min(double(tw) / iw, double(th) / ih);
        const SIZE size{std::max(1, int(iw * scale)), std::max(1, int(ih * scale))};
        drawScaled(dc, frame, {0, 0, int(iw), int(ih)}, size,
                   {target.left + (tw - size.cx) / 2, target.top + (th - size.cy) / 2});
        break;
    }
    case Fit::Stretch:
        drawScaled(dc, frame, {0, 0, int(iw), int(ih)}, {tw, th}, {target.left, target.top});
        break;
    case Fit::Center: {
        // Original size; crop whatever doesn't fit.
        const int cw = std::min(int(iw), tw);
        const int ch = std::min(int(ih), th);
        const WICRect crop{(int(iw) - cw) / 2, (int(ih) - ch) / 2, cw, ch};
        drawScaled(dc, frame, crop, {cw, ch}, {target.left + (tw - cw) / 2, target.top + (th - ch) / 2});
        break;
    }
    case Fit::Tile:
        for (int y = target.top; y < target.bottom; y += int(ih)) {
            for (int x = target.left; x < target.right; x += int(iw))
                blit(dc, frame, {x, y});
        }
        break;
    }

    RestoreDC(dc, saved);
}

} // namespace

void paintWallpaper(HDC__ *dc, const tagRECT &paintRect)
{
    const Settings settings = readSettings();

    HBRUSH background = CreateSolidBrush(settings.background);
    FillRect(dc, &paintRect, background);
    DeleteObject(background);

    if (settings.path.isEmpty() || !wicFactory())
        return;

    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    const HRESULT hr = wicFactory()->CreateDecoderFromFilename(reinterpret_cast<LPCWSTR>(settings.path.utf16()),
                                                                nullptr, GENERIC_READ,
                                                                WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || FAILED(decoder->GetFrame(0, &frame))) {
        qWarning() << "cannot decode wallpaper" << settings.path << Qt::hex << hr;
        return;
    }
    UINT iw = 0, ih = 0;
    frame->GetSize(&iw, &ih);
    if (!iw || !ih)
        return;

    // Client coordinates are screen coordinates shifted by the virtual
    // screen's origin (the desktop window covers the virtual screen).
    const POINT origin{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN)};
    std::vector<RECT> targets;
    if (settings.fit == Fit::Span) {
        targets.push_back({0, 0, GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN)});
    } else {
        EnumDisplayMonitors(
            nullptr, nullptr,
            [](HMONITOR, HDC, LPRECT rc, LPARAM data) -> BOOL {
                reinterpret_cast<std::vector<RECT> *>(data)->push_back(*rc);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&targets));
        for (RECT &rc : targets)
            OffsetRect(&rc, -origin.x, -origin.y);
    }

    for (const RECT &target : targets) {
        RECT overlap;
        if (IntersectRect(&overlap, &target, &paintRect))
            drawInto(dc, frame.Get(), iw, ih, target, settings.fit);
    }
}

} // namespace visor
