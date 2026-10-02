# visor

A desktop for Windows built around Visor, a QML status bar. The goal is an
Omarchy-style desktop: tiling, keyboard-driven, themeable. Native C++ and Qt,
event-driven, small. It can run as a plain app under Explorer, or replace
`explorer.exe` as the Windows shell.

Status: **phase 3b**: Hyprland-style tiling and key bindings (`visor-wm`) on top of phase 2's desktop, wallpaper, task list and tray in Visor, app bars and work areas, fullscreen detection and Visor supervision. Phase 0 results (what breaks without Explorer) are in [docs/phase0-compat.md](docs/phase0-compat.md).
See [docs/design.md](docs/design.md) for the architecture and plan.

| Program | Source | What it is |
| --- | --- | --- |
| `visor.exe` | [`src/bar`](src/bar) | The status bar, configured in QML (live reload). It draws all of the UI. It works on its own under Explorer; with visor-shell it also shows tasks and the tray. See [src/bar/README.md](src/bar/README.md) for config and the QML API. |
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

Logs are in `%LOCALAPPDATA%\visor-shell\logs\` (`session.log`, `shell.log`, `wm.log`).

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
src/bar/        visor.exe: C++ sources, QML types, the default config (src/bar/config)
src/shell/      visor-shell.exe
src/session/    visor-session.exe
src/wm/         visor-wm.exe (its default wm.conf ships in src/bar/config)
src/common/     Code shared by all of them (exit codes, logging, the shell <-> bar link)
etc/            Scripts: bootstrap, build, install/uninstall, icon generator
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
```

`screenshot.ps1` and `input.ps1` work through Hyper-V and PowerShell Direct, so they need no VM window or focus on the host. Note that Windows 11 opens console programs in Windows Terminal, which takes the foreground. The input helper runs under `conhost --headless` so it doesn't disturb what it is testing.

## Window manager

`visor-wm` tiles windows the way Hyprland does in Omarchy. visor-shell starts it in replace mode and restarts it if it crashes. Under Explorer it never starts by itself: run `visor-wm.exe` by hand to try tiling. It has no window, so stop it from Task Manager; your windows stay where they are.

- **Dwindle layout:** each new window splits the focused one, side by side when the space is wider than tall, otherwise one above the other. Closing a window gives its space back.
- **What tiles:** normal resizable app windows. Dialogs, fixed-size, always-on-top and fullscreen windows float, and so do windows of elevated apps (such as Task Manager), because Windows won't let a normal app move them.
- **Maximise and minimise still work:** a maximised window keeps its tile and goes back into it when restored. A minimised window leaves the layout until it comes back.
- **Dragging** a tiled window snaps it back into its tile, or into the layout of the monitor it was dropped on.
- **Borders:** the focused window gets `col.active_border` and the rest `col.inactive_border` (Windows 11 draws them 1 px wide).

Config is `wm.conf`, a subset of `hyprland.conf`, and saving it applies it at once. It is looked up like Visor's config: `--config`, `%VISOR_WM_CONFIG%`, `~/.config/visor/wm.conf`, then [`src/bar/config/wm.conf`](src/bar/config/wm.conf) (debug builds), then `config/wm.conf` next to the exe. The default file documents every setting: gaps, border colours, the dwindle options and window rules such as `windowrule = float, exe:^notepad\.exe$`.

### Keys

The default bindings follow Omarchy. They are all `bind` lines in `wm.conf`, in Hyprland's syntax (`bind = SUPER SHIFT, left, swapwindow, l`), so you can change them and save to apply.

| Keys | Action |
| --- | --- |
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

Windows reserves some Win-key combinations even without Explorer (Win+arrows, Win+Shift+arrows, Win+Return, Win+=), so `visor-wm` catches those with a keyboard hook instead of a hotkey. One limit comes with that: they don't work while an app running as administrator has focus. Win+L always locks the screen. Workspaces come next, in phase 3c.

## Phase 0 hotkeys

| Keys | Action |
| --- | --- |
| Ctrl+Alt+E | File Explorer (This PC) |
| Ctrl+Alt+T | Windows Terminal, or cmd if it won't start |
| Ctrl+Alt+R | Run dialog |
| Ctrl+Alt+Q | Quit to Explorer |
| Ctrl+Shift+Esc | Task Manager (handled by Windows itself) |
