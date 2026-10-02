#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// Theming, the Windows way: Windows owns the wallpaper, the dark/light mode
// and the accent colour (Settings > Personalization), every app follows them,
// and a theme is a .theme file naming all three. Visor follows them too
// (personalization()), and switches them by applying a .theme file (apply),
// which is what Settings does when you pick a theme: in replace mode Settings
// can't run, so the switcher is Visor's.
//
// .theme files are looked up in Windows' own folder (%SystemRoot%\Resources\
// Themes: Windows light, dark, ...), the user's (%LOCALAPPDATA%\Microsoft\
// Windows\Themes, where Settings saves themes and theme packs unpack) and the
// ones shipped next to the exe (config/themes). Of a .theme we apply the
// wallpaper and its fit, SystemMode / AppMode and ColorizationColor; sounds,
// cursors and desktop icons are left alone. Two additions of ours:
//   - a Wallpaper path relative to the .theme file (Windows wants absolute
//     ones). Applying such a theme first installs an absolute-path copy in
//     the user's theme folder, so Settings shows it like any other;
//   - <name>.terminal.json next to the .theme: a Windows Terminal colour
//     scheme (the object you would put in settings.json's "schemes"), which is
//     written there and made the default profiles' scheme. Windows has no
//     convention for terminal palettes; this one is Omarchy's.
namespace visor::theme {

using Rgb = quint32; // 0xRRGGBB

QString hex(Rgb rgb); // "#rrggbb"

// A .theme file, the parts we read.
struct Theme
{
    QString path; // the .theme file
    QString name; // DisplayName, resolved when it is a resource string
    QString wallpaper;         // absolute path, or empty to leave it alone
    int wallpaperStyle = 10;   // HKCU\Control Panel\Desktop WallpaperStyle (10 = Fill)
    bool tileWallpaper = false;
    bool systemLight = true;   // SystemMode (the taskbar, Start; and Visor's bar)
    bool appsLight = true;     // AppMode
    bool hasAccent = false;    // ColorizationColor given and AutoColorization off
    Rgb accent = 0x0078d4;
    QString terminalScheme;    // <name>.terminal.json, if it exists
    bool portable = false;     // the Wallpaper path was relative (a shipped theme)
    QString source;            // for an installed copy: the shipped file it came from

    bool valid() const { return !path.isEmpty(); }

    static Theme load(const QString &path);
};

// The theme folders, in the order they are searched.
QStringList themeRoots();
// Every .theme found, sorted by name; a theme whose name was seen in an
// earlier folder is left out, so Visor's installed copies stand in for the
// shipped originals.
QList<Theme> loadAll();
// HKCU\...\Themes\CurrentTheme: the .theme last applied (by Settings or us).
QString currentThemePath();

// What Windows has now: the source of truth for Visor's colours.
struct Personalization
{
    bool systemLight = false;
    bool appsLight = false;
    Rgb accent = 0x0078d4;
    QString wallpaper;
};
Personalization personalization();

// Applies a theme the way Settings would: wallpaper (SPI_SETDESKWALLPAPER, so
// Explorer or visor-shell repaints), modes and accent (the registry values
// Settings writes, then the "ImmersiveColorSet" broadcast that apps, DWM and
// Visor itself listen for), CurrentTheme, and the Terminal scheme when there
// is one. Broadcasts to every window, so call it off the UI thread. Problems
// are logged, not fatal.
void apply(const Theme &theme);

} // namespace visor::theme
