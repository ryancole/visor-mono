#include "services/apps.h"

#include "services/appindex.h"
#include "services/shelllink.h"

#include <QImage>

#include <windows.h>
#include <shobjidl_core.h>
#include <shlobj.h>

#include <algorithm>

namespace {

bool launchable(const AppIndex::App &app)
{
    if (!app.packaged || !app.launchPath.isEmpty())
        return true;
    const ShellLink *link = ShellLink::instance();
    return !link || link->mode() != QLatin1String("replace");
}

} // namespace

Apps::Apps(QObject *parent)
    : QAbstractListModel(parent)
{
    AppIndex *index = AppIndex::instance();
    if (!index)
        return;
    index->ensureIndexed();
    rebuild();
    connect(index, &AppIndex::changed, this, [this] {
        rebuild();
        emit readyChanged();
    });
    // Replace mode decides which packaged apps are launchable.
    if (ShellLink *link = ShellLink::instance())
        connect(link, &ShellLink::connectedChanged, this, &Apps::rebuild);
}

void Apps::setQuery(const QString &query)
{
    if (m_query == query)
        return;
    m_query = query;
    rebuild();
    emit queryChanged();
}

bool Apps::ready() const
{
    return AppIndex::instance() && AppIndex::instance()->ready();
}

int Apps::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant Apps::data(const QModelIndex &index, int role) const
{
    const AppIndex *apps = AppIndex::instance();
    if (!apps || !index.isValid() || index.row() >= m_rows.size())
        return {};
    const AppIndex::App &app = apps->apps()[m_rows[index.row()]];
    switch (role) {
    case KeyRole:
        return app.key;
    case NameRole:
        return app.name;
    case IdRole:
        return app.id;
    case IconRole:
        return QStringLiteral("image://visor-app-icon/%1").arg(app.key);
    case PackagedRole:
        return app.packaged;
    case LaunchableRole:
        return launchable(app);
    default:
        return {};
    }
}

QHash<int, QByteArray> Apps::roleNames() const
{
    return {
        {KeyRole, "key"},           {NameRole, "name"},   {IdRole, "id"},   {IconRole, "icon"},
        {PackagedRole, "packaged"}, {LaunchableRole, "launchable"},
    };
}

void Apps::launch(int row, bool asAdmin)
{
    AppIndex *apps = AppIndex::instance();
    if (!apps || row < 0 || row >= m_rows.size())
        return;
    apps->launch(apps->apps()[m_rows[row]].key, asAdmin);
}

void Apps::refresh()
{
    if (AppIndex *apps = AppIndex::instance())
        apps->refresh();
}

int Apps::fuzzyScore(const QString &query, const QString &name)
{
    if (query.isEmpty())
        return 1;
    const QString q = query.toLower();
    const QString n = name.toLower();
    int score = 0;
    int qi = 0;
    int previous = -2;
    for (int ni = 0; ni < n.size() && qi < q.size(); ++ni) {
        if (n[ni] != q[qi])
            continue;
        int bonus = 1;
        if (ni == 0)
            bonus += 12;
        else if (!n[ni - 1].isLetterOrNumber())
            bonus += 8; // start of a word
        if (ni == previous + 1)
            bonus += 4; // consecutive
        score += bonus;
        previous = ni;
        ++qi;
    }
    if (qi < q.size())
        return 0;
    if (n.startsWith(q))
        score += 20;
    // Between equals, the shorter name is the likelier one.
    return std::max(1, score - int(n.size() - q.size()) / 8);
}

void Apps::rebuild()
{
    const AppIndex *index = AppIndex::instance();
    beginResetModel();
    m_rows.clear();
    if (index) {
        const QList<AppIndex::App> &apps = index->apps();
        if (m_query.isEmpty()) {
            // Everything A-Z (the index is sorted).
            for (int i = 0; i < apps.size(); ++i)
                m_rows.append(i);
        } else {
            struct Hit
            {
                int row;
                int score;
            };
            QList<Hit> hits;
            for (int i = 0; i < apps.size(); ++i) {
                if (const int score = fuzzyScore(m_query, apps[i].name))
                    hits.append({i, score});
            }
            // Best match first; apps that can't open sink below those that
            // can; use, then the name, breaks ties.
            std::stable_sort(hits.begin(), hits.end(), [&apps](const Hit &a, const Hit &b) {
                const AppIndex::App &x = apps[a.row];
                const AppIndex::App &y = apps[b.row];
                const bool lx = launchable(x);
                const bool ly = launchable(y);
                if (lx != ly)
                    return lx;
                if (a.score != b.score)
                    return a.score > b.score;
                if (x.launches != y.launches)
                    return x.launches > y.launches;
                return std::tie(y.lastLaunch, x.name) < std::tie(x.lastLaunch, y.name);
            });
            for (const Hit &h : std::as_const(hits))
                m_rows.append(h.row);
        }
    }
    endResetModel();
    emit countChanged();
}

// ---- Desktop ----------------------------------------------------------------

DesktopItems::DesktopItems(QObject *parent)
    : QAbstractListModel(parent)
{
    AppIndex *index = AppIndex::instance();
    if (!index)
        return;
    index->ensureIndexed();
    connect(index, &AppIndex::changed, this, [this] {
        beginResetModel();
        endResetModel();
        emit countChanged();
    });
}

int DesktopItems::count() const
{
    return AppIndex::instance() ? int(AppIndex::instance()->desktop().size()) : 0;
}

int DesktopItems::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant DesktopItems::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= count())
        return {};
    const AppIndex::App &item = AppIndex::instance()->desktop()[index.row()];
    switch (role) {
    case KeyRole:
        return item.key;
    case NameRole:
        return item.name;
    case IdRole:
        return item.id;
    case IconRole:
        return QStringLiteral("image://visor-app-icon/%1").arg(item.key);
    default:
        return {};
    }
}

QHash<int, QByteArray> DesktopItems::roleNames() const
{
    return {{KeyRole, "key"}, {NameRole, "name"}, {IdRole, "id"}, {IconRole, "icon"}};
}

void DesktopItems::launch(int row, bool asAdmin)
{
    if (row < 0 || row >= count())
        return;
    AppIndex *index = AppIndex::instance();
    index->launch(index->desktop()[row].key, asAdmin);
}

// ---- Icons ------------------------------------------------------------------

AppIconProvider::AppIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

namespace {

// A 32-bit DIB with alpha, as IShellItemImageFactory returns.
QImage imageFromBitmap(HBITMAP bitmap)
{
    BITMAP bm{};
    if (!GetObjectW(bitmap, sizeof(bm), &bm) || bm.bmBitsPixel != 32)
        return {};
    QImage image(bm.bmWidth, bm.bmHeight, QImage::Format_ARGB32_Premultiplied);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = bm.bmWidth;
    bi.bmiHeader.biHeight = -bm.bmHeight; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    const HDC dc = GetDC(nullptr);
    const int lines = GetDIBits(dc, bitmap, 0, bm.bmHeight, image.bits(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    return lines == bm.bmHeight ? image : QImage();
}

} // namespace

QImage AppIconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QByteArray pidl = AppIndex::pidlFor(id.toInt());
    if (pidl.isEmpty())
        return {};
    const int px = requestedSize.isValid() && !requestedSize.isEmpty() ? std::max(requestedSize.width(), requestedSize.height()) : 32;

    QImage image;
    // The provider runs on Qt's image threads; COM must be set up on each.
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IShellItemImageFactory *factory = nullptr;
    if (SUCCEEDED(SHCreateItemFromIDList(reinterpret_cast<PCIDLIST_ABSOLUTE>(pidl.constData()),
                                         IID_PPV_ARGS(&factory)))) {
        HBITMAP bitmap = nullptr;
        if (SUCCEEDED(factory->GetImage({px, px}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &bitmap)) && bitmap) {
            image = imageFromBitmap(bitmap);
            DeleteObject(bitmap);
        }
        factory->Release();
    }
    if (SUCCEEDED(hr))
        CoUninitialize();

    if (!image.isNull() && requestedSize.isValid() && !requestedSize.isEmpty() && image.size() != requestedSize)
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (size)
        *size = image.size();
    return image;
}
