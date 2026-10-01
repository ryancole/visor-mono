# visor-shell

A replacement for `explorer.exe` as the Windows shell, built around
[Visor](../visor). The goal is an Omarchy-style desktop: tiling, keyboard-driven,
themeable. C++ / Qt (QtCore only), event-driven, small.

Status: **phase 2**: desktop, wallpaper, task list and tray in Visor, app bars and work areas, fullscreen detection, Visor supervision. Phase 0 results (what breaks without Explorer) are in [docs/phase0-compat.md](docs/phase0-compat.md).
See [docs/design.md](docs/design.md) for the architecture and plan.

| Program | What it is |
| --- | --- |
| `visor-session.exe` | What Windows starts at sign-in. Plain Win32, static CRT, no Qt. Starts `visor-shell`, restarts it after a crash, and falls back to Explorer when it can't run. |
| `visor-shell.exe` | Shell services: desktop and wallpaper, the shell-ready signal, hotkeys, window (task) tracking, the notification area (`Shell_TrayWnd`), the app bar server, and starting and supervising Visor. Draws no UI of its own. Visor does that, over the link in [`src/common/linkprotocol.h`](src/common/linkprotocol.h). |

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

The requirements are the same as Visor's. If Visor is checked out next to this repo, its Qt is shared through a junction.

```powershell
pwsh etc/build.ps1 release
```

## Test VM

All of these scripts run on the host. They need Hyper-V admin rights.

```powershell
pwsh etc/vm/new-vm.ps1 -Iso <win11.iso>   # create the VM, then install Windows 11 Pro
pwsh etc/vm/new-vm.ps1 -Checkpoint        # take the "clean" checkpoint
pwsh etc/vm/save-credential.ps1           # save the VM login (encrypted, outside the repo)
pwsh etc/vm/deploy.ps1 -Install           # copy the build and make it the VM user's shell
pwsh etc/vm/deploy.ps1                    # later deploys: copy and restart the shell
pwsh etc/vm/deploy.ps1 -Visor             # also ship ../visor/build/release
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
