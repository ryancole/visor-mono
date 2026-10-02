#include "common/log.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QStandardPaths>

#include <windows.h>

#include <cstdio>

namespace visor {

namespace {

constexpr qint64 kMaxLogBytes = 1 << 20;

// Opened for appending only (FILE_APPEND_DATA), so each write lands at the
// end of the file even when another process appends to it too: an exiting
// visor-wm and its successor both write wm.log for a moment.
HANDLE g_log = INVALID_HANDLE_VALUE;

QMutex &logMutex()
{
    static QMutex mutex;
    return mutex;
}

void handler(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    static const char *const levels[] = {"debug", "warning", "critical", "fatal", "info"};
    const QString line = QStringLiteral("%1 [%2] %3\n")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                  QLatin1StringView(levels[qBound(0, int(type), 4)]), message);

    OutputDebugStringW(reinterpret_cast<const wchar_t *>(line.utf16()));
    if (GetStdHandle(STD_ERROR_HANDLE))
        std::fputs(line.toLocal8Bit().constData(), stderr);

    QMutexLocker lock(&logMutex());
    if (g_log != INVALID_HANDLE_VALUE) {
        const QByteArray utf8 = line.toUtf8();
        DWORD written = 0;
        WriteFile(g_log, utf8.constData(), DWORD(utf8.size()), &written, nullptr);
    }
}

} // namespace

QString dataDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("visor-shell"));
}

void installLogHandler(const QString &name)
{
    const QDir dir(dataDir() + QStringLiteral("/logs"));
    dir.mkpath(QStringLiteral("."));

    const QString path = QDir::toNativeSeparators(dir.filePath(name + QStringLiteral(".log")));
    if (QFile(path).size() > kMaxLogBytes)
        QFile::remove(path);
    g_log = CreateFileW(reinterpret_cast<const wchar_t *>(path.utf16()), FILE_APPEND_DATA,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
    const DWORD error = GetLastError();

    qInstallMessageHandler(handler);
    if (g_log == INVALID_HANDLE_VALUE)
        qWarning() << "cannot open log file" << path << "error" << error;
}

} // namespace visor
