#pragma once

#include <QString>

namespace visor {

// %LOCALAPPDATA%\visor-shell: logs and the safe-mode flag live here.
QString dataDir();

// Routes qDebug/qWarning/etc. to %LOCALAPPDATA%\visor-shell\logs\shell.log (and
// the debugger), since visor-shell has no console when it runs as the shell.
void installLogHandler();

} // namespace visor
