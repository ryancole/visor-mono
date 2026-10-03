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
| Crash | `Stop-Process visor-shell` | ✅ | Phase 8: nothing restarted it; Visor stayed up as a plain bar and reconnected when the shell was started again. Phase 9: the Run entry runs `visor-session --mode hosted`, which restarted it a second later; Visor reconnected (no second Visor), visor-wm handed over to the one the new shell started, the taskbar stayed auto-hidden, the record survived and the next clean exit restored the original |
| Back to replace mode | `deploy.ps1 -Install`, then stop explorer.exe | ✅ | The Run entry goes, the Shell override returns, Winlogon starts the HKCU shell |

### Tiling under Explorer (Phase 9)

`deploy.ps1 -Hosted -Tiling`, same build, console session (no audio device).

| Check | How | Result | Notes |
| --- | --- | --- | --- |
| visor-wm starts and tiles | Open Notepad, Super+Return | ✅ | `wm.log`: "mode: hosted", "21 key bindings, 17 of them through the keyboard hook", 16 "left to Windows". Both windows tile under the bar with the gaps; frames end at y=1070 (work area bottom 1080, gaps_out 10). A third window splits the left column; Super+W closes it; Super+V floats the terminal centred and tiles it back |
| Volume key shows Windows' flyout | VK_VOLUME_UP (keybd_event, captured in the same script) | ✅ | Windows 11's flyout (at 0, no audio device), not Visor's OSD |
| Alt+Tab is Windows' | Alt held, Tab | ✅ | Windows' switcher with its previews |
| Win+N is Windows' | Win+N | ✅ | Windows' Notification Center, and it isn't tiled |
| Windows desktops | Win+Ctrl+D, open Notepad, Win+Ctrl+Left | ✅ | The new desktop is empty and the bar shows no pills; Notepad tiles alone there ("on Windows desktop 957c…", "desktop 2"); back on the first, the Notepad + Terminal layout is as it was |
| Win+arrows move focus | Win+Right from Notepad | ✅ | "SUPER+right -> movefocus r"; the terminal gets focus and the caret |
| Win+arrows move the window | Win+arrows on PowerShell among Edge, Claude and Battle.net | ✅ | "SUPER+left -> movewindow l" and the rest in `wm.log`; Ryan moved it around all four ways and it did what he expected |
| Win+Up on a full-height column | PowerShell as the right column beside Claude over Battle.net | ✅ | PowerShell beside Claude in the top row, Battle.net across the bottom row |
| Win+Down from the top row | PowerShell beside Claude, Battle.net across the bottom | ✅ | The first press makes PowerShell the full-height right column again, the second puts it beside Battle.net in the bottom row; Win+Up goes back the same way |
| visor-wm crash | `Stop-Process visor-wm` | ✅ | visor-shell restarted it a second later; it adopted the two windows into the same tiles |
| visor-shell crash | `Stop-Process visor-shell` | ✅ | See "Crash" above |
| Ctrl+Alt+Q | | ✅ | visor-shell, Visor, visor-wm and visor-session all exit; auto-hide off again and the record removed |
| Launched-from-background windows | `Start-Process notepad` over PowerShell Direct | ⚠️ | Not ours: a window started by a background process can't take the foreground, its taskbar button flashes, and Explorer keeps an auto-hidden taskbar up while one flashes. It hides once the window is clicked. Apps started from the launcher or a key don't do this |

## The rest of the bar (Phase 10, on the host)

Checked live on Ryan's desktop (replace mode, build 26300, 3440x1440, no battery, one keyboard layout), by screen capture.

| Check | How | Result | Notes |
| --- | --- | --- | --- |
| The cluster | Look at the bar | ✅ | Ethernet icon, volume icon and level, then the bell; no battery, as there is none. `visor.log`: "network: ethernet "Ethernet" internet 5 bars", "radios: wifi off bluetooth off" |
| Centre indicators | Look at the bar | ✅ | Only the clock: one layout, no restart pending, nothing using the mic, so nothing shows, as intended |
| Quick Settings | Win+A, or click the cluster | ✅ | `wm.log` "SUPER+A -> visor quicksettings"; the popup under the cluster: Wi-Fi and Bluetooth buttons (both radios present, off), the volume slider at 50 with "Speakers (HyperX Cloud MIX 2)", and the "Ethernet" line. No brightness slider (desktop), no battery tile |
| Do not disturb | The moon in the Notification Center; the setting flipped from a script in the session | ✅ | `visor.log`: "RegistryWatch: change under ...Notifications\\Settings", "do not disturb on", the bell becomes a moon; off again three seconds later. (A write from Claude's own shell never reached the real registry: see CLAUDE.md) |
| Minimum sizes in the layout | Five windows on one monitor, Discord and Claude stacked in a quarter | | Expect the stack to take ~910 px of height and Battle.net below to shrink, no overlap, within a moment of Discord opening |
| Clock format and click | Look at the bar; click the clock | ✅ | "Fri 2 Oct   10:52 PM" (en-US, `h:mm tt`); a click opened the Date and Time dialog, floating, without Explorer |
| Keyboard layout | Needs a second layout | | Not tested: one layout installed |
| Restart required | Needs a pending Windows Update | | Not tested |
| Mic / camera in use | Start a call or recording | | Not tested |
| Desktop in the launcher | Win; type a few letters | ✅ | `visor.log`: "indexed 169 apps and 11 desktop items", the 11 Explorer shows (Recycle Bin, 4 from the user's Desktop, 6 from the Public one); a first cut opened `FOLDERID_Desktop` as a shell item, which is the namespace root, and listed 32. The grid stays while All apps filters; checked by Ryan |
| Desktop folder, This PC, Recycle Bin from the launcher | Show them in Desktop icon settings, open one | | Not tested: expected to open Explorer as a file-manager window, as Super+E does |
