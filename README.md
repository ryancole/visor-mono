# visor

A desktop for Windows built around Visor, a QML status bar. The goal is an
Omarchy-style desktop: tiling, keyboard-driven, themeable. Native C++ and Qt,
event-driven, small. It can run as a plain app under Explorer, or replace
`explorer.exe` as the Windows shell.

Status: **phase 2**: desktop, wallpaper, task list and tray in Visor, app bars and work areas, fullscreen detection, Visor supervision. Phase 0 results (what breaks without Explorer) are in [docs/phase0-compat.md](docs/phase0-compat.md).
See [docs/design.md](docs/design.md) for the architecture and plan.

| Program | Source | What it is |
| --- | --- | --- |
| `visor.exe` | [`src/bar`](src/bar) | The status bar, configured in QML (live reload). It draws all of the UI. It works on its own under Explorer; with visor-shell it also shows tasks and the tray. See [src/bar/README.md](src/bar/README.md) for config and the QML API. |
| `visor-session.exe` | [`src/session`](src/session) | What Windows starts at sign-in. Plain Win32, static CRT, no Qt. Starts `visor-shell`, restarts it after a crash, and falls back to Explorer when it can't run. |
| `visor-shell.exe` | [`src/shell`](src/shell) | Shell services: desktop and wallpaper, the shell-ready signal, hotkeys, window (task) tracking, the notification area (`Shell_TrayWnd`), the app bar server, and starting and supervising Visor. Draws no UI of its own. Visor does that, over the link in [`src/common/linkprotocol.h`](src/common/linkprotocol.h). |

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

Logs are in `%LOCALAPPDATA%\visor-shell\logs\` (`session.log`, `shell.log`).

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
src/common/     Headers shared by all of them (exit codes, the shell <-> bar link)
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

## Phase 0 hotkeys

| Keys | Action |
| --- | --- |
| Ctrl+Alt+E | File Explorer (This PC) |
| Ctrl+Alt+T | Windows Terminal, or cmd if it won't start |
| Ctrl+Alt+R | Run dialog |
| Ctrl+Alt+Q | Quit to Explorer |
| Ctrl+Shift+Esc | Task Manager (handled by Windows itself) |
