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
| One-switch themes (bar, terminal, wallpaper…) | Theme service | Phase 5 |
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
| Start menu, search, Widgets | Gone [V/I] | Launcher in Visor |
| Win-key hotkeys | Explorer registered most of them, so they go dead. Win+L stays with the system [V] | `RegisterHotKey(MOD_WIN, …)` once Explorer no longer holds them, plus a low-level hook for a bare Win press [I] |
| Alt+Tab | Switching still works, but **no UI is drawn on 24H2 and later** [V] | Our own switcher (later phase) |
| Snap and Snap Layouts, Task View, virtual desktops | Live in twinui inside Explorer, so lost [I] | Defer. Rebuild only if needed |
| Toasts, Notification Center, Quick Settings, volume/brightness OSD | Lost [V] | Defer. Visor already has Audio; an OSD is cheap |
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

### Launcher

The launcher is QML in Visor. The bare Win key opens it.

- **Index:** `shell:AppsFolder` (Start Menu shortcuts plus packaged apps), read lazily and refreshed when those folders change.
- **Launching:** through `IShellItem` / `ShellExecuteEx`.
- **Packaged apps:** in replace mode they are listed only once Phase 0 shows they launch.

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

**Phase 3: window manager (`visor-wm`).** 3a (single-monitor dwindle tiling with gaps, float rules and borders), 3b (Hyprland-style key bindings) and 3c (virtual desktops that follow Windows 11's conventions, and the `Workspaces` QML type) are done. Next is 3d (multiple monitors, DPI and edge cases).
- Tiling layouts and workspaces 1–9.
- A key-binding config.
- A `Workspaces` QML type in Visor.
- Float rules for dialogs and tool windows.

**Phase 4: launcher and menu.**
- `Apps` index and the launcher UI (Super+Space).
- A system/power menu.
- A keybinding cheat sheet.
- A Run box and Win+E.

**Phase 5: theming.**
- One theme file drives Visor QML, the wallpaper, Windows dark mode and accent, and the Windows Terminal scheme.
- A theme switcher in the menu.

**Phase 6: notifications and OSD.**
- Volume and brightness OSD.
- A notification popup. In replace mode this means reading the toast database.

**Phase 7: gaps, guided by Phase 0.**
- An Alt+Tab switcher with DWM thumbnails.
- A packaged-app strategy, or a decision that hosted mode is the answer for users who need Settings and Store apps.

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
