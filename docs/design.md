# visor-shell: design and plan

Status: **proposal**. No code is written yet. Everything here is meant to be checked in a Hyper-V VM before it is relied on.

Tags used below: **[V]** = confirmed by a source (Microsoft docs, or ManagedShell/Cairo source we read). **[I]** = inferred, still to be tested on build 26200.

## 0. Goal: an Omarchy-style desktop on Windows

[Omarchy](https://omarchy.org) is an opinionated, keyboard-driven desktop: Hyprland tiling, Waybar, a launcher, one-switch theming, and notifications and on-screen displays. Visor (`src/bar`) plays the role of Waybar. The rest of this repo is the rest of the desktop.

| Omarchy piece | Ours | Where it is covered |
| --- | --- | --- |
| Hyprland: tiling, workspaces, key bindings | `visor-wm` | §4, Phase 3 |
| Waybar | Visor | Exists |
| Walker launcher, Omarchy menu, keybinding cheat sheet | Visor QML launcher and menu | Phase 4 |
| One-switch themes (bar, terminal, wallpaper…) | Windows themes, followed and switched by Visor | §4 "Theming", Phase 5 |
| mako notifications, SwayOSD | Visor overlays | Phase 6 |
| The session itself (what Explorer quietly does on Windows) | `visor-shell` | §1, Phases 1–2 |

Our own tiling, workspaces and switcher stand in for most of what replace mode loses (Snap, Task View, virtual desktops, the Alt+Tab UI). What replace mode really costs is mainly the Settings app and packaged apps (§2).

## 1. What Explorer does as the shell, and what we lose without it

When `HKCU\...\Winlogon\Shell` names another program, userinit starts that program instead of Explorer. The HKCU value takes precedence over HKLM, needs no policy, and works on Pro [V]. Explorer is then never started as the shell. Each feature below lands in one of three groups: we must rebuild it, we lose it, or it still works.

| Feature | Without Explorer | Our answer |
| --- | --- | --- |
| Shell-ready signal | Logon screen stays up for about 30 s, then times out [V] | `SetEvent("ShellDesktopSwitchEvent")` once the desktop is up [V, Cairo] |
| Desktop and wallpaper | No Progman, only the bare win32k desktop [I] | Our own desktop window plus `SetShellWindow`; we paint the wallpaper ourselves |
| Notification area (`Shell_NotifyIcon`) | Gone: no `Shell_TrayWnd` [V] | We are `Shell_TrayWnd`. We decode `WM_COPYDATA` and send icons to Visor |
| AppBars (`SHAppBarMessage`) | Gone. **Visor's own `PanelWindow` depends on this** for exclusive zones | We are the appbar server and own the work area. Cairo never built this part [V] |
| Shell hook events | Still work: `RegisterShellHookWindow` is a user32 feature [V] | Shell hook window, plus `SetTaskmanWindow` |
| ITaskbarList (progress, overlay) | Gone | `TaskbandHWND` property on our tray [V, ManagedShell] |
| Run, RunOnce, Startup folders | Never run [V] | Our own startup runner (`shell/startup.cpp`, done) |
| Start menu, search, Widgets | Gone [V] | Launcher in Visor (Phase 4, done) |
| Win-key hotkeys | Explorer registered most of them, so they go dead. Win+L stays with the system [V] | `RegisterHotKey(MOD_WIN, …)` once Explorer no longer holds them, plus a low-level hook for a bare Win press [I] |
| Alt+Tab | Switching still works, but **no UI is drawn on 24H2 and later** [V] | Our own switcher (Phase 7, done): visor-wm's hook keeps the key, Visor draws it with DWM thumbnails |
| Snap and Snap Layouts, Task View, virtual desktops | Live in twinui inside Explorer, so lost [I] | Defer. Rebuild only if needed |
| Toasts, Notification Center, Quick Settings, volume/brightness OSD | Lost [V]. The volume keys go dead too: Explorer acted on them | Phase 6: toasts and a history read from the notification platform, which keeps running; visor-wm binds the volume keys and Visor shows an OSD. Quick Settings: not rebuilt |
| **UWP/packaged apps, including the Settings app** | **"Class not registered"**: only Explorer, or Shell Launcher (Enterprise-only), can start them [V, Cairo #365/#936] | **Biggest open risk.** See §2 |
| Explorer as a file manager | Works when given a path. A bare `explorer.exe` may try to become the shell [V] | Always launch it with a target. `SetShellWindow` makes our shell visible to it |
| Ctrl+Shift+Esc, Ctrl+Alt+Del | Still work [V] | Our recovery path |
| Shell crash or exit | `AutoRestartShell` restarts only Explorer. Exit code 0 can make winlogon start the HKLM shell [V] | Our own watchdog (§4) |

## 2. The key decision: replace Explorer, or host alongside it

On Windows 11 a full replacement loses the **Settings app**, the **Store**, every packaged app (Copilot, Terminal-as-default-handler flows, Photos, …), and toasts. The only known workaround patches twinui's private ABI and ties us to specific builds [V]. Cairo's maintainers recommend keeping Explorer running for that reason.

The good news is that we don't have to pick now. The "run as a normal app while Explorer is the shell" mode you asked for is a mode Cairo and RetroBar already ship. So **we build both modes from the same code, from day one**:

- **Hosted mode.** Explorer stays the Winlogon shell. We start from a Run entry and ask Explorer's taskbar to auto-hide through the documented app bar API (`ABM_SETSTATE`), restoring it on exit; `Shell_TrayWnd` is left alone and the tray stays on the auto-hidden taskbar (taking it over by z-order, as ManagedShell does, is a later option). Settings, UWP, toasts, Alt+Tab and snapping keep working. We pay for Explorer's RAM, roughly 100 MB [I]. Built and verified in Phase 8; see §4 "Hosted mode".
- **Replace mode.** Our shell is the Winlogon shell and Explorer never starts as the shell. This is the lowest-RAM and fastest-logon option, and it has the gaps listed in §1.

Phase 0 tested §1 in the VM: win32 and full-trust packaged apps work without Explorer, UWP apps (Settings, Calculator) don't, and Phases 1–7 rebuilt the rest. **The decision (Phase 8): hosted mode is the Windows-conventional configuration and the one for a real machine, since Windows has no supported way to replace Explorer on desktop editions; replace mode stays the lighter enthusiast path.** Both are built from the same code.

## 3. Recommendation: the shell runs as its own process, not inside Visor

| | Inside Visor | Separate process (recommended) |
| --- | --- | --- |
| Restart behaviour | Visor restarts itself when you change the renderer and rebuilds windows on reload. If it held `Shell_TrayWnd`, every restart would drop all tray icons and appbar registrations | The shell stays up and Visor comes and goes |
| Crash surface | A QML error, a D3D driver fault or a Qt Quick bug would take the session shell down | The shell has a tiny surface: no QML, no GPU |
| Memory | Qt Quick is already loaded | QtCore plus raw Win32. Target under 8 MB private [I] |
| Who owns the UI | Not applicable | The shell has **no UI**. Visor draws the tray, task list and launcher |

Visor already re-registers its tray icon on `TaskbarCreated` (`visor/src/tray.cpp:137`) and already uses `SHAppBarMessage` (`visor/src/panelwindow.cpp:206`). The work splits along those lines: the shell is the server side of protocols Visor already speaks as a client.

## 4. Architecture

```
winlogon → userinit → HKCU Shell =
  visor-session.exe   pure Win32, static CRT, no Qt: watchdog and failsafe
    └─ visor-shell.exe   QtCore + Win32: shell services, no UI
         ├─ visor-wm.exe    window manager: tiling, workspaces, key bindings
         └─ visor.exe       existing status bar (Qt Quick): all UI
```

`visor-wm` runs as its own process because tiling is the most complex and crash-prone part. If it crashes, windows just stay where they are, which is harmless. If the tray process crashed, every tray icon would be lost. It uses the same approach as komorebi and GlazeWM, the prior art to study:

- Tracks windows through WinEvent and shell hooks.
- Arranges them with `SetWindowPos` inside each monitor's work area, the area left after appbars such as Visor.
- Fakes workspaces by hiding windows. The hiding method has trade-offs that Phase 0 needs to test.
- Owns all Super-key bindings, read from config.

It reports workspaces and the current layout to Visor through the same `VisorLink` channel.

### visor-session.exe (watchdog), about 200 lines

- No DLL dependencies, so a broken Qt deployment can never cause a black screen.
- Starts `visor-shell.exe` and restarts it if it crashes.
- `--mode hosted` (what the hosted-mode Run entry runs, Phase 9): the same, with `visor-shell --mode hosted`, except that where it would start Explorer it just stops (Explorer is there), and a clean exit of the shell (Ctrl+Alt+Q) ends it.
- **Falls back to Explorer** after 3 crashes in 60 s, or if the shell executable is missing. With no shell window present, a bare `explorer.exe` becomes the full shell, which gives a normal desktop.
- **Escape hatch:** if Shift is held at logon, or `%LOCALAPPDATA%\visor\safe-mode` exists, it starts Explorer immediately.
- Exits with a non-zero code so winlogon never starts the HKLM shell behind our back [V].

### visor-shell.exe, one module per service, all event-driven

| Module | Responsibility |
| --- | --- |
| `ShellWindow` | Desktop window (bottom of z-order, covers the virtual screen), `SetShellWindow`, wallpaper painted through WIC/GDI, reacts to `WM_SETTINGCHANGE` and display changes. Replace mode only. |
| `ShellReady` | Signals `ShellDesktopSwitchEvent` after the desktop and tray exist. |
| `TrayHost` | `Shell_TrayWnd` and `TrayNotifyWnd`. Decodes `WM_COPYDATA` (dwData 0 = appbar, 1 = `NIM_*`, 3 = GetRect) using the **32-bit wire layouts** [V]. Broadcasts `TaskbarCreated`. Forwards clicks back using v3/v4 semantics. The SysTray shell service object (the classic volume, network and power icons) is not loaded: Windows 11's taskbar draws its own, Visor has a volume widget, and the classic icons are white for a dark taskbar with flyouts that live in Explorer. |
| `AppBarServer` | A real registry for `ABM_NEW/QUERYPOS/SETPOS/REMOVE/GETTASKBARPOS/GETSTATE…`. Stacks bars per monitor per edge, sets `SPI_SETWORKAREA` per monitor, sends `ABN_POSCHANGED` and `ABN_FULLSCREENAPP`. Replace mode only: in hosted mode Explorer is the server and Visor's bars register with it. |
| `Taskbar` | Hosted mode only: asks Explorer's taskbar to auto-hide (`ABM_SETSTATE`) while the shell runs, records the original state under `HKCU\Software\visor-shell` the first time (auto-hide is Explorer's persistent setting, so a crash would leave it on), restores it on a clean exit and re-applies it when Explorer restarts (`TaskbarCreated`). Leaves it alone at session end. |
| `Tasks` | Shell hook window, `SetTaskmanWindow`, HSHELL_* events (created, destroyed, activated, flash, fullscreen enter/exit, getminrect), cloak tracking, `TaskbandHWND` progress and overlay. |
| `Startup` | HKCU RunOnce (each value deleted before it runs), then Run (HKLM, Wow6432Node, HKCU, the Policies keys), then the Startup folders (machine, then user), all subject to Settings' `StartupApproved` switches and launched detached three seconds after the desktop is up. Replace mode only, once per sign-in: a volatile registry key marks it, which vanishes at sign-out. Verified in the VM: OneDrive, Edge's background launch and the security-health tray icon start. |
| `Hotkeys` | Ctrl+Alt+E/T/R/Q (nothing in Windows reserves them), in both modes; the Win-key bindings are visor-wm's. |
| `VisorLink` | IPC with Visor, and starts Visor and keeps it running, in both modes. A `WM_CLOSE` to its window (`visor-shell --quit`) quits the shell as Ctrl+Alt+Q does. |

### IPC between visor-shell and Visor

- **Transport:** `WM_COPYDATA` between message-only windows. No extra Qt module, delivery is ordered and synchronous, and it matches the pattern both sides already use.
- **Discovery:** the shell broadcasts a registered `VisorShellCreated` message, the same way `TaskbarCreated` works, so either side can restart and the two sides reconnect.
- **Icons:** sent as HICON handles. USER objects are valid across processes in the same session, so there's no bitmap serialisation. The shell keeps its own `CopyIcon` of each one.
- **Clicks:** Visor calls `AllowSetForegroundWindow(iconPid)` itself, because it is the foreground process at click time. It then asks the shell to send the click to the app.
- **New QML types in Visor:** `SystemTray` (model of icons), `Tasks` (window list with flashing, progress and overlay), `Apps` (launcher index from `FOLDERID_AppsFolder`), and `Shell` (mode, `showDesktop()`, `run()`).
- **Fallback:** when Visor runs without visor-shell, for example on a normal Explorer session, these types show empty models and nothing else changes.

### Launcher and menus (Phase 4, done)

The launcher, the power menu and the key-binding cheat sheet are QML in Visor (`Launcher.qml`, `SystemMenu.qml`, `CheatSheet.qml`), built on three things:

- **Keys.** Windows' own where it has one: a bare Win press or Win+S for the launcher, Win+X for the power menu, Win+R for Run; Omarchy's Super+K for the cheat sheet. All are `bind` lines in `wm.conf` with the `visor` dispatcher (`bindd = SUPER, X, System menu, visor, menu`), so visor-wm sends `{"type":"visor.command","name":"menu"}` to the shell, which forwards it to Visor, where `shell.qml` decides what each name opens. The bare Win press is `bindr = SUPER, SUPER_L, ...` (Hyprland's syntax): a release binding in the keyboard hook, cancelled by any other key, so Win+anything is unaffected. Win+Space stays Windows' layout switcher. visor-wm also sends its `bindd` descriptions (`{"type":"bindings"}`) for the cheat sheet; the shell caches both this and the desktop state and re-sends them when Visor reconnects.
- **A window that takes focus.** `PopupWindow` (a QML type next to `PanelWindow`): frameless, topmost, no taskbar button, DWM rounded corners. Visor's bars are `WS_EX_NOACTIVATE`, so a key in visor-wm or a click on the bar gives Visor no foreground rights; `open()` takes the foreground the way tray clicks already do (attach to the foreground thread's input, `SetForegroundWindow`), then activates. It closes on Escape or when focus goes elsewhere, and gives focus back to the window it took it from. It opens on the monitor of the focused window, or the bar's when clicked.
- **The app index.** `AppIndex` (process-wide, survives config reloads) enumerates `FOLDERID_AppsFolder` through `IShellItem` on a worker thread, three seconds after start, and again when either Start Menu folder changes (`FindFirstChangeNotification`) or a package is installed, updated or removed (`PackageCatalog`). Each app keeps its PIDL; icons come from `IShellItemImageFactory` through an `image://visor-app-icon/` provider, drawn only for visible rows. The `Apps` QML model filters and ranks it (fuzzy match rewarding prefixes, word starts and runs; recently launched apps first for an empty query; launch history in `%APPDATA%\visor\apps.ini`). Every launch runs on its own thread.

**Packaged apps in replace mode.** Launching an AppsFolder item (`ShellExecuteEx` with its PIDL, or `shell:AppsFolder\<id>`) fails with `REGDB_E_CLASSNOTREG` for every packaged app when Explorer isn't the shell, though it works for shortcuts. Full-trust packaged apps (Terminal, Store Notepad) run fine as processes, so the index reads each packaged app's `AppxManifest.xml` and records how to start it without the shell: its execution alias (`%LOCALAPPDATA%\Microsoft\WindowsApps\<family>\wt.exe`, which gives it its package identity), or the executable of a full-trust (`Windows.FullTrustApplication` / `packagedClassicApp`) app. UWP apps (Settings, Calculator) get nothing: they're listed dimmed as "Needs Explorer", after the launchable results, rather than hidden, so a search for "settings" doesn't look broken (Control Panel is the usable alternative). The shell tells Visor its mode (`{"type":"shell","mode":"replace"}`) when it connects; under Explorer every app launches through the shell.

The power menu's actions run in Visor (`LockWorkStation`, `ExitWindowsEx` with `SE_SHUTDOWN_NAME`, `SetSuspendState`); "Quit to Explorer" sends `{"type":"shell.quit"}` to the shell, the same path as Ctrl+Alt+Q. Win+R shows shell32's Run dialog from Visor, after taking the foreground.

### Theming (Phase 5, done)

**Windows is the source of truth.** On Windows the theme belongs to Windows: Personalization owns the wallpaper, the dark/light mode and the accent colour, every app follows them through `UISettings` and the `"ImmersiveColorSet"` `WM_SETTINGCHANGE` broadcast, and a theme is a `.theme` file naming those three. So Visor and visor-wm are followers, like the taskbar is, and the unit of switching is a `.theme` file. (A first cut had Visor own a palette of its own, Omarchy-style, and push it onto Windows; it was replaced because it inverted the platform's model.)

- **Following.** `Themes` (`services/themes.cpp`) reads `SystemUsesLightTheme` / `AppsUseLightTheme`, the accent through `UISettings`, and the wallpaper, and derives the bar's palette the way the taskbar's is derived: Windows 11's dark or light surfaces and text, the accent for highlights. A `QAbstractNativeEventFilter` sees the `ImmersiveColorSet` and `SPI_SETDESKWALLPAPER` broadcasts and `WM_DWMCOLORIZATIONCOLORCHANGED` on Visor's own windows and re-reads. The config's `Theme.qml` builds on `Themes.background` etc., so bindings update live with no QML reload. visor-wm does the same for its borders: `col.active_border = accent` (the default) is resolved from Windows' accent and refreshed on the same broadcasts.
- **Themes are `.theme` files** (`common/theme.cpp`): Windows' own (`%SystemRoot%\Resources\Themes`), the user's (`%LOCALAPPDATA%\Microsoft\Windows\Themes`, one subfolder deep for unpacked theme packs; Settings' `Custom.theme` only while current) and the shipped ones (`config/themes`), read with the profile API, `DisplayName` resource strings resolved with `SHLoadIndirectString`. `CurrentTheme` in the registry says which is active. Applying one does what Settings does: `WallpaperStyle` / `TileWallpaper` and `SPI_SETDESKWALLPAPER`, the `Personalize`, `Explorer\Accent` and `DWM` values for mode and accent, `CurrentTheme`, then the broadcast. Verified in the VM: Notepad and Terminal switch mode, WinUI controls and DWM take the accent, visor-shell's desktop repaints. Sounds, cursors and desktop icons are not applied.
- **Two additions of ours.** A `Wallpaper` path relative to the `.theme` (so themes can ship next to the exe); applying such a theme first installs an absolute-path copy, with a `VisorSource` line, in the user's theme folder, and that copy stands in for the shipped file in the list. And `<name>.terminal.json` beside a `.theme`: a Windows Terminal scheme object, written into every `settings.json` found and set as `profiles.defaults.colorScheme` (JSONC comments stripped; the original kept once as `.before-visor`). Windows has no convention for terminal palettes; this one is Omarchy's. Windows' own themes leave Terminal alone.
- **Spotlight.** Windows Spotlight is Windows 11's default theme; its `.theme` names a 280x175 placeholder (`web\wallpaper\spotlight\img50.jpg`) and `WindowsSpotlight=1`, and Explorer's Spotlight feature swaps in the downloaded picture. Only Settings can switch it on, so the picker leaves it out, and while it is current in replace mode visor-shell paints Windows' stock wallpaper for the mode (`img0.jpg` light, `img19.jpg` dark) rather than the placeholder blown up (`shell/wallpaper.cpp`). It is recognised by the theme's `WindowsSpotlight=1` or by the wallpaper path being in the Spotlight folder: opening Settings > Personalization > Themes under Explorer saves the live state as `Custom.theme`, which names the placeholder without that key (seen in Phase 8), and the placeholder is never shown.
- **Shipped themes:** Tokyo Night, Catppuccin Mocha and Latte, Nord, Gruvbox, Everforest (MIT palettes, credited in each file). Omarchy's background images have no clear provenance, so `etc/make-wallpapers.py` renders a gradient from each theme's Terminal background and accent instead (committed).
- **Switcher:** `ThemePicker.qml` (a `PopupWindow` with wallpaper previews, from the Win+X menu's **Themes** row or `visor, theme`), `visor, theme next` on Super+Ctrl+Shift+Space (Omarchy's key; `RegisterHotKey` accepts it), `visor, theme <name or path>`. Under Explorer the picker's last row opens `ms-settings:themes`, since Settings is where Windows keeps the rest. Everything runs from Visor, so it is the same in replace mode, hosted mode and plain Visor; the Settings row was verified in hosted mode in Phase 8.

### Notifications and on-screen display (Phase 6, done)

What mako and SwayOSD do in Omarchy, in Windows 11's shapes. Everything is in Visor, read from the platform directly: none of it is a shell service, so the link protocol gained no messages.

- **Replace mode only.** Under Explorer, Windows shows toasts, has the bell and acts on the volume keys, so Visor does nothing: `Notifications` stays empty unless the shell reports replace mode, and the OSD is shown by the `visor volume` commands, which only visor-wm sends. A plain Visor under Explorer sees none of this. Verified in hosted mode in Phase 8: a WinRT toast is drawn by Windows and the bar has no bell, and the volume key shows Windows' flyout.
- **Where toasts come from.** Phase 0 found that without Explorer toasts are "lost", which is only the UI: the notification platform (WpnUserService) keeps running, records every toast in `%LOCALAPPDATA%\Microsoft\Windows\Notifications\wpndatabase.db` (SQLite, with the AUMID, XML payload and expiry) and serves them to the `UserNotificationListener` API. The spike showed the API works from a plain exe with no package identity, access is granted already on a fresh install (the machine-wide consent is Allow), it returns the app's display name and parsed text, and `RemoveNotification` really dismisses. So `Notifications` (`services/notifications.cpp`) reads the listener and the database is left alone. Two things the spike had to find out the hard way: the listener's `NotificationChanged` event needs package identity ("element not found" otherwise), and the database changes on disk without any file notification (the service keeps it open, so NTFS updates the directory entry lazily), so the change signal is the platform's own operational event log channel (`Microsoft-Windows-PushNotification-Platform/Operational`, enabled by default, readable by interactive users): an `EvtSubscribe` push subscription for event 3153 (a toast was delivered) and 3055 (toasts were cleared) triggers a re-read. Also, the first call must not be made from inside the shell's `WM_COPYDATA` `SendMessage` (the mode message), where outgoing COM calls fail with `E_UNEXPECTED`; it is queued to the event loop.
- **What is shown.** `Toasts.qml`: pop-ups bottom-right on the primary monitor, newest at the bottom, five seconds each (longer under the pointer), then into the history, as Windows 11 does; the X or a click sends one there at once. A toast's action belongs to its app and the listener doesn't pass it on, so a click does no more. `NotificationCenter.qml` (Win+N, Windows' key, or the bell in the bar, with an unread count kept in `%APPDATA%\visor\notifications.ini`): the history, newest first, with per-item dismiss and Clear all, both of which remove from the platform. The icons are the launcher's (`AppIndex`, by AUMID). The backlog present when Visor starts goes to the history without pop-ups, as Windows wouldn't re-show it either.
- **Volume keys.** Explorer acted on them, so without it they do nothing. `wm.conf` binds them Omarchy-style (`bindeld = , XF86AudioRaiseVolume, Volume up, visor, volume up`; `RegisterHotKey` takes them with no modifier), Visor applies the change through `Audio` (2 % steps, a step unmutes, as Windows) and shows `Osd.qml`: Windows 11's pill (icon, level bar, number) at the bottom centre of the focused window's monitor, with the accent colour from `Theme.qml`, gone two seconds later. Brightness keys have no virtual key (the OS acts on them itself), so `Brightness` (`services/brightness.cpp`, the WMI classes Windows' slider uses) subscribes to `WmiMonitorBrightnessEvent` and the OSD shows the new level; `visor brightness up|down` exist for bindings of your own. Untested: the VM has no brightness control.
- **`OsdWindow`** (`osdwindow.cpp`): the window kind both use. Frameless, topmost, transparent, `WS_EX_NOACTIVATE` like the bars, optionally click-through; placed at the bottom centre, bottom right, centre or under the bar, with a `timeout` after which `shown` turns false so the config can fade it. `PopupWindow` would have taken focus and closed on losing it.
- **Testing.** The VM has no audio device; Hyper-V's Enhanced Session gives it a Remote Audio endpoint while connected (`etc/vm/new-vm.ps1` now sets the VMBus transport that needs), and `etc/vm/screenshot.ps1 -Inside` and `etc/vm/run.ps1` capture and run inside that session, where the console thumbnail only shows the lock screen.

### Hosted mode (Phase 8, done)

The configuration for a real machine: Explorer is the Winlogon shell and `visor-shell --mode hosted` runs beside it. Decisions, and why, each the Windows way where there is one:

- **Starting.** A Run entry (`HKCU\...\CurrentVersion\Run\visor-shell`, written by `etc/install.ps1 -Hosted`): Windows' own mechanism for starting an app at sign-in, listed in Settings > Apps > Startup with a switch. Exclusive with the replace-mode shell override, because replace mode's startup runner would otherwise start a second, hosted shell (which exits as `AlreadyRunning`, but still). In hosted mode visor-shell starts and supervises Visor as in replace mode; `VisorLink::start(true)` in both.
- **The taskbar.** `ABM_SETSTATE` with `ABS_AUTOHIDE` (`shell/taskbar.cpp`): the documented API, and what Settings > Taskbar's "Automatically hide" does, rather than hiding `Shell_TrayWnd`. Verified on 26300: the work area becomes 0,32–1920,1080 (Visor's bar reserves its strip, the auto-hidden taskbar none), maximised windows fill it, the taskbar peeks on hover and briefly when a toast arrives, as it does normally. Because the state is Explorer's persistent setting, the original is recorded once and restored on a clean exit; a shell killed with Stop-Process leaves the record, the next run doesn't overwrite it, and that run's exit restores it (verified).
- **The tray.** Left to Explorer's taskbar. Visor's tray area is hidden (`SystemTray.available` is false), and Visor's own icon lands on the taskbar. The z-order takeover of `Shell_TrayWnd` is deferred.
- **Keys.** Explorer keeps the Win-key shortcuts; the launcher and menu come from the bar's button; `visor-wm` is opt-in (Phase 9, below). The Phase 0 hotkeys stay registered (Ctrl+Alt combinations clash with nothing). Ctrl+Alt+Q, the menu's quit row (labelled **Quit Visor** here) and `visor-shell --quit` quit the shell, Visor and visor-wm.
- **What Visor leaves to Windows.** Everything gated on `Shell.replacingExplorer`: toasts and the Notification Center, the OSD and the switcher. Verified: Settings launches from the launcher and from the picker's row, a WinRT toast is Windows', a volume key shows Windows' flyout, Alt+Tab is Windows'.
- **Scripts.** `etc/vm/deploy.ps1 -Hosted` installs and starts; a plain deploy quits a hosted shell cleanly (`--quit`, so the taskbar comes back) and restarts it; `-Restore` quits it before uninstalling. `--quit` finds the link window by class and posts `WM_CLOSE`; in replace mode that is Ctrl+Alt+Q too (exit code `StartExplorer`).
- **Open.** The tray stays on Explorer's taskbar (above). The watchdog and visor-wm under Explorer came in Phase 9.

### Tiling under Explorer (Phase 9, done)

visor-wm in hosted mode, so a real machine gets Omarchy's tiling and keys with Windows' own everything else. Decisions, Windows' way first:

- **Which keys.** Windows keeps every key it acts on itself under Explorer, and visor-wm has a built-in list of them (`WindowManager::leftToWindows`), not a flag in `wm.conf`: Hyprland has no per-binding mode, and the list is a fact about Windows rather than a preference. Skipped in hosted mode: release bindings (the bare Win press is Start), bindings with no modifier (the volume and media keys), Win+S/X/R/N, Alt+Tab and Alt+Shift+Tab (Windows' switcher), Win+Ctrl+D/F4/Left/Right, and every desktop dispatcher. They're logged as "left to Windows" and left out of the `bindings` message, so the cheat sheet is honest. Everything else binds as in replace mode (`RegisterHotKey`, then the hook for what Windows reserves). The one departure from "Windows' key does Windows' thing": Win+arrows and Win+Shift+arrows stay movefocus and swapwindow rather than Snap, because a Snap would pull a tiled window out of its tile (visor-wm only re-tiles on a drag end), so Snap and tiling would fight. Super+W/V/F/K/E are Explorer's (Widgets, clipboard history, Feedback Hub, Cast, Explorer) and the hook takes them: that is what opting into tiling means.
- **Desktops.** Windows' own. visor-wm's desktop dispatchers are off, nothing is hidden or recorded, and Visor is sent an empty `workspaces` list, so `Desktops.qml` shows nothing (Task View is the UI). Windows on other desktops were already out of the layout, since Windows cloaks them and `classify` ignores cloaked windows; what was added is a layout per Windows desktop, through the documented `IVirtualDesktopManager` (`GetWindowDesktopId`, `IsWindowOnCurrentVirtualDesktop`; it has no "current desktop" or list, which is fine). Each `Desktop` carries the id, a window's cloak event with the window on another desktop keeps its tile there (a switch, or a move in Task View, which re-homes it), and the first uncloaked window of another desktop makes that the current one and re-arranges. Empty desktops are forgotten. Verified: a new desktop (Win+Ctrl+D) shows no pills and tiles its own windows, and the first desktop's layout is intact on return.
- **Opt-in.** A `Tiling` value under `HKCU\Software\visor-shell` (per-user app state in HKCU, where visor-shell already keeps `PreviousTaskbarAutoHide`), written by `install.ps1 -Hosted -Tiling` and cleared by `-Hosted` alone, rather than a flag in the Run entry: the entry stays a plain "start this app", and the setting can later be toggled from a menu without rewriting it. visor-shell reads it in hosted mode and runs the same `Supervisor` as replace mode, passing `--mode hosted`; visor-wm's default, `auto`, looks at who owns the shell window, so a hand-run visor-wm behaves too. `quit` goes to visor-wm in both modes now.
- **Watchdog.** The Run entry runs `visor-session --mode hosted`, still Qt-free (it only parses that one option from the command line). It starts `visor-shell --mode hosted`, restarts crashes, treats exit 0 as the clean quit it is in hosted mode, and where replace mode would start Explorer (Shift, the safe-mode file, a missing exe, 3 crashes in a minute) it logs and stops. `deploy.ps1` and `uninstall.ps1` start and quit through it.
- **Explorer's own windows.** `Progman`, `WorkerW`, `Shell_TrayWnd` and `Shell_SecondaryTrayWnd` are top-level and unowned, so `classify` now ignores them explicitly; otherwise they would count as floating app windows and killactive could send the taskbar `WM_CLOSE`.
- **Verified in the VM** (`deploy.ps1 -Hosted -Tiling`): two windows tile under the bar with gaps, a third splits, Super+Return/W/V and Win+Right work, the volume key shows Windows' flyout, Alt+Tab Windows' switcher, Win+N Windows' Notification Center, Win+Ctrl+D a Windows desktop with no pills; a killed visor-wm is back in a second, a killed visor-shell too (visor-wm handed over, the taskbar still auto-hidden, the record kept), and Ctrl+Alt+Q quits all three and restores the taskbar.

## 5. Safety and test environment

- **Development happens in a Hyper-V VM only. The host's registry is never touched.** The install script refuses to run on any machine unless `-IAmInAVm` is passed or it detects a Hyper-V guest.
- **VM:** Gen 2, vTPM plus Secure Boot (Windows 11 needs both), 4 vCPU, 8 GB, a dynamic 80 GB disk, Default Switch, Enhanced Session.
- **Checkpoints:** take `clean` right after the OS is installed, and `tooling` once the VC++ runtime is in.
- **Deploy:** run a release build on the host, then `windeployqt`, then `Copy-Item -ToSession` over PowerShell Direct. PowerShell Direct needs no networking and works while the guest desktop is black.
- **Recovery, from best to last resort:**
  1. The watchdog falls back to Explorer on its own.
  2. Hold Shift at logon.
  3. From the host, run `Invoke-Command -VMName visor-test { Remove-ItemProperty 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\Winlogon' Shell }`.
  4. Ctrl+Shift+Esc → Task Manager → Run new task → `explorer.exe`, or `regedit`.
  5. Revert to a checkpoint.
- **Scripts:**
  - `etc/install.ps1`: sets the HKCU Shell value, saving the previous value first; `-Hosted` writes the Run entry instead (and removes the Shell value).
  - `etc/uninstall.ps1`: restores the saved value, or deletes it so HKLM's explorer applies, and removes the Run entry.
  - `etc/vm/deploy.ps1`: copies the build into the VM.
  - `etc/vm/new-vm.ps1`: creates the VM. Run it elevated.
- **Open question to check in Phase 0:** whether plain Safe Mode honours the HKCU Shell value. Safe Mode with Command Prompt is the documented fallback.

## 6. Phased plan

Each phase ends with something that runs in the VM.

**Phase 0: VM, safety tooling and a compatibility spike.**
- Build the VM scripts, `visor-session.exe` with its fallback paths, and install/uninstall.
- Build a minimal replace-mode shell: desktop window, ready event, Ctrl+Alt+E to open Explorer.
- Fill in the [I] rows of §1 by testing on 26200: Settings, Store app, toasts, Alt+Tab, each Win hotkey, snapping, Safe Mode.
- **Gate:** choose the default mode.

**Phase 1: shell core and IPC.**
- The `visor-shell` skeleton (both modes, `--mode hosted|replace`) starts and supervises Visor.
- `VisorLink`; `Tasks`, plus the `Tasks` QML type in Visor.
- Wallpaper (replace mode).
- This is the first phase that can be developed on the host in hosted mode.

**Phase 2: tray and appbars.**
- `TrayHost` (hosted-mode z-order coexistence first, so it can be tested without replacing Explorer), plus the `SystemTray` QML type. The coexistence was not built: hosted mode leaves the tray to Explorer's taskbar (Phase 8).
- `AppBarServer` with per-monitor work areas. Visor's exclusive zones must behave the same as under Explorer.
- Fullscreen detection.

Session work runs alongside Phases 2–3:
- Startup runner. Done (`shell/startup.cpp`).
- ITaskbarList progress and overlay. Not done.
- Logoff and shutdown: done. The desktop window agrees to `WM_QUERYENDSESSION` and on `WM_ENDSESSION` the shell tells Visor and visor-wm to quit and exits; `visor-session` sees `SM_SHUTTINGDOWN` and doesn't restart it. Multi-monitor and DPI changes: done in Phase 3d.
- Explorer-as-file-manager polish.

**Phase 3: window manager (`visor-wm`).** 3a (single-monitor dwindle tiling with gaps, float rules and borders), 3b (Hyprland-style key bindings) 3c (virtual desktops that follow Windows 11's conventions, and the `Workspaces` QML type) and 3d (multi-monitor moves, DPI-scaled gaps, apps with minimum sizes, desktops surviving restarts) are done. The multi-monitor behaviour is written but still needs testing on more than one monitor.
- Tiling layouts and workspaces 1–9.
- A key-binding config.
- A `Workspaces` QML type in Visor.
- Float rules for dialogs and tool windows.

**Phase 4: launcher and menu.** Done; see §4 "Launcher and menus".
- `Apps` index and the launcher UI (a bare Win press, or Win+S).
- A system/power menu (Win+X).
- A keybinding cheat sheet (Super+K).
- Win+R opens the Run dialog; Win+E was already in `wm.conf`.

**Phase 5: theming.** Done; see §4 "Theming".
- Windows' theme (a `.theme` file: wallpaper, dark/light mode, accent) drives Visor QML, visor-wm's borders and, for Visor's themes, the Windows Terminal scheme.
- A theme switcher in the menu, and Super+Ctrl+Shift+Space for the next theme.

**Phase 6: notifications and OSD.** Done; see §4 "Notifications and on-screen display".
- Volume and brightness OSD.
- A notification popup and a Notification Center of our own, read from the notification platform.

**Phase 7: gaps, guided by Phase 0.**
- An Alt+Tab switcher with DWM thumbnails. Done: `Switcher.qml`, a `PopupWindow` of `WindowThumbnail` items (`windowthumbnail.cpp`, `DwmRegisterThumbnail` into the popup, fitted to each cell in physical pixels), over `Tasks.zOrder()` (the task windows front to back, which is Windows' most-recently-used order). `wm.conf` binds `ALT, Tab` and `ALT SHIFT, Tab` to `visor, switcher next|previous`; these always go through the keyboard hook, never `RegisterHotKey`, because only the hook sees the key before Windows does and can stop its blind switch. The popup takes focus, so the Alt release reaches it as a key event and picks; `Tasks.bringToFront()` then raises the window (restoring a minimised one) rather than `activate()`, whose taskbar semantics would minimise a window that is already in front.
- A packaged-app strategy for UWP apps (full-trust packaged apps launch, see Phase 4), or a decision that hosted mode is the answer for users who need Settings and Store apps. Decided: hosted mode (Phase 8).

**Phase 8: hosted mode.** Done; see §4 "Hosted mode". Explorer stays the shell, started from a Run entry, taskbar auto-hidden through `ABM_SETSTATE`, Visor supervised by the shell, everything verified under Explorer in the VM.

**Phase 9: tiling under Explorer.** Done; see §4 "Tiling under Explorer". visor-wm in hosted mode (opt-in), Windows' keys and desktops left to Windows, and visor-session as the hosted watchdog.

## 7. Prior art to read while building

Local clones are in the session scratchpad. Re-clone if they are gone.

- ManagedShell:
  - `WindowsTray/TrayService.cs`, `NotificationArea.cs`, `NotifyIcon.cs`: tray wire format and click semantics.
  - `AppBar/AppBarManager.cs`, `FullScreenHelper.cs`.
  - `WindowsTasks/TasksService.cs`: HSHELL codes and the `TaskbandHWND` message table.
  - `Common/SupportingClasses/StartupRunner.cs`, `ShellWindow.cs`, `ShellHelper.cs`: ready event and launching Explorer safely.
- Cairo: `CairoApplication.xaml.cs` (exit code 1), and issues #365, #936 and #953 (packaged apps).
- RetroBar: `Utilities/ExplorerMonitor.cs` (hosted-mode reattachment on `TaskbarCreated`).
- komorebi and GlazeWM (tiling window managers for Windows): window eligibility rules, hiding strategies for workspaces, and handling of DPI and monitor changes.
