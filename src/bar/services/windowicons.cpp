#include "services/windowicons.h"

#include <windows.h>
#include <shellapi.h>

namespace {

// WM_GETICON goes to the window's thread; don't wait on a hung app.
HICON sentIcon(HWND hwnd, WPARAM which)
{
    DWORD_PTR icon = 0;
    if (!SendMessageTimeoutW(hwnd, WM_GETICON, which, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &icon))
        return nullptr;
    return reinterpret_cast<HICON>(icon);
}

QString processPath(HWND hwnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return {};
    wchar_t buffer[MAX_PATH * 2];
    DWORD size = DWORD(std::size(buffer));
    QString path;
    if (QueryFullProcessImageNameW(process, 0, buffer, &size))
        path = QString::fromWCharArray(buffer, int(size));
    CloseHandle(process);
    return path;
}

} // namespace

WindowIconProvider::WindowIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage WindowIconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    // The id may carry a "?n" suffix to force a refresh; only the hwnd matters.
    const auto hwnd = reinterpret_cast<HWND>(quintptr(id.section(u'?', 0, 0).toULongLong()));
    const bool large = requestedSize.width() > GetSystemMetrics(SM_CXSMICON)
                       || requestedSize.height() > GetSystemMetrics(SM_CYSMICON);

    QImage image;
    if (IsWindow(hwnd)) {
        HICON icon = large ? sentIcon(hwnd, ICON_BIG) : sentIcon(hwnd, ICON_SMALL2);
        if (!icon)
            icon = large ? sentIcon(hwnd, ICON_SMALL2) : sentIcon(hwnd, ICON_BIG);
        if (!icon)
            icon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, large ? GCLP_HICON : GCLP_HICONSM));
        if (!icon)
            icon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, large ? GCLP_HICONSM : GCLP_HICON));
        if (icon) {
            image = QImage::fromHICON(icon); // borrowed; owned by the window
        } else {
            // Many apps only have the icon in their executable.
            const QString path = processPath(hwnd);
            SHFILEINFOW info{};
            if (!path.isEmpty()
                && SHGetFileInfoW(reinterpret_cast<LPCWSTR>(path.utf16()), 0, &info, sizeof(info),
                                  SHGFI_ICON | (large ? SHGFI_LARGEICON : SHGFI_SMALLICON))
                && info.hIcon) {
                image = QImage::fromHICON(info.hIcon);
                DestroyIcon(info.hIcon);
            }
        }
    }

    if (!image.isNull() && requestedSize.isValid() && !requestedSize.isEmpty() && image.size() != requestedSize)
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (size)
        *size = image.size();
    return image;
}

TrayIconProvider::TrayIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage TrayIconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const auto icon = reinterpret_cast<HICON>(quintptr(id.toULongLong()));
    QImage image;
    ICONINFO info{};
    // Guards against a handle visor-shell has already replaced.
    if (icon && GetIconInfo(icon, &info)) {
        if (info.hbmColor)
            DeleteObject(info.hbmColor);
        if (info.hbmMask)
            DeleteObject(info.hbmMask);
        image = QImage::fromHICON(icon);
    }
    if (!image.isNull() && requestedSize.isValid() && !requestedSize.isEmpty() && image.size() != requestedSize)
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (size)
        *size = image.size();
    return image;
}
