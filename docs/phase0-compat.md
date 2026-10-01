# Phase 0: what works without Explorer (build 26300)

Tested in `visor-test` (Windows 11 Pro 26300.9457) with visor-shell in replace mode. Each **[I]** row in `design.md` §1 gets settled here.

Fill in each result as ✅ works, ❌ broken, or ⚠️ partly works, and add notes.

**Summary so far:** win32 apps and *full-trust* packaged apps (Store Notepad, Terminal) work. **UWP/CoreWindow apps (Settings, Calculator) do not.** Explorer's keyboard shortcuts, snapping and the Alt+Tab UI are gone, which our own window manager replaces anyway.

## Session

| Check | How | Result | Notes |
| --- | --- | --- | --- |
| Sign-in reaches our desktop without a ~30 s wait | Sign out, then sign in | ✅ | About 3 s from Winlogon's notifications to our ready signal |
| `ShellDesktopSwitchEvent` signalled | `shell.log` | ✅ | |
| Wallpaper drawn by `PaintDesktop` | Look at the desktop | ❌ | Black, even after re-applying the wallpaper with `SPI_SETDESKWALLPAPER` at runtime (the configured wallpaper is the default Spotlight JPEG, Fill mode). Fixed in phase 1: visor-shell decodes and scales the wallpaper itself with WIC, honouring the fit mode, with nothing cached (2.4 MB private) |
| Winlogon restarts the shell when it dies | Kill `visor-shell` and `visor-session` | ✅ | **AutoRestartShell applies to custom shells too.** Killing `visor-shell` (which owns the shell window) relaunches the HKCU shell almost immediately. Killing only `visor-session` does not. `visor-session` is single-instance so the two restarts don't race |
| Memory | `Get-Process` | ✅ | Private memory: `visor-session` 1.4 MB, `visor-shell` 2.3 MB, Visor 26 MB |
| Shift at sign-in falls back to Explorer | Hold Shift while signing in | | |
| Crash fallback (3 crashes, then Explorer) | Kill `visor-shell` 3× in Task Manager | | |
| Ctrl+Alt+Q hands over to Explorer (a full taskbar appears) | Ctrl+Alt+Q | ✅ | Explorer was the shell 0.3 s later. Winlogon relaunched `visor-session` 19 ms after `visor-shell` exited (even with exit code 2); the single-instance check made it exit |
| Sign out and shut down work | Ctrl+Alt+Del | | |
| Plain Safe Mode honours the HKCU shell | Advanced startup → Safe Mode | | |
| Enhanced Session (RDP) behaves like the console | Connect both ways | | |

## Apps and features

| Check | How | Result | Notes |
| --- | --- | --- | --- |
| File Explorer opens as a window, not as the shell | Ctrl+Alt+E | ✅ | Opens on This PC, with no taskbar or desktop takeover |
| Win32 app (Notepad) | Run → `notepad` | ✅ | This launched the **packaged** Notepad (WindowsApps, with tabs): full-trust packaged apps work |
| Run dialog | Ctrl+Alt+R | ✅ | It runs inside visor-shell, so a launch that hangs (see Settings) blocks the shell. Fixed in phase 1: launches and the Run dialog now run on worker threads. A hanging `ms-settings:` no longer blocks hotkeys |
| Settings app | Run → `ms-settings:` | ❌ | No window and no SystemSettings process. The launch blocked the shell's input for a while, then gave up |
| Store app (Calculator) | Run → `calc` | ❌ | `CalculatorApp.exe` starts but never gets a window: UWP/CoreWindow apps have no immersive shell to host them |
| Windows Terminal (packaged) | Ctrl+Alt+T | ✅ | Full-trust packaged app; `wt.exe` alias works |
| Edge | Run → `msedge` | ✅ | |
| Toast notification | `New-BurntToastNotification`, or any app's toast | | |
| Alt+Tab shows a switcher | Alt+Tab with 2+ windows open | ⚠️ | Switching works (focus moved from Explorer to Terminal), but nothing is drawn. Not checked while holding Alt |
| Win+R / Win+E / Win+D / Win+L | Press each | ❌ | Win+R and Win+E do nothing; the letter reaches the focused app. Win+D and Win+L not tested. Free for our own bindings |
| Win+Shift+S (snip) / Win+V (clipboard) / Win+. (emoji) | Press each | | |
| Snap: drag to an edge, and Win+arrows | | ❌ | Dragging to the left edge shows no snap preview; the window just moves off-screen. Win+arrows not tested |
| Volume / brightness keys show an OSD | | | |
| Visor runs, and its bar reserves space | `deploy.ps1 -Visor` | ⚠️ | Visor runs and tracks the active window correctly (26 MB). It reserves no space yet (needs the appbar server, phase 2). It keeps running after handover to Explorer |
| Tray icons appear anywhere | | | Expected ❌ until the tray host exists (phase 2) |
| Minimised windows | Minimise any window | ✅ | With no taskbar, Windows parks them as Win 3.1-style title-bar stubs at the bottom-left. Fixed in phase 1: while Visor is connected (and lists them), visor-shell sets `ARW_HIDE` for the session and restores the old value when Visor goes away. Windows minimised before that keep their stub until restored |
| Stopping Explorer starts our shell | Kill explorer.exe while it is the shell | ✅ | Winlogon relaunches the configured (HKCU) shell. Switches a session from Explorer back to visor-shell without signing out |
