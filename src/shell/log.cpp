#include "shell/log.h"

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

QFile &logFile()
{
    static QFile file;
    return file;
}

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
    QFile &file = logFile();
    if (file.isOpen()) {
        file.write(line.toUtf8());
        file.flush();
    }
}

} // namespace

QString dataDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("visor-shell"));
}

void installLogHandler()
{
    const QDir dir(dataDir() + QStringLiteral("/logs"));
    dir.mkpath(QStringLiteral("."));

    QFile &file = logFile();
    file.setFileName(dir.filePath(QStringLiteral("shell.log")));
    if (file.size() > kMaxLogBytes)
        file.remove();
    const bool opened = file.open(QIODevice::Append | QIODevice::Text);

    qInstallMessageHandler(handler);
    if (!opened)
        qWarning() << "cannot open log file" << file.fileName() << file.errorString();
}

} // namespace visor
