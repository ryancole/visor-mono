#pragma once

// The visor-shell <-> Visor link. Both ends: src/shell/visorlink.cpp and
// src/bar/services/shelllink.cpp.
//
// Transport: WM_COPYDATA between two hidden top-level windows, with
// dwData = kLinkMagic and a UTF-8 JSON object as the payload ({"type": ...}).
// wParam carries the sender's window. Shell -> Visor messages are sent with
// SendMessageTimeout(SMTO_ABORTIFHUNG) so a hung Visor never blocks the shell.
//
// Discovery: the shell's window has class kShellLinkClass. When the shell
// starts it broadcasts the registered message kShellCreatedMessage (like
// Explorer's TaskbarCreated), so a running Visor reconnects; Visor also sends
// "hello" when it starts. A WM_CLOSE to the shell's window (what
// `visor-shell --quit` sends, for scripts) quits the shell as Ctrl+Alt+Q does.
//
// Visor -> shell:
//   {"type":"hello","version":"..."}
//   {"type":"tray.click","id":n,"button":"left"|"right"|"middle"|"double","x":n,"y":n}
//                                               (screen position, physical px)
//   {"type":"shell.quit"}                       (hand the session to Explorer)
// Shell -> Visor:
//   {"type":"shell","mode":"replace"|"hosted"}  (first, after hello)
//   {"type":"tasks.reset","tasks":[Task...],"active":hwnd}
//   {"type":"task.added","task":Task}
//   {"type":"task.changed","task":Task}
//   {"type":"task.removed","hwnd":hwnd}
//   {"type":"task.activated","hwnd":hwnd}      (0: nothing we track)
//   {"type":"tray.reset","icons":[TrayIcon...]}
//   {"type":"tray.added","icon":TrayIcon}
//   {"type":"tray.changed","icon":TrayIcon}
//   {"type":"tray.removed","id":n}
//   {"type":"quit"}                             (handing the session to Explorer)
//
// visor-wm sends to the shell's link window; the shell forwards to Visor,
// re-sends the last "workspaces" and "bindings" when Visor (re)connects, and
// sends empty ones when visor-wm exits. Visor's requests go the other way,
// to visor-wm's window (class kWmClass).
//   visor-wm -> shell -> Visor:
//     {"type":"workspaces","workspaces":[Desktop...],"active":index,"pid":n}
//                                                   (empty under Explorer: the desktops are Windows')
//     {"type":"bindings","bindings":[Binding...]}   (the key bindings visor-wm holds, for the cheat sheet)
//     {"type":"visor.command","name":"launcher"}    (a `visor` binding was pressed)
//   Visor -> shell -> visor-wm:
//     {"type":"workspace.activate","index":n}
// Desktop = {"name":"Desktop 1","windows":n}; pid is visor-wm's, so Visor
// can let it take the foreground when the user clicks a desktop.
// Binding = {"keys":"SUPER+Return","description":"Terminal","dispatcher":"exec",
//            "argument":"wt.exe","group":n}
//
// Task = {"hwnd":n,"title":"...","pid":n,"path":"...","flashing":bool}
// TrayIcon = {"id":n,"pid":n,"tip":"...","icon":hicon,"hidden":bool}
//   icon is an HICON owned by visor-shell (icons are session-wide USER
//   objects, so Visor can draw it directly); it changes whenever the image does.
// HWNDs and HICONs travel as JSON numbers; their values fit in 32 bits.
namespace visor::link {

constexpr unsigned long kLinkMagic = 0x56534C31; // 'VSL1'
constexpr wchar_t kShellLinkClass[] = L"VisorShellLink";
constexpr wchar_t kShellCreatedMessage[] = L"VisorShellCreated";
constexpr wchar_t kWmClass[] = L"VisorWm"; // visor-wm's (hidden) window

} // namespace visor::link
