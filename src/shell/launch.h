#pragma once

#include <QString>

namespace visor {

// ShellExecuteEx with logging. Returns false (and logs why) on failure.
bool shellExecute(const QString &file, const QString &parameters = {});

// Opens a File Explorer window on This PC. Explorer is always given a target:
// a bare explorer.exe may try to become the shell.
void openFileExplorer();

// Windows Terminal if it launches, otherwise cmd.exe.
void openTerminal();

// Shell32's built-in Run dialog (undocumented, ordinal 61). Blocks in its own
// modal loop until closed.
void showRunDialog(void *ownerHwnd);

} // namespace visor
