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
| Run, RunOnce, Startup folders | Never run [V] | Our own startup runner |
| Start menu, search, Widgets | Gone [V] | Launcher in Visor (Phase 4, done) |
| Win-key hotkeys | Explorer registered most of them, so they go dead. Win+L stays with the system [V] | `RegisterHotKey(MOD_WIN, …)` once Explorer no longer holds them, plus a low-level hook for a bare Win press [I] |
| Alt+Tab | Switching still works, but **no UI is drawn on 24H2 and later** [V] | Our own switcher (later phase) |
| Snap and Snap Layouts, Task View, virtual desktops | Live in twinui inside Explorer, so lost [I] | Defer. Rebuild only if needed |
| Toasts, Notification Center, Quick Settings, volume/brightness OSD | Lost [V]. The volume keys go dead too: Explorer acted on them | Phase 6: toasts and a history read from the notification platform, which keeps running; visor-wm binds the volume keys and Visor shows an OSD. Quick Settings: not rebuilt |
| **UWP/packaged apps, including the Settings app** | **"Class not registered"**: only Explorer, or Shell Launcher (Enterprise-only), can start them [V, Cairo #365/#936] | **Biggest open risk.** See §2 |
| Explorer as a file manager | Works when given a path. A bare `explorer.exe` may try to become the shell [V] | Always launch it with a target. `SetShellWindow` makes our shell visible to it |
| Ctrl+Shift+Esc, Ctrl+Alt+Del | Still work [V] | Our recovery path |
| Shell crash or exit | `AutoRestartShell` restarts only Explorer. Exit code 0 can make winlogon start the HKLM shell [V] | Our own watchdog (§4) |

## 2. The key decision: replace Explorer, or host alongside it

On Windows 11 a full replacement loses the **Settings app**, the **Store**, every packaged app (Copilot, Terminal-as-default-handler flows, Photos, …), and toasts. The only known workaround patches twinui's private ABI and ties us to specific builds [V]. Cairo's maintainers recommend keeping Explorer running for that reason.

The good news is that we don't have to pick now. The "run as a normal app while Explorer is the shell" mode you asked for is a mode Cairo and RetroBar already ship. So **we build both modes from the same code, from day one**:

- **Hosted mode.** Explorer stays the Winlogon shell. We start at logon and hide Explorer's taskbar (auto-hide plus hiding `Shell_TrayWnd` [V, ManagedShell]). We win `FindWindow("Shell_TrayWnd")` by z-order, so tray traffic comes to us, and we pass everything we don't handle on to Explorer. Settings, UWP, toasts, Alt+Tab and snapping keep working. We pay for Explorer's RAM, roughly 100 MB [I].
- **Replace mode.** Our shell is the Winlogon shell and Explorer never starts as the shell. This is the lowest-RAM and fastest-logon option, and it has the gaps listed in §1.

Phase 0 tests §1 in the VM. That result decides which mode is the default. **My recommendation: use hosted mode as the daily driver until Phase 0 shows how bad replace mode is on 26200, then decide.**

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
- **Falls back to Explorer** after 3 crashes in 60 s, or if the shell executable is missing. With no shell window present, a bare `explorer.exe` becomes the full shell, which gives a normal desktop.
- **Escape hatch:** if Shift is held at logon, or `%LOCALAPPDATA%\visor\safe-mode` exists, it starts Explorer immediately.
- Exits with a non-zero code so winlogon never starts the HKLM shell behind our back [V].

### visor-shell.exe, one module per service, all event-driven

| Module | Responsibility |
| --- | --- |
| `ShellWindow` | Desktop window (bottom of z-order, covers the virtual screen), `SetShellWindow`, wallpaper painted through WIC/GDI, reacts to `WM_SETTINGCHANGE` and display changes. Replace mode only. |
| `ShellReady` | Signals `ShellDesktopSwitchEvent` after the desktop and tray exist. |
| `TrayHost` | `Shell_TrayWnd` and `TrayNotifyWnd`. Decodes `WM_COPYDATA` (dwData 0 = appbar, 1 = `NIM_*`, 3 = GetRect) using the **32-bit wire layouts** [V]. Broadcasts `TaskbarCreated`. Loads the SysTray shell service object (volume, network, power icons). Forwards clicks back using v3/v4 semantics. |
| `AppBarServer` | A real registry for `ABM_NEW/QUERYPOS/SETPOS/REMOVE/GETTASKBARPOS/GETSTATE…`. Stacks bars per monitor per edge, sets `SPI_SETWORKAREA` per monitor, sends `ABN_POSCHANGED` and `ABN_FULLSCREENAPP`. In hosted mode it forwards to Explorer, the way ManagedShell does. |
| `Tasks` | Shell hook window, `SetTaskmanWindow`, HSHELL_* events (created, destroyed, activated, flash, fullscreen enter/exit, getminrect), cloak tracking, `TaskbandHWND` progress and overlay. |
| `Startup` | RunOnce (synchronous, with `!`/`*` semantics, delete HKCU values), then Run (HKLM, HKCU, Wow6432Node, Policies), then the Startup folders, all subject to `StartupApproved`. Replace mode only, once per logon. |
| `Hotkeys` | Win+E, Win+R, Win+D, and a bare Win press for the launcher. Replace mode only, since Explorer owns these in hosted mode. |
| `VisorLink` | IPC with Visor, and starts Visor and keeps it running. |

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
- **Spotlight.** Windows Spotlight is Windows 11's default theme; its `.theme` names a 280x175 placeholder (`web\wallpaper\spotlight\img50.jpg`) and `WindowsSpotlight=1`, and Explorer's Spotlight feature swaps in the downloaded picture. Only Settings can switch it on, so the picker leaves it out, and while it is current in replace mode visor-shell paints Windows' stock wallpaper for the mode (`img0.jpg` light, `img19.jpg` dark) rather than the placeholder blown up (`shell/wallpaper.cpp`).
- **Shipped themes:** Tokyo Night, Catppuccin Mocha and Latte, Nord, Gruvbox, Everforest (MIT palettes, credited in each file). Omarchy's background images have no clear provenance, so `etc/make-wallpapers.py` renders a gradient from each theme's Terminal background and accent instead (committed).
- **Switcher:** `ThemePicker.qml` (a `PopupWindow` with wallpaper previews, from the Win+X menu's **Themes** row or `visor, theme`), `visor, theme next` on Super+Ctrl+Shift+Space (Omarchy's key; `RegisterHotKey` accepts it), `visor, theme <name or path>`. Under Explorer the picker's last row opens `ms-settings:themes`, since Settings is where Windows keeps the rest. Everything runs from Visor, so it is the same in replace mode, hosted mode and plain Visor; hosted mode has not been tested in the VM.

### Notifications and on-screen display (Phase 6, done)

What mako and SwayOSD do in Omarchy, in Windows 11's shapes. Everything is in Visor, read from the platform directly: none of it is a shell service, so the link protocol gained no messages.

- **Replace mode only.** Under Explorer, Windows shows toasts, has the bell and acts on the volume keys, so Visor does nothing: `Notifications` stays empty unless the shell reports replace mode, and the OSD is shown by the `visor volume` commands, which only visor-wm sends. A plain Visor under Explorer sees none of this.
- **Where toasts come from.** Phase 0 found that without Explorer toasts are "lost", which is only the UI: the notification platform (WpnUserService) keeps running, records every toast in `%LOCALAPPDATA%\Microsoft\Windows\Notifications\wpndatabase.db` (SQLite, with the AUMID, XML payload and expiry) and serves them to the `UserNotificationListener` API. The spike showed the API works from a plain exe with no package identity, access is granted already on a fresh install (the machine-wide consent is Allow), it returns the app's display name and parsed text, and `RemoveNotification` really dismisses. So `Notifications` (`services/notifications.cpp`) reads the listener and the database is left alone. Two things the spike had to find out the hard way: the listener's `NotificationChanged` event needs package identity ("element not found" otherwise), and the database changes on disk without any file notification (the service keeps it open, so NTFS updates the directory entry lazily), so the change signal is the platform's own operational event log channel (`Microsoft-Windows-PushNotification-Platform/Operational`, enabled by default, readable by interactive users): an `EvtSubscribe` push subscription for event 3153 (a toast was delivered) and 3055 (toasts were cleared) triggers a re-read. Also, the first call must not be made from inside the shell's `WM_COPYDATA` `SendMessage` (the mode message), where outgoing COM calls fail with `E_UNEXPECTED`; it is queued to the event loop.
- **What is shown.** `Toasts.qml`: pop-ups bottom-right on the primary monitor, newest at the bottom, five seconds each (longer under the pointer), then into the history, as Windows 11 does; the X or a click sends one there at once. A toast's action belongs to its app and the listener doesn't pass it on, so a click does no more. `NotificationCenter.qml` (Win+N, Windows' key, or the bell in the bar, with an unread count kept in `%APPDATA%\visor\notifications.ini`): the history, newest first, with per-item dismiss and Clear all, both of which remove from the platform. The icons are the launcher's (`AppIndex`, by AUMID). The backlog present when Visor starts goes to the history without pop-ups, as Windows wouldn't re-show it either.
- **Volume keys.** Explorer acted on them, so without it they do nothing. `wm.conf` binds them Omarchy-style (`bindeld = , XF86AudioRaiseVolume, Volume up, visor, volume up`; `RegisterHotKey` takes them with no modifier), Visor applies the change through `Audio` (2 % steps, a step unmutes, as Windows) and shows `Osd.qml`: Windows 11's pill (icon, level bar, number) at the bottom centre of the focused window's monitor, with the accent colour from `Theme.qml`, gone two seconds later. Brightness keys have no virtual key (the OS acts on them itself), so `Brightness` (`services/brightness.cpp`, the WMI classes Windows' slider uses) subscribes to `WmiMonitorBrightnessEvent` and the OSD shows the new level; `visor brightness up|down` exist for bindings of your own. Untested: the VM has no brightness control.
- **`OsdWindow`** (`osdwindow.cpp`): the window kind both use. Frameless, topmost, transparent, `WS_EX_NOACTIVATE` like the bars, optionally click-through; placed at the bottom centre, bottom right, centre or under the bar, with a `timeout` after which `shown` turns false so the config can fade it. `PopupWindow` would have taken focus and closed on losing it.
- **Testing.** The VM has no audio device; Hyper-V's Enhanced Session gives it a Remote Audio endpoint while connected (`etc/vm/new-vm.ps1` now sets the VMBus transport that needs), and `etc/vm/screenshot.ps1 -Inside` and `etc/vm/run.ps1` capture and run inside that session, where the console thumbnail only shows the lock screen.

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
  - `etc/install.ps1`: sets the HKCU Shell value, saving the previous value first.
  - `etc/uninstall.ps1`: restores the saved value, or deletes it so HKLM's explorer applies.
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
- `TrayHost` (hosted-mode z-order coexistence first, so it can be tested without replacing Explorer), plus the `SystemTray` QML type.
- `AppBarServer` with per-monitor work areas. Visor's exclusive zones must behave the same as under Explorer.
- Fullscreen detection.

Session work runs alongside Phases 2–3:
- Startup runner.
- ITaskbarList progress and overlay.
- Logoff and shutdown (`WM_QUERYENDSESSION`), multi-monitor and DPI changes.
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
- An Alt+Tab switcher with DWM thumbnails.
- A packaged-app strategy for UWP apps (full-trust packaged apps launch, see Phase 4), or a decision that hosted mode is the answer for users who need Settings and Store apps.

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
