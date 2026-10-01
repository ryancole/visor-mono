#pragma once

#include <QString>

namespace visor {

// All launches run on a worker thread and return immediately; failures are
// logged. Some launches (e.g. UWP apps under replace mode) hang for a long
// time before failing, and must never block the shell's thread.

// ShellExecuteEx of a file, app alias, or URI.
void shellExecute(const QString &file, const QString &parameters = {});

// Opens a File Explorer window on This PC. Explorer is always given a target:
// a bare explorer.exe may try to become the shell.
void openFileExplorer();

// Windows Terminal if it launches, otherwise cmd.exe.
void openTerminal();

// Shell32's built-in Run dialog (undocumented, ordinal 61). Only one at a time.
void showRunDialog();

} // namespace visor
