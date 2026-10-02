#include "shell/startup.h"

#include "common/launch.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QSettings>

#include <windows.h>
#include <shlobj.h>

#include <iterator>

namespace visor {

namespace {

// Settings > Apps > Startup keeps its switches here: a binary value per
// entry (named as the Run value, or the Startup folder file), whose first
// byte is even when the entry is enabled and odd when it is turned off.
// Both hives are consulted, the user's first.
bool approved(const QString &subkey, const QString &name)
{
    for (const QString &hive : {QStringLiteral("HKEY_CURRENT_USER"), QStringLiteral("HKEY_LOCAL_MACHINE")}) {
        const QSettings s(hive + QStringLiteral("\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\") + subkey,
                          QSettings::NativeFormat);
        const QByteArray data = s.value(name).toByteArray();
        if (!data.isEmpty())
            return (data[0] & 1) == 0;
    }
    return true;
}

// Run values are often REG_EXPAND_SZ (%windir%\\system32\\...).
QString expand(const QString &text)
{
    wchar_t out[MAX_PATH * 4] = {};
    if (!ExpandEnvironmentStringsW(text.toStdWString().c_str(), out, DWORD(std::size(out))))
        return text;
    return QString::fromWCharArray(out);
}

QString knownFolder(const KNOWNFOLDERID &id)
{
    PWSTR path = nullptr;
    QString result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path)))
        result = QString::fromWCharArray(path);
    CoTaskMemFree(path);
    return result;
}

// Once per sign-in: a volatile key lives until the user's hive is unloaded
// at sign-out, and no longer.
bool firstRunThisSession()
{
    HKEY key = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\visor-shell\\startup-ran", 0, nullptr, REG_OPTION_VOLATILE,
                        KEY_WRITE, nullptr, &key, &disposition)
        != ERROR_SUCCESS)
        return true;
    RegCloseKey(key);
    return disposition == REG_CREATED_NEW_KEY;
}

} // namespace

Startup::Startup(QObject *parent)
    : QObject(parent)
{
}

void Startup::run()
{
    if (!firstRunThisSession()) {
        qInfo() << "startup programs already run this sign-in";
        return;
    }
    // Explorer's order: RunOnce, then Run (machine, then user, then the
    // policies), then the Startup folders.
    runRegistry(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce"), true);
    for (const QString &key :
         {QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
          QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run"),
          QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
          QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\Run"),
          QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\Run")})
        runRegistry(key, false);
    runFolder(knownFolder(FOLDERID_CommonStartup));
    runFolder(knownFolder(FOLDERID_Startup));
}

void Startup::runRegistry(const QString &key, bool once)
{
    QSettings s(key, QSettings::NativeFormat);
    const QStringList names = s.childKeys();
    const QString approvedKey = key.contains(QLatin1String("WOW6432Node")) ? QStringLiteral("Run32") : QStringLiteral("Run");
    for (const QString &name : names) {
        const QString command = expand(s.value(name).toString().trimmed());
        if (command.isEmpty())
            continue;
        if (once) {
            // A RunOnce value is deleted before it runs, so a crash can't
            // make it run again. (A "!" prefix asks Explorer to delete it
            // after the program exits instead; launches here are detached,
            // so it is treated the same. "*" marks one for safe mode too.)
            s.remove(name);
        } else if (!approved(approvedKey, name)) {
            qInfo().noquote() << "startup: skipping" << name << "(turned off in Settings)";
            continue;
        }
        qInfo().noquote() << "startup:" << name << "->" << command;
        visor::run(command);
    }
}

void Startup::runFolder(const QString &folder)
{
    if (folder.isEmpty() || !QDir(folder).exists())
        return;
    QDirIterator it(folder, QDir::Files | QDir::NoDotAndDotDot);
    while (it.hasNext()) {
        const QFileInfo info = it.nextFileInfo();
        if (info.fileName().compare(QLatin1String("desktop.ini"), Qt::CaseInsensitive) == 0)
            continue;
        if (!approved(QStringLiteral("StartupFolder"), info.fileName())) {
            qInfo().noquote() << "startup: skipping" << info.fileName() << "(turned off in Settings)";
            continue;
        }
        qInfo().noquote() << "startup:" << QDir::toNativeSeparators(info.absoluteFilePath());
        visor::shellExecute(QDir::toNativeSeparators(info.absoluteFilePath()));
    }
}

} // namespace visor
