#pragma once

#include <QString>

#include <functional>

namespace visor {

// All launches run on a worker thread and return immediately; failures are
// logged. Some launches (e.g. UWP apps under replace mode) hang for a long
// time before failing, and must never block the shell's thread.

// Runs `work` on its own short-lived STA thread (COM set up), for launches
// of your own.
void runDetached(std::function<void()> work);

// ShellExecuteEx of a file, app alias, or URI. asAdmin: the "runas" verb.
void shellExecute(const QString &file, const QString &parameters = {}, bool asAdmin = false);

// Runs a command line: the first word (or "quoted path") is the program, the
// rest its arguments, e.g. `wt.exe -d C:\` or `"C:\Program Files\app.exe" -x`.
void run(const QString &commandLine);

// Opens a File Explorer window on This PC. Explorer is always given a target:
// a bare explorer.exe may try to become the shell.
void openFileExplorer();

// Windows Terminal if it launches, otherwise cmd.exe.
void openTerminal();

// Shell32's built-in Run dialog (undocumented, ordinal 61). Only one at a time.
void showRunDialog();

} // namespace visor
