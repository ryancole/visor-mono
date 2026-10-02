#pragma once

#include "common/theme.h"

#include <QAbstractListModel>
#include <QColor>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <memory>

// Windows' personalisation, as seen from QML, and the themes to switch it
// with. Windows is the source of truth: the colours here are derived from its
// dark/light mode and accent colour (the way the taskbar's are), and change
// the moment Windows does, whether through Settings, a .theme file or
// apply(). The config's Theme.qml builds on them:
//
//   readonly property color background: Qt.alpha(Themes.background, 0.9)
//   ListView { model: Themes; delegate: Text { required property string name; ... } }
//   MouseArea { onClicked: Themes.apply(key) }
//
// The model lists .theme files: Windows' own, the user's (what Settings
// saves) and the ones shipped with Visor (see common/theme.h). apply() does
// what picking a theme in Settings does; in replace mode Settings can't run,
// so this is the switcher.
//
// Roles: key (the .theme path), name, light (the system mode it sets),
// accent, wallpaper (a path, or ""), current, source ("Windows", "Visor" or
// "" for the user's own).
class Themes : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int count READ count NOTIFY themesChanged)
    // The .theme Windows records as current, or "" (its name likewise).
    Q_PROPERTY(QString current READ current NOTIFY currentChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentChanged)
    // Windows' system mode (taskbar, Start: what the bar follows) and app mode.
    Q_PROPERTY(bool light READ light NOTIFY colorsChanged)
    Q_PROPERTY(bool appsLight READ appsLight NOTIFY colorsChanged)
    // Derived from the mode and accent, like the taskbar's own colours.
    Q_PROPERTY(QColor background READ background NOTIFY colorsChanged)
    Q_PROPERTY(QColor surface READ surface NOTIFY colorsChanged)
    Q_PROPERTY(QColor text READ text NOTIFY colorsChanged)
    Q_PROPERTY(QColor subtext READ subtext NOTIFY colorsChanged)
    Q_PROPERTY(QColor accent READ accent NOTIFY colorsChanged)
    Q_PROPERTY(QString wallpaper READ wallpaper NOTIFY colorsChanged)

public:
    enum Role {
        KeyRole = Qt::UserRole + 1,
        NameRole,
        LightRole,
        AccentRole,
        WallpaperRole,
        CurrentRole,
        SourceRole,
    };

    explicit Themes(QObject *parent = nullptr);
    ~Themes() override;

    int count() const { return int(m_themes.size()); }
    QString current() const { return m_current; }
    QString currentName() const;
    bool light() const { return m_windows.systemLight; }
    bool appsLight() const { return m_windows.appsLight; }
    QColor background() const;
    QColor surface() const;
    QColor text() const;
    QColor subtext() const;
    QColor accent() const { return color(m_windows.accent); }
    QString wallpaper() const { return m_windows.wallpaper; }

    // Applies a .theme (its path, the `key` role, or its name, e.g. "Nord" or
    // "Windows (dark)"): wallpaper, mode, accent and, for Visor's themes, the
    // Terminal scheme. Runs in the background; the colours here follow as
    // Windows takes it.
    Q_INVOKABLE void apply(const QString &key);
    // The next / previous theme in the list, wrapping (Omarchy's
    // Super+Ctrl+Shift+Space); the first if the current one isn't listed.
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    // The row of a theme, by path or name, or -1.
    Q_INVOKABLE int indexOf(const QString &key) const;
    // Re-reads the theme folders (they're watched, so rarely needed).
    Q_INVOKABLE void refresh();

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Called by the native event filter when Windows says its colours or
    // wallpaper changed.
    void windowsChanged();

signals:
    void themesChanged();
    void currentChanged();
    void colorsChanged();

private:
    struct Filter;

    static QColor color(visor::theme::Rgb rgb) { return QColor::fromRgb(int(rgb)); }
    void step(int delta);
    // Re-reads what Windows has and the current theme, and tells QML what moved.
    void sync();
    void watch();

    QList<visor::theme::Theme> m_themes;
    visor::theme::Personalization m_windows;
    QString m_current; // CurrentTheme, as a path
    std::unique_ptr<Filter> m_filter;
    QFileSystemWatcher m_watcher;
    QTimer m_syncTimer;   // coalesces Windows' broadcasts
    QTimer m_themesTimer; // coalesces theme-folder changes
};
