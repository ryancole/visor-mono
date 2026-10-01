# visor-shell

A replacement for `explorer.exe` as the Windows shell, built around
[Visor](../visor). The goal is an Omarchy-style desktop: tiling, keyboard-driven,
themeable. C++ / Qt (QtCore only), event-driven, small.

Status: **phase 0**, a minimal shell for testing what breaks without Explorer.
See [docs/design.md](docs/design.md) for the architecture and plan.

| Program | What it is |
| --- | --- |
| `visor-session.exe` | What Windows starts at sign-in. Plain Win32, static CRT, no Qt. Starts `visor-shell`, restarts it after a crash, and falls back to Explorer when it can't run. |
| `visor-shell.exe` | Shell services: desktop window, the shell-ready signal and hotkeys today; tray, appbars and tasks later. Draws no UI of its own. Visor does that. |

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
```

## Phase 0 hotkeys

| Keys | Action |
| --- | --- |
| Ctrl+Alt+E | File Explorer (This PC) |
| Ctrl+Alt+T | Windows Terminal, or cmd if it won't start |
| Ctrl+Alt+R | Run dialog |
| Ctrl+Alt+Q | Quit to Explorer |
| Ctrl+Shift+Esc | Task Manager (handled by Windows itself) |
