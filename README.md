# visor

A desktop for Windows built around Visor, a QML status bar. The goal is an
Omarchy-style desktop: tiling, keyboard-driven, themeable. Native C++ and Qt,
event-driven, small. It can run as a plain app under Explorer, or replace
`explorer.exe` as the Windows shell.

Status: **phase 6**: notifications (toast pop-ups and a Notification Center in Visor, read from the notification platform that keeps running without Explorer) and an on-screen display for the volume keys, on top of phase 5's Windows themes, followed and switched by Visor (bar, wallpaper, dark/light mode, accent, window borders and the Terminal scheme in one go), phase 4's app launcher, power menu and key-binding cheat sheet in Visor, phase 3's Hyprland-style tiling, key bindings and Windows-style virtual desktops (`visor-wm`) and phase 2's desktop, wallpaper, task list and tray, app bars and work areas, fullscreen detection and Visor supervision. Phase 0 results (what breaks without Explorer) are in [docs/phase0-compat.md](docs/phase0-compat.md).
See [docs/design.md](docs/design.md) for the architecture and plan.

| Program | Source | What it is |
| --- | --- | --- |
| `visor.exe` | [`src/bar`](src/bar) | The status bar, launcher, menus and theme switcher, configured in QML (live reload). It draws all of the UI. It works on its own under Explorer; with visor-shell it also shows tasks and the tray. See [src/bar/README.md](src/bar/README.md) for config and the QML API. |
| `visor-session.exe` | [`src/session`](src/session) | What Windows starts at sign-in. Plain Win32, static CRT, no Qt. Starts `visor-shell`, restarts it after a crash, and falls back to Explorer when it can't run. |
| `visor-shell.exe` | [`src/shell`](src/shell) | Shell services: desktop and wallpaper, the shell-ready signal, hotkeys, window (task) tracking, the notification area (`Shell_TrayWnd`), the app bar server, and starting and supervising Visor and visor-wm. Draws no UI of its own. Visor does that, over the link in [`src/common/linkprotocol.h`](src/common/linkprotocol.h). |
| `visor-wm.exe` | [`src/wm`](src/wm) | The tiling window manager, in Hyprland's role: tiles app windows with the dwindle layout inside the space Visor's bar leaves. Configured by a `hyprland.conf`-style `wm.conf`. See [Window manager](#window-manager). |

## Safety first

**Only install this in a VM** until it is proven. `etc/install.ps1` refuses to run on a physical machine.

On a real machine, run `visor-shell --mode hosted` alongside Explorer, which is what `pwsh etc/build.ps1 -Run` does.

### Emergency recovery

If sign-in lands on a black or broken desktop, try these in order:

1. **Wait.** If `visor-shell` crashes 3 times within a minute, `visor-session` starts Explorer.
2. **Sign out and back in holding Shift.** That session starts Explorer instead. Sign out with Ctrl+Alt+Del.
3. **Use Task Manager.** Press Ctrl+Shift+Esc, choose **Run new task**, and enter `explorer.exe`. Enter `regedit` instead to edit the setting by hand.
4. **Restore from the host:** `pwsh etc/vm/deploy.ps1 -Restore`. This removes the setting and starts Explorer in the VM. It works even when the VM's screen is black.
5. **Use the safe-mode file.** Create `%LOCALAPPDATA%\visor-shell\safe-mode` and every sign-in starts Explorer until you delete it.
6. **Edit the registry by hand.** Delete the `Shell` value under `HKCU\Software\Microsoft\Windows NT\CurrentVersion\Winlogon`. The machine-wide `HKLM` value is never touched.
7. **Revert the VM** to its `clean` checkpoint.

Logs are in `%LOCALAPPDATA%\visor-shell\logs\` (`session.log`, `shell.log`, `wm.log`, and `visor.log` when Visor isn't run from a terminal). Windows hidden on other desktops are listed in `%LOCALAPPDATA%\visor-shell\wm-hidden.txt` while `visor-wm` runs.

## Building

Requirements: Visual Studio 2022+ with the C++ workload, CMake 3.21+, Ninja,
and Python 3 (used once, to fetch Qt).

```powershell
pwsh etc/bootstrap.ps1          # pinned Qt 6.10.3 into .deps/ (gitignored)
pwsh etc/build.ps1              # debug build of everything -> build/debug/
pwsh etc/build.ps1 release      # what etc/vm/deploy.ps1 ships to the VM
pwsh etc/build.ps1 -Run         # build, then run the bar
pwsh etc/build.ps1 -RunShell    # build, then run visor-shell alongside Explorer (hosted mode)
```

`etc/build.ps1` loads the MSVC environment for you. From a VS Developer prompt or
an IDE with CMake presets support you can use `cmake --preset debug` /
`cmake --build --preset debug` directly. All executables, the Qt runtime and the
bar's default config land in one folder, `build/<preset>/`.

## Layout

```
src/bar/        visor.exe: C++ sources, QML types, the default config and the shipped themes (src/bar/config)
src/shell/      visor-shell.exe
src/session/    visor-session.exe
src/wm/         visor-wm.exe (its default wm.conf ships in src/bar/config)
src/common/     Code shared by all of them (exit codes, logging, the shell <-> bar link)
etc/            Scripts: bootstrap, build, install/uninstall, icon and wallpaper generators
etc/vm/         Test VM scripts: create, deploy, screenshot, input
docs/           Design, plan, and the compatibility results
.deps/          Local Qt toolchain (created by etc/bootstrap.ps1, not committed)
```

## Test VM

All of these scripts run on the host. They need Hyper-V admin rights.

```powershell
pwsh etc/vm/new-vm.ps1 -Iso <win11.iso>   # create the VM, then install Windows 11 Pro
pwsh etc/vm/new-vm.ps1 -Checkpoint        # take the "clean" checkpoint
pwsh etc/vm/save-credential.ps1           # save the VM login (encrypted, outside the repo)
pwsh etc/vm/deploy.ps1 -Install           # copy the build and make it the VM user's shell
pwsh etc/vm/deploy.ps1                    # later deploys: copy everything to C:\visor and restart
pwsh etc/vm/deploy.ps1 -Restore           # back to Explorer
pwsh etc/vm/screenshot.ps1                # the VM's screen -> build/vm-screen.png
pwsh etc/vm/input.ps1 -Click 948,16 -Button right   # click inside the VM
pwsh etc/vm/input.ps1 -Key ctrl+alt+r     # press keys inside the VM
pwsh etc/vm/run.ps1 -Script '...'         # run PowerShell in the VM user's session, and get its output
pwsh etc/vm/screenshot.ps1 -Inside        # the screen as the signed-in user sees it (works in an Enhanced Session)
```

The VM has no sound hardware. For anything that needs an audio device (the volume keys), connect to it with vmconnect's **Enhanced Session** (View menu) with remote audio on: Windows in the VM then has a Remote Audio endpoint for as long as you're connected. While you are, the console shows the lock screen, so `screenshot.ps1` needs `-Inside`.

`screenshot.ps1` and `input.ps1` work through Hyper-V and PowerShell Direct, so they need no VM window or focus on the host. Note that Windows 11 opens console programs in Windows Terminal, which takes the foreground. The input helper runs under `conhost --headless` so it doesn't disturb what it is testing.

## Launcher and menus

These are QML in Visor ([`src/bar/config`](src/bar/config): `Launcher.qml`, `SystemMenu.qml`, `CheatSheet.qml`), opened by key bindings in `wm.conf` that visor-wm passes to Visor (`bindd = SUPER, S, Launcher, visor, launcher`), or from the button at the left of the bar.

| Keys | Action |
| --- | --- |
| Win (pressed alone), Win+S, or click the bar's button | The launcher |
| Win+X, or right-click the button | The system menu: the theme picker, then lock, sign out, sleep, restart, shut down, quit to Explorer |
| Super+K | The key-binding cheat sheet |
| Win+R | The Run dialog |
| Super+Ctrl+Shift+Space | The next theme (see [Themes](#themes)) |
| Win+N, or click the bell | The Notification Center (see [Notifications and on-screen display](#notifications-and-on-screen-display)) |

- **Launcher:** lists what the Start menu would (`shell:AppsFolder`: Start Menu shortcuts and packaged apps), indexed in the background and refreshed when a Start Menu folder or a package changes. Type to search (fuzzy: `wt` finds Windows Terminal); Up/Down pick, Enter opens, Ctrl+Shift+Enter opens as administrator, Esc closes. With nothing typed, recently opened apps come first. If nothing matches, Enter runs what you typed as a command. Launches run on their own threads, so one that hangs blocks nothing.
- **Packaged apps in replace mode:** without Explorer, Windows can't start packaged apps through the shell ("class not registered"). Those that run as ordinary processes (Terminal, Store Notepad, Paint) are started through their execution alias or executable instead, found in their manifest. UWP apps (Settings, Calculator, Clock) couldn't show a window anyway; they're listed dimmed as "Needs Explorer", after everything else. Under Explorer everything launches normally.
- The bare Win press uses visor-wm's keyboard hook, so like the hooked keys below it doesn't work while an app running as administrator has focus; Win+S does. Win+Space is left alone: Windows uses it to switch keyboard layouts.
- Pop-ups open on the monitor of the focused window, under the bar (the cheat sheet in the middle), and close when focus goes elsewhere.

## Themes

Windows owns the look, and Visor follows it, the way the taskbar and every app do: the bar's colours come from Windows' dark/light mode and accent colour, and change the moment they do, whether through Settings, a `.theme` file or Visor itself. A theme is Windows' own kind, a `.theme` file (wallpaper, dark/light mode, accent colour), and switching one does what picking it in Settings > Personalization does, so apps, window frames and the wallpaper follow. In replace mode Settings can't run, so Visor has the switcher: **Themes** in the Win+X menu, or Super+Ctrl+Shift+Space for the next one. Under Explorer the picker also offers Settings itself.

The picker lists Windows' themes (`Windows (light)`, `Windows (dark)`, Flow, Glow, ...), the ones Settings saved for you (`%LOCALAPPDATA%\Microsoft\Windows\Themes`, where theme packs unpack too), and six that ship with Visor (`config/themes`): Tokyo Night, Catppuccin Mocha, Catppuccin Latte (light), Nord, Gruvbox and Everforest, all MIT palettes, each a `.theme` with a generated wallpaper (`etc/make-wallpapers.py`) and, Omarchy-style, a matching Windows Terminal scheme in `<name>.terminal.json` beside it. Windows has no convention for terminal palettes, so that part is the one addition: applying such a theme puts the scheme into Terminal's `settings.json` and makes it the default profiles' scheme (keeping the original once as `settings.json.before-visor`); Windows' own themes leave Terminal alone.

Of a `.theme`, Visor applies the wallpaper and its fit, `SystemMode` / `AppMode` and `ColorizationColor` (the registry values Settings writes, followed by the `ImmersiveColorSet` broadcast everything listens for), and records it as Windows' current theme. Sounds, cursors and desktop icons are left alone. A shipped theme names its wallpaper relative to itself, which Windows can't read, so applying one first installs an absolute-path copy in your Windows theme folder; from then on Settings shows and can re-apply it like any other. Drop your own `.theme` (and optional `.terminal.json`) in `%LOCALAPPDATA%\Microsoft\Windows\Themes` to add one. The whole switch runs from Visor, so it works the same under Explorer. `visor-wm`'s focused-window border is `accent` by default and follows the accent colour too.

## Notifications and on-screen display

Windows shows toasts, keeps them in the Notification Center and puts a flyout on screen for the volume keys. All of that is Explorer's, so in replace mode Visor does it, the way Windows 11 does; under Explorer it stays out of the way.

- **Toasts** appear at the bottom right of the primary monitor, newest at the bottom, for five seconds (longer while the pointer is over one), then go to the history. The X, or a click, sends one there at once. What a click would do in the app isn't available to us, so that is all it does.
- **The Notification Center** is the bell at the right of the bar, with how many notifications came since you last looked, or Win+N (Windows' key): the history, newest first, with the app's icon and name. Up/Down and Delete, or the X, dismiss one; **Clear all** clears them. Both remove them from Windows too, so they don't come back.
- **Where they come from:** without Explorer, Windows still delivers every toast to its notification platform (which is how they reach the Notification Center normally); Visor reads them from there through the same listener API a notification-mirroring app would use. It's allowed by default. If Settings > Privacy & security > Notifications ever turns access off for Visor, the center says so.
- **The volume keys** change the volume in 2 % steps (a step unmutes, as in Windows) and show the display: a pill at the bottom centre of the monitor you're working on, with the level and the number, gone two seconds later. It never takes focus. Without Explorer, Windows itself does nothing with these keys; they're `wm.conf` bindings (`bindeld = , XF86AudioRaiseVolume, Volume up, visor, volume up`, Omarchy's names), so they need `visor-wm`.
- **Brightness** keys are handled by Windows itself even without Explorer, and on hardware that has brightness control (laptops) the display shows the new level when they're pressed. `visor brightness up` / `down` are there to bind to keys of your own. *Not tested yet: the VM has no brightness control.*

## Window manager

`visor-wm` tiles windows the way Hyprland does in Omarchy. visor-shell starts it in replace mode and restarts it if it crashes. Under Explorer it never starts by itself: run `visor-wm.exe` by hand to try tiling. It has no window, so stop it from Task Manager; your windows stay where they are.

- **Dwindle layout:** each new window splits the focused one, side by side when the space is wider than tall, otherwise one above the other. Closing a window gives its space back.
- **What tiles:** normal resizable app windows. Dialogs, fixed-size, always-on-top and fullscreen windows float, and so do windows of elevated apps (such as Task Manager), because Windows won't let a normal app move them.
- **Maximise and minimise still work:** a maximised window keeps its tile and goes back into it when restored. A minimised window leaves the layout until it comes back.
- **Dragging** a tiled window snaps it back into its tile, or into the layout of the monitor it was dropped on.
- **Borders:** the focused window gets `col.active_border` and the rest `col.inactive_border` (Windows 11 draws them 1 px wide). A colour can be `accent`: Windows' accent colour, followed as it changes (the default for the focused window; see [Themes](#themes)).
- **Windows that won't shrink:** some apps have a minimum size (Windows Terminal stops at about 465 px wide). When a window ends up bigger than its tile, `visor-wm` remembers that size and gives the window that much room, kept on-screen, so it covers part of its neighbour instead of running off the edge. Resizing stops there too.
- **Multiple monitors:** each monitor has its own layout. New windows tile on the monitor they open on. Gaps are logical pixels, so they scale with each monitor's DPI, as in Hyprland. Focus and swapping cross monitors. Where there's no window to swap with, Super+Shift+arrows moves the window to the monitor in that direction, like Win+Shift+Left/Right in Windows (floating windows too). Windows on a monitor that's unplugged move to the primary one. *This part hasn't been tested with more than one monitor yet.*

Config is `wm.conf`, a subset of `hyprland.conf`, and saving it applies it at once. It is looked up like Visor's config: `--config`, `%VISOR_WM_CONFIG%`, `~/.config/visor/wm.conf`, then [`src/bar/config/wm.conf`](src/bar/config/wm.conf) (debug builds), then `config/wm.conf` next to the exe. The default file documents every setting: gaps, border colours, the dwindle options, `source = <file>` includes and window rules such as `windowrule = float, exe:^notepad\.exe$`.

### Keys

The default bindings follow Omarchy. They are all `bind` lines in `wm.conf`, in Hyprland's syntax (`bind = SUPER SHIFT, left, swapwindow, l`), so you can change them and save to apply.

| Keys | Action |
| --- | --- |
| Win, Super+S, Super+X, Super+K, Super+R | Launcher, system menu, cheat sheet, Run (see [Launcher and menus](#launcher-and-menus)) |
| Super+Ctrl+Shift+Space | Next theme (see [Themes](#themes)) |
| Volume keys | Volume up / down / mute, with the on-screen display |
| Win+N | Notification Center |
| Super+Return | Terminal (`wt.exe`) |
| Super+E | File Explorer |
| Super+W | Close the window |
| Super+V | Float or tile the window |
| Super+F | Fullscreen, covering the bar |
| Super+Alt+F | Maximise, keeping the bar |
| Super+J | Switch the window's split between side by side and stacked |
| Super+arrows | Move focus |
| Super+Shift+arrows | Swap the window with its neighbour |
| Super+minus / equal | Narrower / wider (add Shift for shorter / taller) |

Windows reserves some Win-key combinations even without Explorer (Win+arrows, Win+Shift+arrows, Win+Return, Win+=), so `visor-wm` catches those with a keyboard hook instead of a hotkey, as it does the bare Win press (`bindr = SUPER, SUPER_L, ...`: fires on release, if nothing else was pressed). One limit comes with that: they don't work while an app running as administrator has focus. Win+L always locks the screen.

### Desktops

Windows 11's virtual desktops live in Explorer, so they're gone in replace mode. `visor-wm` provides its own, working the way Windows' do:

| Keys | Action |
| --- | --- |
| Win+Ctrl+D | New desktop (and go to it) |
| Win+Ctrl+Left / Right | Previous / next desktop |
| Win+Ctrl+F4 | Close the desktop; its windows move to the one on the left |
| Win+Ctrl+Shift+Left / Right | Move the window to the previous / next desktop, and go with it |

- Create as many as you like. They're named "Desktop 1", "Desktop 2"..., and each one covers every monitor.
- Visor's task list shows only the current desktop's windows, which is Windows' default.
- Once there are two or more desktops, the bar shows them as numbered pills. Click one to switch, or scroll over them.
- Windows has no key for moving a window to another desktop (it uses Task View), so the last row is our addition.
- Windows on other desktops are hidden. Desktops survive `visor-wm` restarting (after a crash, a redeploy, or visor-shell restarting): the next `visor-wm` picks up the desktops and their hidden windows. If the session goes back to Explorer (Ctrl+Alt+Q, or visor-shell not coming back), every window is shown first, so none is ever lost.


## Phase 0 hotkeys

visor-shell's own, which work even when visor-wm is down:

| Keys | Action |
| --- | --- |
| Ctrl+Alt+E | File Explorer (This PC) |
| Ctrl+Alt+T | Windows Terminal, or cmd if it won't start |
| Ctrl+Alt+R | Run dialog |
| Ctrl+Alt+Q | Quit to Explorer |
| Ctrl+Shift+Esc | Task Manager (handled by Windows itself) |
