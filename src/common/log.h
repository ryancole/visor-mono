#pragma once

#include <QString>

namespace visor {

// %LOCALAPPDATA%\visor-shell: logs and the safe-mode flag live here.
QString dataDir();

// Routes qDebug/qWarning/etc. to %LOCALAPPDATA%\visor-shell\logs\<name>.log
// (and the debugger), since visor-shell and visor-wm have no console when
// they run under the shell.
void installLogHandler(const QString &name);

} // namespace visor
