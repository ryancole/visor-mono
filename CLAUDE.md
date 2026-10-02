# Working on visor

visor is a Windows desktop meant to feel like Omarchy (Hyprland), built the
Windows way. Read `README.md` and `docs/design.md` first.

## The rule: Windows' way wherever Windows has one

Follow Windows 11's conventions wherever Windows has one; borrow from
Omarchy/Hyprland only where Windows has nothing. Whenever you propose
options, a plan or an order of work, **say which option is the most
Windows-conventional** (how Windows 11 does it, or Microsoft's supported
mechanism, as opposed to undocumented behaviour) and lead with it, or say
plainly why you recommend something else.

## Testing

- Never install or test the shell on the host. Everything runs in the
  Hyper-V VM `visor-test` (`etc/vm/*.ps1`): build with
  `pwsh etc/build.ps1 release`, deploy with `pwsh etc/vm/deploy.ps1`, look
  with `etc/vm/screenshot.ps1` (`-Inside` during an Enhanced Session),
  drive it with `etc/vm/input.ps1` and `etc/vm/run.ps1`.
- Don't sign the VM out or restart it unasked; there is no auto sign-in.
- Test every feature in the VM with screenshots before calling it done, and
  update `README.md`, `docs/design.md`, `docs/phase0-compat.md` and
  `src/bar/README.md` with it.

## Code

- Scripts go in `etc/`, code in `src/`. Match the surrounding style and
  comment density.
- Visor -> shell messages go over the WM_COPYDATA link
  (`src/common/linkprotocol.h`); new message types are documented there.
- New UI uses `Theme.qml` (colours follow Windows' mode and accent), never
  literals. Write glyphs as `\uXXXX` escapes, not literal private-use
  characters.
- Don't add a file to `src/bar/services` whose name clashes with a Windows
  SDK header: that folder is on the include path.
- Commit only when asked ("git commit"). End commit messages with the
  Co-Authored-By line.
