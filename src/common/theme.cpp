// C++/WinRT first: Qt's keyword macros must not leak into its headers.
#include <unknwn.h>
#include <winrt/base.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.ViewManagement.h>

#include "common/theme.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>

#include <windows.h>
#include <shlwapi.h>

#include <algorithm>
#include <cctype>

namespace visor::theme {

namespace {

const wchar_t kPersonalizeKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
const wchar_t kThemesKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes";
const wchar_t kAccentKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent";
const wchar_t kDwmKey[] = L"Software\\Microsoft\\Windows\\DWM";
const wchar_t kDesktopKey[] = L"Control Panel\\Desktop";

// ---- Registry and INI helpers ---------------------------------------------------

QString regString(const wchar_t *key, const wchar_t *name)
{
    wchar_t buffer[2048] = {};
    DWORD size = sizeof(buffer);
    if (RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS)
        return {};
    return QString::fromWCharArray(buffer);
}

bool regDword(const wchar_t *key, const wchar_t *name, DWORD *out)
{
    DWORD size = sizeof(*out);
    return RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_DWORD, nullptr, out, &size) == ERROR_SUCCESS;
}

void setDword(const wchar_t *key, const wchar_t *name, DWORD value)
{
    const LSTATUS status = RegSetKeyValueW(HKEY_CURRENT_USER, key, name, REG_DWORD, &value, sizeof(value));
    if (status != ERROR_SUCCESS)
        qWarning() << "theme: cannot set" << QString::fromWCharArray(key) << QString::fromWCharArray(name) << status;
}

void setBinary(const wchar_t *key, const wchar_t *name, const QByteArray &data)
{
    const LSTATUS status =
        RegSetKeyValueW(HKEY_CURRENT_USER, key, name, REG_BINARY, data.constData(), DWORD(data.size()));
    if (status != ERROR_SUCCESS)
        qWarning() << "theme: cannot set" << QString::fromWCharArray(key) << QString::fromWCharArray(name) << status;
}

void setString(const wchar_t *key, const wchar_t *name, const QString &value)
{
    const std::wstring w = value.toStdWString();
    const LSTATUS status =
        RegSetKeyValueW(HKEY_CURRENT_USER, key, name, REG_SZ, w.c_str(), DWORD((w.size() + 1) * sizeof(wchar_t)));
    if (status != ERROR_SUCCESS)
        qWarning() << "theme: cannot set" << QString::fromWCharArray(key) << QString::fromWCharArray(name) << status;
}

// .theme files are INI files, ANSI or UTF-16; the profile API reads both.
QString iniValue(const QString &file, const wchar_t *section, const wchar_t *key)
{
    wchar_t buffer[2048] = {};
    GetPrivateProfileStringW(section, key, L"", buffer, DWORD(std::size(buffer)), file.toStdWString().c_str());
    return QString::fromWCharArray(buffer);
}

// %SystemRoot% and friends, as Windows expands them in these files.
QString expand(const QString &value)
{
    wchar_t buffer[2048] = {};
    if (!ExpandEnvironmentStringsW(value.toStdWString().c_str(), buffer, DWORD(std::size(buffer))))
        return value;
    return QString::fromWCharArray(buffer);
}

// "@%SystemRoot%\System32\themeui.dll,-2114" -> "Windows (dark)".
QString resolveIndirect(const QString &value)
{
    if (!value.startsWith(QLatin1Char('@')))
        return value;
    wchar_t buffer[512] = {};
    if (SUCCEEDED(SHLoadIndirectString(expand(value).toStdWString().c_str(), buffer, DWORD(std::size(buffer)), nullptr)))
        return QString::fromWCharArray(buffer);
    return {};
}

bool isImage(const QFileInfo &info)
{
    static const QStringList suffixes{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                      QStringLiteral("bmp"), QStringLiteral("webp"), QStringLiteral("gif")};
    return suffixes.contains(info.suffix().toLower());
}

QString firstImageIn(const QString &dir)
{
    QStringList files;
    QDirIterator it(dir, QDir::Files);
    while (it.hasNext()) {
        const QFileInfo info = it.nextFileInfo();
        if (isImage(info))
            files << info.absoluteFilePath();
    }
    files.sort(Qt::CaseInsensitive);
    return files.value(0);
}

QString userThemesDir()
{
    // %LOCALAPPDATA%\Microsoft\Windows\Themes
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/Microsoft/Windows/Themes");
}

QString windowsThemesDir()
{
    return QDir::fromNativeSeparators(expand(QStringLiteral("%SystemRoot%\\Resources\\Themes")));
}

Rgb mix(Rgb a, Rgb b, double t)
{
    const auto channel = [&](int shift) {
        const double x = double((a >> shift) & 0xff);
        const double y = double((b >> shift) & 0xff);
        return Rgb(std::clamp(int(x + (y - x) * t + 0.5), 0, 255)) << shift;
    };
    return channel(16) | channel(8) | channel(0);
}

// 0xAABBGGRR, how the accent keys store colours.
DWORD abgr(Rgb rgb)
{
    return 0xff000000u | ((rgb & 0xff) << 16) | (rgb & 0xff00) | ((rgb >> 16) & 0xff);
}

// Tells every window (and DWM) that the personalisation settings changed,
// as Settings does. Hung windows are skipped.
void broadcastSettingChange(const wchar_t *what)
{
    DWORD_PTR result = 0;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(what),
                        SMTO_ABORTIFHUNG | SMTO_NOTIMEOUTIFNOTHUNG, 1000, &result);
}

// ---- Applying -----------------------------------------------------------------

// A shipped theme names its wallpaper relative to itself, which Windows
// doesn't understand. Put a copy with absolute paths (and the Terminal scheme)
// where Settings keeps the user's themes, and apply that, so Settings shows
// and can re-apply it like any other theme.
Theme install(const Theme &theme)
{
    const QDir dir(userThemesDir());
    if (!dir.mkpath(QStringLiteral("."))) {
        qWarning().noquote() << "theme: cannot create" << QDir::toNativeSeparators(dir.path());
        return theme;
    }
    const QString base = QFileInfo(theme.path).completeBaseName();
    const QString copy = dir.filePath(base + QStringLiteral(".theme"));

    QFile in(theme.path);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning().noquote() << "theme: cannot read" << QDir::toNativeSeparators(theme.path);
        return theme;
    }
    QStringList lines = QString::fromUtf8(in.readAll()).split(QLatin1Char('\n'));
    for (QString &line : lines) {
        line = line.trimmed();
        if (line.startsWith(QLatin1String("Wallpaper="), Qt::CaseInsensitive))
            line = QStringLiteral("Wallpaper=") + QDir::toNativeSeparators(theme.wallpaper);
        else if (line.compare(QLatin1String("[Theme]"), Qt::CaseInsensitive) == 0)
            line += QStringLiteral("\r\nVisorSource=") + QDir::toNativeSeparators(theme.path);
    }
    QSaveFile out(copy);
    if (!out.open(QIODevice::WriteOnly)) {
        qWarning().noquote() << "theme: cannot write" << QDir::toNativeSeparators(copy) << out.errorString();
        return theme;
    }
    {
        // UTF-16 with a BOM: what the profile API reads for any path.
        QTextStream stream(&out);
        stream.setEncoding(QStringConverter::Utf16LE);
        stream.setGenerateByteOrderMark(true);
        stream << lines.join(QStringLiteral("\r\n"));
    }
    if (!out.commit()) {
        qWarning().noquote() << "theme: cannot write" << QDir::toNativeSeparators(copy) << out.errorString();
        return theme;
    }
    if (!theme.terminalScheme.isEmpty()) {
        const QString scheme = dir.filePath(base + QStringLiteral(".terminal.json"));
        QFile::remove(scheme);
        QFile::copy(theme.terminalScheme, scheme);
    }
    qInfo().noquote() << "theme: installed" << QDir::toNativeSeparators(copy);
    Theme installed = Theme::load(copy);
    return installed.valid() ? installed : theme;
}

void applyWallpaper(const Theme &theme)
{
    if (theme.wallpaper.isEmpty())
        return;
    setString(kDesktopKey, L"WallpaperStyle", QString::number(theme.wallpaperStyle));
    setString(kDesktopKey, L"TileWallpaper", theme.tileWallpaper ? QStringLiteral("1") : QStringLiteral("0"));
    // Writes HKCU\Control Panel\Desktop\WallPaper and broadcasts the change,
    // so Explorer (hosted) or visor-shell's desktop (replace) repaints.
    std::wstring path = QDir::toNativeSeparators(theme.wallpaper).toStdWString();
    if (!SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, path.data(), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE))
        qWarning() << "theme: SPI_SETDESKWALLPAPER failed, error" << GetLastError();
}

// The values Settings writes for the mode and the accent. Not documented,
// but every app reloads them on the "ImmersiveColorSet" broadcast, and so
// do DWM and Explorer.
void applyColors(const Theme &theme)
{
    setDword(kPersonalizeKey, L"AppsUseLightTheme", theme.appsLight ? 1 : 0);
    setDword(kPersonalizeKey, L"SystemUsesLightTheme", theme.systemLight ? 1 : 0);

    if (theme.hasAccent) {
        const Rgb accent = theme.accent;
        // Settings' palette: three lighter shades, the accent, three darker
        // ones, and a last entry it uses for Start; 4 bytes each (R, G, B, 0).
        QByteArray palette;
        const double steps[] = {-0.6, -0.4, -0.2, 0.0, 0.2, 0.4, 0.6, 0.8};
        for (double t : steps) {
            const Rgb shade = t < 0 ? mix(accent, 0xffffff, -t) : mix(accent, 0x000000, t);
            palette.append(char((shade >> 16) & 0xff));
            palette.append(char((shade >> 8) & 0xff));
            palette.append(char(shade & 0xff));
            palette.append('\0');
        }
        setBinary(kAccentKey, L"AccentPalette", palette);
        setDword(kAccentKey, L"AccentColorMenu", abgr(accent));
        setDword(kAccentKey, L"StartColorMenu", abgr(mix(accent, 0x000000, 0.4)));
        // DWM: window frames and title bars (when "show accent colour on
        // title bars" is on; that stays the user's choice).
        setDword(kDwmKey, L"AccentColor", abgr(accent));
        setDword(kDwmKey, L"ColorizationColor", 0xc4000000u | accent);
        setDword(kDwmKey, L"ColorizationAfterglow", 0xc4000000u | accent);
    }
}

// settings.json allows // and /* */ comments and trailing commas, which
// QJsonDocument doesn't. Terminal itself drops the comments whenever its
// settings UI saves, so losing them here is nothing new.
QByteArray stripJsonc(const QByteArray &in)
{
    QByteArray out;
    out.reserve(in.size());
    bool inString = false;
    for (qsizetype i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (inString) {
            out += c;
            if (c == '\\' && i + 1 < in.size())
                out += in[++i];
            else if (c == '"')
                inString = false;
            continue;
        }
        if (c == '"') {
            inString = true;
            out += c;
        } else if (c == '/' && i + 1 < in.size() && in[i + 1] == '/') {
            while (i < in.size() && in[i] != '\n')
                ++i;
            out += '\n';
        } else if (c == '/' && i + 1 < in.size() && in[i + 1] == '*') {
            i += 2;
            while (i + 1 < in.size() && !(in[i] == '*' && in[i + 1] == '/'))
                ++i;
            ++i;
        } else if (c == ',') {
            // A trailing comma: drop it if only whitespace separates it from
            // the closing bracket.
            qsizetype j = i + 1;
            while (j < in.size() && isspace(static_cast<unsigned char>(in[j])))
                ++j;
            if (j < in.size() && (in[j] == ']' || in[j] == '}'))
                continue;
            out += c;
        } else {
            out += c;
        }
    }
    return out;
}

// Puts the scheme into one settings.json (replacing one of the same name)
// and makes it the default profiles' scheme. Profiles with a scheme of their
// own keep it.
void applyTerminalFile(const QString &path, const QJsonObject &scheme)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(stripJsonc(raw), &error);
    if (!doc.isObject()) {
        qWarning().noquote() << "theme: not changing" << path << "-" << error.errorString();
        return;
    }
    QJsonObject root = doc.object();
    const QString name = scheme.value(QStringLiteral("name")).toString();

    QJsonArray schemes = root.value(QStringLiteral("schemes")).toArray();
    bool replaced = false;
    for (qsizetype i = 0; i < schemes.size(); ++i) {
        if (schemes[i].toObject().value(QStringLiteral("name")).toString() == name) {
            schemes[i] = scheme;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        schemes.append(scheme);
    root.insert(QStringLiteral("schemes"), schemes);

    // profiles is {"defaults": {...}, "list": [...]} (or, in old files, a
    // bare list, which we leave alone).
    const QJsonValue profilesValue = root.value(QStringLiteral("profiles"));
    if (profilesValue.isArray()) {
        qWarning().noquote() << "theme:" << path << "has an old-style profiles list; scheme added but not selected";
    } else {
        QJsonObject profiles = profilesValue.toObject();
        QJsonObject defaults = profiles.value(QStringLiteral("defaults")).toObject();
        defaults.insert(QStringLiteral("colorScheme"), name);
        profiles.insert(QStringLiteral("defaults"), defaults);
        root.insert(QStringLiteral("profiles"), profiles);
    }

    // Once: keep what was there before Visor ever touched it.
    const QString backup = path + QStringLiteral(".before-visor");
    if (!QFileInfo::exists(backup))
        QFile::copy(path, backup);

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        qWarning().noquote() << "theme: cannot write" << path << out.errorString();
        return;
    }
    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (out.commit())
        qInfo().noquote() << "theme: Terminal scheme" << name << "written to" << QDir::toNativeSeparators(path);
    else
        qWarning().noquote() << "theme: cannot write" << path << out.errorString();
}

void applyTerminal(const Theme &theme)
{
    if (theme.terminalScheme.isEmpty())
        return;
    QFile file(theme.terminalScheme);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning().noquote() << "theme: cannot read" << QDir::toNativeSeparators(theme.terminalScheme);
        return;
    }
    QJsonParseError error{};
    QJsonObject scheme = QJsonDocument::fromJson(stripJsonc(file.readAll()), &error).object();
    if (scheme.isEmpty()) {
        qWarning().noquote() << "theme:" << QDir::toNativeSeparators(theme.terminalScheme) << "-" << error.errorString();
        return;
    }
    if (scheme.value(QStringLiteral("name")).toString().isEmpty())
        scheme.insert(QStringLiteral("name"), theme.name);

    const QString local = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation); // %LOCALAPPDATA%
    const QStringList files{
        local + QStringLiteral("/Packages/Microsoft.WindowsTerminal_8wekyb3d8bbwe/LocalState/settings.json"),
        local + QStringLiteral("/Packages/Microsoft.WindowsTerminalPreview_8wekyb3d8bbwe/LocalState/settings.json"),
        local + QStringLiteral("/Microsoft/Windows Terminal/settings.json"), // unpackaged
    };
    for (const QString &path : files) {
        if (QFileInfo::exists(path))
            applyTerminalFile(path, scheme);
    }
}

} // namespace

// ---- Theme files ----------------------------------------------------------------

QString hex(Rgb rgb)
{
    return QStringLiteral("#%1").arg(rgb & 0xffffff, 6, 16, QLatin1Char('0'));
}

Theme Theme::load(const QString &file)
{
    const QFileInfo info(file);
    if (!info.isFile())
        return {};
    Theme t;
    t.path = info.absoluteFilePath();
    t.name = resolveIndirect(iniValue(file, L"Theme", L"DisplayName"));
    if (t.name.isEmpty())
        t.name = info.completeBaseName();
    t.source = QDir::fromNativeSeparators(expand(iniValue(file, L"Theme", L"VisorSource")));

    // Wallpaper: the named file, else a slideshow folder's first image.
    const QString wallpaper = expand(iniValue(file, L"Control Panel\\Desktop", L"Wallpaper"));
    if (!wallpaper.isEmpty()) {
        QFileInfo w(wallpaper);
        if (w.isRelative()) {
            t.portable = true;
            w = QFileInfo(info.dir().filePath(wallpaper));
        }
        if (w.exists())
            t.wallpaper = w.absoluteFilePath();
    } else {
        const QString folder = expand(iniValue(file, L"Slideshow", L"ImagesRootPath"));
        if (!folder.isEmpty())
            t.wallpaper = firstImageIn(folder);
    }
    // Fit: WallpaperStyle + TileWallpaper (the registry's values), or
    // Settings' own PicturePosition (0 centre, 1 tile, 2 stretch, 3 fit,
    // 4 fill, 5 span).
    const QString style = iniValue(file, L"Control Panel\\Desktop", L"WallpaperStyle");
    const QString position = iniValue(file, L"Control Panel\\Desktop", L"PicturePosition");
    if (!style.isEmpty()) {
        t.wallpaperStyle = style.toInt();
        t.tileWallpaper = iniValue(file, L"Control Panel\\Desktop", L"TileWallpaper").toInt() != 0;
    } else if (!position.isEmpty()) {
        static const int styles[] = {0, 0, 2, 6, 10, 22};
        const int p = std::clamp(position.toInt(), 0, 5);
        t.wallpaperStyle = styles[p];
        t.tileWallpaper = p == 1;
    }

    // Modes default to light when absent, as in Windows' own light theme.
    t.systemLight =
        iniValue(file, L"VisualStyles", L"SystemMode").compare(QLatin1String("Dark"), Qt::CaseInsensitive) != 0;
    t.appsLight = iniValue(file, L"VisualStyles", L"AppMode").compare(QLatin1String("Dark"), Qt::CaseInsensitive) != 0;

    // "0XC40078D4": alpha then RGB. AutoColorization=1 means "from the
    // wallpaper", which we can't compute; the accent is then left alone.
    const QString colorization = iniValue(file, L"VisualStyles", L"ColorizationColor").trimmed();
    const bool automatic = iniValue(file, L"VisualStyles", L"AutoColorization").toInt() == 1;
    if (!automatic && colorization.size() >= 6) {
        bool ok = false;
        const Rgb value = colorization.right(6).toUInt(&ok, 16);
        if (ok) {
            t.accent = value;
            t.hasAccent = true;
        }
    }

    const QString scheme = info.dir().filePath(info.completeBaseName() + QStringLiteral(".terminal.json"));
    if (QFileInfo::exists(scheme))
        t.terminalScheme = scheme;
    return t;
}

QStringList themeRoots()
{
    QStringList roots{userThemesDir(), windowsThemesDir()};
#ifdef VISOR_DEV_CONFIG_DIR
    roots << QStringLiteral(VISOR_DEV_CONFIG_DIR "/themes");
#endif
    roots << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("config/themes"));
    return roots;
}

QList<Theme> loadAll()
{
    const QString current = currentThemePath();
    QList<Theme> themes;
    QStringList names;
    for (const QString &root : themeRoots()) {
        const QString rootPath = QDir(root).absolutePath();
        // Theme packs unpack into a folder of their own, one level down.
        QDirIterator it(root, {QStringLiteral("*.theme")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QFileInfo info = it.nextFileInfo();
            const QString parent = info.dir().absolutePath();
            if (parent != rootPath && QFileInfo(parent).dir().absolutePath() != rootPath)
                continue; // deeper than one level
            // Settings' record of unsaved changes: shown only while in use.
            if (info.fileName().compare(QLatin1String("Custom.theme"), Qt::CaseInsensitive) == 0
                && info.absoluteFilePath().compare(current, Qt::CaseInsensitive) != 0)
                continue;
            // Windows Spotlight (rotating wallpapers from the web) is a
            // service only Settings can switch on, so its theme is left out.
            if (iniValue(info.absoluteFilePath(), L"Control Panel\\Desktop", L"WindowsSpotlight") == QLatin1String("1"))
                continue;
            Theme theme = Theme::load(info.absoluteFilePath());
            if (!theme.valid() || names.contains(theme.name, Qt::CaseInsensitive))
                continue;
            names << theme.name;
            themes.append(std::move(theme));
        }
    }
    std::sort(themes.begin(), themes.end(),
              [](const Theme &a, const Theme &b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; });
    return themes;
}

QString currentThemePath()
{
    const QString path = regString(kThemesKey, L"CurrentTheme");
    return path.isEmpty() ? QString() : QFileInfo(QDir::fromNativeSeparators(expand(path))).absoluteFilePath();
}

// ---- What Windows has -------------------------------------------------------------

Personalization personalization()
{
    Personalization p;
    DWORD value = 0;
    if (regDword(kPersonalizeKey, L"SystemUsesLightTheme", &value))
        p.systemLight = value != 0;
    if (regDword(kPersonalizeKey, L"AppsUseLightTheme", &value))
        p.appsLight = value != 0;

    // The accent, through the API apps are meant to use.
    try {
        using namespace winrt::Windows::UI::ViewManagement;
        const winrt::Windows::UI::Color c = UISettings().GetColorValue(UIColorType::Accent);
        p.accent = (Rgb(c.R) << 16) | (Rgb(c.G) << 8) | Rgb(c.B);
    } catch (const winrt::hresult_error &e) {
        qWarning() << "theme: UISettings failed:" << QString::fromWCharArray(e.message().c_str());
        if (regDword(kDwmKey, L"AccentColor", &value)) // 0xAABBGGRR
            p.accent = ((value & 0xff) << 16) | (value & 0xff00) | ((value >> 16) & 0xff);
    }

    wchar_t path[MAX_PATH * 2] = {};
    if (SystemParametersInfoW(SPI_GETDESKWALLPAPER, DWORD(std::size(path)), path, 0))
        p.wallpaper = QString::fromWCharArray(path);
    return p;
}

// ---- Applying -----------------------------------------------------------------

void apply(const Theme &requested)
{
    if (!requested.valid())
        return;
    qInfo().noquote() << "theme: applying" << requested.name << "from" << QDir::toNativeSeparators(requested.path);
    const Theme theme = requested.portable ? install(requested) : requested;

    applyWallpaper(theme);
    applyColors(theme);
    setString(kThemesKey, L"CurrentTheme", QDir::toNativeSeparators(theme.path));
    broadcastSettingChange(L"ImmersiveColorSet");
    applyTerminal(theme);
    qInfo().noquote() << "theme: applied" << theme.name;
}

} // namespace visor::theme
