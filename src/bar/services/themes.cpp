#include "services/themes.h"

#include "common/launch.h"

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>

#include <windows.h>
#include <dwmapi.h>

using visor::theme::Rgb;
using visor::theme::Theme;

// Windows tells every top-level window when the personalisation changes:
// WM_SETTINGCHANGE with "ImmersiveColorSet" for the mode and accent (the
// broadcast Settings, and our apply(), send), SPI_SETDESKWALLPAPER for the
// wallpaper, WM_DWMCOLORIZATIONCOLORCHANGED from DWM. Visor's bars receive
// them like any window; this filter sees them first.
struct Themes::Filter : QAbstractNativeEventFilter
{
    explicit Filter(Themes *owner)
        : owner(owner)
    {
    }

    bool nativeEventFilter(const QByteArray &, void *message, qintptr *) override
    {
        const auto *msg = static_cast<const MSG *>(message);
        bool relevant = msg->message == WM_DWMCOLORIZATIONCOLORCHANGED || msg->message == WM_THEMECHANGED;
        if (msg->message == WM_SETTINGCHANGE) {
            const auto *what = reinterpret_cast<const wchar_t *>(msg->lParam);
            relevant = msg->wParam == SPI_SETDESKWALLPAPER
                       || (what && wcscmp(what, L"ImmersiveColorSet") == 0);
        }
        if (relevant)
            owner->windowsChanged();
        return false;
    }

    Themes *owner;
};

Themes::Themes(QObject *parent)
    : QAbstractListModel(parent)
    , m_filter(std::make_unique<Filter>(this))
{
    // Several windows get each broadcast, and Settings sends a few in a row.
    m_syncTimer.setSingleShot(true);
    m_syncTimer.setInterval(100);
    connect(&m_syncTimer, &QTimer::timeout, this, &Themes::sync);
    m_themesTimer.setSingleShot(true);
    m_themesTimer.setInterval(500);
    connect(&m_themesTimer, &QTimer::timeout, this, &Themes::refresh);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_themesTimer.start(); });

    m_windows = visor::theme::personalization();
    m_current = visor::theme::currentThemePath();
    m_themes = visor::theme::loadAll();
    QCoreApplication::instance()->installNativeEventFilter(m_filter.get());
    watch();
}

Themes::~Themes()
{
    QCoreApplication::instance()->removeNativeEventFilter(m_filter.get());
}

// The taskbar's palette, roughly: Windows 11's dark and light surfaces and
// text, with the accent for highlights. The config applies its own alpha.
QColor Themes::background() const
{
    return color(light() ? 0xf3f3f3 : 0x202020);
}

QColor Themes::surface() const
{
    return color(light() ? 0xdedede : 0x383838);
}

QColor Themes::text() const
{
    return color(light() ? 0x1b1b1b : 0xffffff);
}

QColor Themes::subtext() const
{
    return color(light() ? 0x5f5f5f : 0xc5c5c5);
}

QString Themes::currentName() const
{
    const int row = indexOf(m_current);
    if (row >= 0)
        return m_themes[row].name;
    // Current but not listed (e.g. a theme pack we don't look in).
    const Theme t = Theme::load(m_current);
    return t.valid() ? t.name : QString();
}

int Themes::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant Themes::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_themes.size())
        return {};
    const Theme &t = m_themes[index.row()];
    switch (role) {
    case KeyRole:
        return t.path;
    case NameRole:
        return t.name;
    case LightRole:
        return t.systemLight;
    case AccentRole:
        return color(t.hasAccent ? t.accent : m_windows.accent);
    case WallpaperRole:
        return t.wallpaper;
    case CurrentRole:
        return t.path.compare(m_current, Qt::CaseInsensitive) == 0;
    case SourceRole: {
        const QStringList roots = visor::theme::themeRoots();
        if (t.path.startsWith(roots.value(1), Qt::CaseInsensitive))
            return QStringLiteral("Windows");
        if (t.portable || !t.source.isEmpty())
            return QStringLiteral("Visor");
        return QString();
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> Themes::roleNames() const
{
    return {
        {KeyRole, "key"},           {NameRole, "name"},       {LightRole, "light"},   {AccentRole, "accent"},
        {WallpaperRole, "wallpaper"}, {CurrentRole, "current"}, {SourceRole, "source"},
    };
}

int Themes::indexOf(const QString &key) const
{
    const QString path = QDir::fromNativeSeparators(key);
    for (qsizetype i = 0; i < m_themes.size(); ++i) {
        const Theme &t = m_themes[i];
        // An installed copy stands for the shipped theme it came from.
        if (t.path.compare(path, Qt::CaseInsensitive) == 0
            || (!t.source.isEmpty() && t.source.compare(path, Qt::CaseInsensitive) == 0))
            return int(i);
    }
    for (qsizetype i = 0; i < m_themes.size(); ++i) {
        if (m_themes[i].name.compare(key, Qt::CaseInsensitive) == 0)
            return int(i);
    }
    return -1;
}

void Themes::apply(const QString &key)
{
    const int row = indexOf(key);
    const Theme theme = row >= 0 ? m_themes[row] : Theme::load(key);
    if (!theme.valid()) {
        qWarning().noquote() << "theme: no theme" << key;
        return;
    }
    // Registry writes, a broadcast to every window and the Terminal file:
    // off the UI thread. Windows' broadcast then brings the colours here.
    visor::runDetached([theme] { visor::theme::apply(theme); });
}

void Themes::next()
{
    step(1);
}

void Themes::previous()
{
    step(-1);
}

void Themes::step(int delta)
{
    if (m_themes.isEmpty())
        return;
    const int row = indexOf(m_current);
    const int n = count();
    apply(m_themes[row < 0 ? 0 : (row + delta + n) % n].path);
}

void Themes::refresh()
{
    beginResetModel();
    m_themes = visor::theme::loadAll();
    endResetModel();
    emit themesChanged();
    watch();
}

void Themes::windowsChanged()
{
    m_syncTimer.start();
}

void Themes::sync()
{
    const visor::theme::Personalization now = visor::theme::personalization();
    if (now.systemLight != m_windows.systemLight || now.appsLight != m_windows.appsLight
        || now.accent != m_windows.accent || now.wallpaper != m_windows.wallpaper) {
        m_windows = now;
        qInfo().noquote() << "theme: Windows is now" << (now.systemLight ? "light" : "dark") << "with accent"
                          << visor::theme::hex(now.accent);
        emit colorsChanged();
    }
    const QString current = visor::theme::currentThemePath();
    if (current != m_current) {
        m_current = current;
        // Applying a shipped theme installs a copy, which belongs in the list.
        const QList<Theme> themes = visor::theme::loadAll();
        if (themes.size() != m_themes.size()) {
            beginResetModel();
            m_themes = themes;
            endResetModel();
            emit themesChanged();
            watch();
        }
        emit currentChanged();
        if (!m_themes.isEmpty())
            emit dataChanged(index(0), index(count() - 1), {CurrentRole});
    }
}

void Themes::watch()
{
    const QStringList dirs = m_watcher.directories();
    if (!dirs.isEmpty())
        m_watcher.removePaths(dirs);
    QStringList paths;
    for (const QString &root : visor::theme::themeRoots()) {
        if (QFileInfo::exists(root))
            paths << root;
    }
    if (!paths.isEmpty())
        m_watcher.addPaths(paths);
}
