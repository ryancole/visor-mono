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
| Crash fallback (3 crashes, then Explorer) | Kill `visor-shell` 3× in Task Manager | ✅ | Winlogon relaunched `visor-shell` after each kill (via `visor-session`, which kept its crash count); after the third within a minute it started Explorer, the shell 313 ms later. Visor stayed running as a plain app. Killing Explorer brought our shell back |
| Ctrl+Alt+Q hands over to Explorer (a full taskbar appears) | Ctrl+Alt+Q | ✅ | Explorer was the shell 0.3 s later. Winlogon relaunched `visor-session` 19 ms after `visor-shell` exited (even with exit code 2); the single-instance check made it exit |
| Sign out and shut down work | Ctrl+Alt+Del | | |
| Plain Safe Mode honours the HKCU shell | Advanced startup → Safe Mode | | |
| Enhanced Session (RDP) behaves like the console | Connect both ways | ✅ | Needs the VMBus transport (`new-vm.ps1` had HvSocket, which is for Linux guests) and Remote Desktop allowed in the guest. The reconnect goes through a placeholder display (`WinDisc`): visor-wm moves windows across it and back, Visor's bar re-registers, the work area ends up right. While connected, the console shows the lock screen (`screenshot.ps1 -Inside` captures the session instead) |

## Apps and features

| Check | How | Result | Notes |
| --- | --- | --- | --- |
| File Explorer opens as a window, not as the shell | Ctrl+Alt+E | ✅ | Opens on This PC, with no taskbar or desktop takeover |
| Win32 app (Notepad) | Run → `notepad` | ✅ | This launched the **packaged** Notepad (WindowsApps, with tabs): full-trust packaged apps work |
| Run dialog | Ctrl+Alt+R | ✅ | It runs inside visor-shell, so a launch that hangs (see Settings) blocks the shell. Fixed in phase 1: launches and the Run dialog now run on worker threads. A hanging `ms-settings:` no longer blocks hotkeys |
| Settings app | Run → `ms-settings:` | ❌ | No window and no SystemSettings process. The launch blocked the shell's input for a while, then gave up |
| Store app (Calculator) | Run → `calc` | ❌ | `CalculatorApp.exe` starts but never gets a window: UWP/CoreWindow apps have no immersive shell to host them |
| Windows Terminal (packaged) | Ctrl+Alt+T | ✅ | Full-trust packaged app; `wt.exe` alias works |
| Packaged apps through the shell (`shell:AppsFolder\<id>`, or the item's PIDL) | Phase 4 launcher | ❌ | `REGDB_E_CLASSNOTREG` for every packaged app, full-trust ones included; shortcuts launch fine. The launcher runs full-trust packaged apps by their execution alias or executable instead (see design.md §4) |
| Edge | Run → `msedge` | ✅ | |
| Toast notification | `New-BurntToastNotification`, or any app's toast | ❌ | Nothing is shown, but the notification platform (WpnUserService) keeps running: every toast lands in `%LOCALAPPDATA%\Microsoft\Windows\Notifications\wpndatabase.db`, and the `UserNotificationListener` API reads them from a plain exe (access was already allowed; the machine-wide consent is Allow). Fixed in phase 6: Visor shows toasts and keeps a history |
| Alt+Tab shows a switcher | Alt+Tab with 2+ windows open | ⚠️ | Switching works (focus moved from Explorer to Terminal), but nothing is drawn. Not checked while holding Alt. Fixed in phase 7: visor-wm's keyboard hook keeps the key (a low-level hook sees it before Windows acts) and Visor draws a switcher with DWM thumbnails |
| Win+R / Win+E / Win+D / Win+L | Press each | ❌ | Win+R and Win+E do nothing; the letter reaches the focused app. Win+D and Win+L not tested. Free for our own bindings |
| Win+Shift+S (snip) / Win+V (clipboard) / Win+. (emoji) | Press each | ❌ | All three are Explorer's (ShellExperienceHost): the letter reaches the focused app instead. Win+V is `visor-wm`'s toggle-floating binding (Omarchy's Super+V), so it is taken anyway |
| Snap: drag to an edge, and Win+arrows | | ❌ | Dragging to the left edge shows no snap preview; the window just moves off-screen. Win+arrows not tested |
| Volume / brightness keys show an OSD | Press them (needs an audio device: Enhanced Session's Remote Audio) | ❌ | Nothing: Explorer handled the volume keys too, so without it they change nothing (VK_VOLUME_DOWN ×5, still 100%). Fixed in phase 6: visor-wm binds them and Visor changes the volume and shows an OSD. Brightness keys have no virtual key; the OS handles them itself |
| Visor runs, and its bar reserves space | `deploy.ps1` | ✅ | Phase 2: visor-shell serves `SHAppBarMessage`. Visor's bar registers, the work area becomes 0,32–1024,768, and maximised windows stop below the bar |
| Tray icons appear anywhere | | ✅ | Phase 2: visor-shell is `Shell_TrayWnd` and Visor draws the icons. Seen: OneDrive, Windows Security, the classic volume icon (from the SysTray shell service object, which costs about 5 MB and some threads in visor-shell; no longer loaded since phase 7, as Visor shows the volume itself), and Visor's own |
| Tray clicks reach apps | Right-click Visor's and OneDrive's icons | ✅ | Menus open at the click point and close on Escape. OneDrive's Activity Center docks under the bar. Visor has to take the foreground first (attach to the foreground thread's input) and pass it on: clicks on its `WS_EX_NOACTIVATE` bar give it no foreground rights. Volume icon click: no visible flyout (the VM has no audio device; Win11's flyout lived in Explorer) |
| Fullscreen apps hide the bar | F11 in Terminal | ✅ | `ABN_FULLSCREENAPP` on foreground changes and `HSHELL_FULLSCREENENTER/EXIT`; the bar comes back on exit |
| Minimised windows | Minimise any window | ✅ | With no taskbar, Windows parks them as Win 3.1-style title-bar stubs at the bottom-left. Fixed in phase 1: while Visor is connected (and lists them), visor-shell sets `ARW_HIDE` for the session and restores the old value when Visor goes away. Windows minimised before that keep their stub until restored |
| Stopping Explorer starts our shell | Kill explorer.exe while it is the shell | ✅ | Winlogon relaunches the configured (HKCU) shell. Switches a session from Explorer back to visor-shell without signing out |

## Hosted mode (Explorer stays the shell)

Phase 8, same build. Tested with `pwsh etc/vm/deploy.ps1 -Hosted` after Ctrl+Alt+Q had handed the session to Explorer, with Ryan connected by Enhanced Session (so the volume key had Remote Audio to act on).

| Check | How | Result | Notes |
| --- | --- | --- | --- |
| Bar docks, taskbar auto-hides | Start visor-shell hosted | ✅ | `ABM_SETSTATE` with `ABS_AUTOHIDE`. Work area 0,32–1920,1080: Visor's bar reserves 32 px, the auto-hidden taskbar none; maximised windows fill it; desktop icons move below the bar. The bar follows Explorer's light mode. The taskbar peeks on hover and for a moment when a toast arrives, as it does normally |
| Task list | Open Notepad | ✅ | Shell hook messages reach a non-shell window, as Phase 2 saw |
| Settings from the launcher | Type "settings", Enter | ✅ | Listed as launchable (no "Needs Explorer"); SystemSettings runs and gets a window |
| Settings > Apps > Startup lists the Run entry | | ✅ | "visor-shell", On, "Not measured" |
| Theme picker's Settings row | Right-click the button > Themes > Personalization settings... | ✅ | Opens Settings at Personalization > Themes |
| Toast is Windows' | WinRT `ToastNotificationManager` from PowerShell | ✅ | Windows' toast (app header, close button) at the bottom right; the bar has no bell |
| Volume key shows Windows' flyout | VK_VOLUME_UP | ✅ | Windows 11's flyout, 84 → 86 |
| Alt+Tab is Windows' | Hold Alt, press Tab | ✅ | Windows' switcher with its large previews |
| Menu's quit row | Right-click the button | ✅ | Reads "Quit Visor" instead of "Quit to Explorer" |
| Ctrl+Alt+Q restores the taskbar | | ✅ | Shell and Visor exit, auto-hide off again, the recorded state removed; the work area returns to 0,0–1920,1032 |
| Clean restart from the host | `deploy.ps1` with a hosted shell running | ✅ | `visor-shell --quit` (a `WM_CLOSE` to the link window), then a fresh start |
| Crash | `Stop-Process visor-shell` | ⚠️ | Nothing restarts it; Visor stays up as a plain bar and reconnects when the shell is started again (no second Visor). The taskbar stays auto-hidden, the record survives, the next run keeps it and its clean exit restores the original |
| Back to replace mode | `deploy.ps1 -Install`, then stop explorer.exe | ✅ | The Run entry goes, the Shell override returns, Winlogon starts the HKCU shell |
| visor-wm under Explorer | | | Not tested |
