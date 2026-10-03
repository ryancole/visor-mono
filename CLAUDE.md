# Working on visor

visor is a Windows desktop meant to feel like Omarchy (Hyprland), built the
Windows way. Read `README.md` and `docs/design.md` first.

## The rule: Windows' way wherever Windows has one

Follow Windows 11's conventions wherever Windows has one; borrow from
Omarchy/Hyprland only where Windows has nothing. Whenever you propose
options, a plan or an order of work, **say which option is the most
Windows-conventional** (how Windows 11 does it, or Microsoft's supported
mechanism, as opposed to undocumented behaviour) and lead with it, or say
plainly why you recommend something else.

## Testing

- **visor is Ryan's daily shell on the host** (since 2026-10-02):
  replace mode, installed from `%LOCALAPPDATA%\Programs\visor` with
  `install.ps1 -AllowPhysicalMachine`, so visor-session is the Winlogon
  shell for his user and Explorer is the fallback. This is a live
  session, not a test box: a crash is Ryan's desktop breaking. Prefer
  small, reversible changes, and never break Ctrl+Alt+Q, the watchdog's
  Explorer fallback, Shift-at-sign-in or the safe-mode file: they are the
  way back.
- **Ryan does the session-level steps himself**: running the install or
  uninstall scripts, changing the Winlogon `Shell` value, signing out,
  killing Explorer or the shell, and switching modes. Give him the
  commands; don't run them unasked.
- Host loop: build with `pwsh etc/build.ps1 release`, then Ryan quits to
  Explorer (Ctrl+Alt+Q), copies `build/release` over the install folder
  and ends `explorer.exe` from Task Manager, which makes Winlogon start
  the shell again (or signs out and in). Logs are in
  `%LOCALAPPDATA%\visor-shell\logs\`. Check the result with a screenshot
  before calling it done. Hosted mode still works on the host for a
  quick look (`visor-shell --mode hosted` from a build dir, Ctrl+Alt+Q
  to stop).
- The Hyper-V VM `visor-test` (`etc/vm/*.ps1`: `deploy.ps1`,
  `screenshot.ps1`, `input.ps1`, `run.ps1`) is for anything too risky to
  try live first: sign-in, the startup runner, the watchdog, Winlogon.
  It is usually shut down; ask before starting it, and don't sign it out
  or restart it unasked (no auto sign-in).
- Claude's own shells on the host run under the Claude desktop app and
  their registry writes to HKCU land in a virtualised view that Visor and
  Windows never see (reads are suspect too). A registry change for a test
  goes through a scheduled task in Ryan's session, or Ryan runs it. File
  writes are unaffected.
- Update `README.md`, `docs/design.md`, `docs/phase0-compat.md` and
  `src/bar/README.md` with every feature.

## Code

- Scripts go in `etc/`, code in `src/`. Match the surrounding style and
  comment density.
- Visor -> shell messages go over the WM_COPYDATA link
  (`src/common/linkprotocol.h`); new message types are documented there.
- New UI uses `Theme.qml` (colours follow Windows' mode and accent), never
  literals. Write glyphs as `\uXXXX` escapes, not literal private-use
  characters.
- Don't add a file to `src/bar/services` whose name clashes with a Windows
  SDK header: that folder is on the include path.
- Commit only when asked ("git commit"). End commit messages with the
  Co-Authored-By line.
