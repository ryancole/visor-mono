# Phase 0: what works without Explorer (build 26300)

Tested in `visor-test` (Windows 11 Pro 26300.9457) with visor-shell in replace mode. Each **[I]** row in `design.md` §1 gets settled here.

Fill in each result as ✅ works, ❌ broken, or ⚠️ partly works, and add notes.

## Session

| Check | How | Result | Notes |
| --- | --- | --- | --- |
| Sign-in reaches our desktop without a ~30 s wait | Sign out, then sign in | ✅ | About 3 s from Winlogon's notifications to our ready signal |
| `ShellDesktopSwitchEvent` signalled | `shell.log` | ✅ | |
| Wallpaper drawn by `PaintDesktop` | Look at the desktop | ❌ | Black, even after re-applying the wallpaper with `SPI_SETDESKWALLPAPER` at runtime (the configured wallpaper is the default Spotlight JPEG, Fill mode). We render it ourselves (phase 1) |
| Winlogon restarts the shell when it dies | Kill `visor-shell` and `visor-session` | ✅ | **AutoRestartShell applies to custom shells too.** Killing `visor-shell` (which owns the shell window) relaunches the HKCU shell almost immediately. Killing only `visor-session` does not. `visor-session` is single-instance so the two restarts don't race |
| Memory | `Get-Process` | ✅ | Private memory: `visor-session` 1.4 MB, `visor-shell` 2.3 MB, Visor 26 MB |
| Shift at sign-in falls back to Explorer | Hold Shift while signing in | | |
| Crash fallback (3 crashes, then Explorer) | Kill `visor-shell` 3× in Task Manager | | |
| Ctrl+Alt+Q hands over to Explorer (a full taskbar appears) | Ctrl+Alt+Q | | |
| Sign out and shut down work | Ctrl+Alt+Del | | |
| Plain Safe Mode honours the HKCU shell | Advanced startup → Safe Mode | | |
| Enhanced Session (RDP) behaves like the console | Connect both ways | | |

## Apps and features

| Check | How | Result | Notes |
| --- | --- | --- | --- |
| File Explorer opens as a window, not as the shell | Ctrl+Alt+E | | |
| Win32 app (Notepad) | Run → `notepad` | | |
| Run dialog | Ctrl+Alt+R | | |
| Settings app | Run → `ms-settings:` | | |
| Store app (Calculator) | Run → `calc` | | |
| Windows Terminal (packaged) | Ctrl+Alt+T | | |
| Edge | Run → `msedge` | | |
| Toast notification | `New-BurntToastNotification`, or any app's toast | | |
| Alt+Tab shows a switcher | Alt+Tab with 2+ windows open | | |
| Win+R / Win+E / Win+D / Win+L | Press each | | |
| Win+Shift+S (snip) / Win+V (clipboard) / Win+. (emoji) | Press each | | |
| Snap: drag to an edge, and Win+arrows | | | |
| Volume / brightness keys show an OSD | | | |
| Visor runs, and its bar reserves space | `deploy.ps1 -Visor` | | Expected ❌ until the appbar server exists (phase 2) |
| Tray icons appear anywhere | | | Expected ❌ until the tray host exists (phase 2) |
