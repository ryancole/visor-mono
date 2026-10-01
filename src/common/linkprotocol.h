#pragma once

// The visor-shell <-> Visor link. Keep in sync with visor/src/services/shelllink.cpp.
//
// Transport: WM_COPYDATA between two hidden top-level windows, with
// dwData = kLinkMagic and a UTF-8 JSON object as the payload ({"type": ...}).
// wParam carries the sender's window. Shell -> Visor messages are sent with
// SendMessageTimeout(SMTO_ABORTIFHUNG) so a hung Visor never blocks the shell.
//
// Discovery: the shell's window has class kShellLinkClass. When the shell
// starts it broadcasts the registered message kShellCreatedMessage (like
// Explorer's TaskbarCreated), so a running Visor reconnects; Visor also sends
// "hello" when it starts.
//
// Visor -> shell:
//   {"type":"hello","version":"..."}
// Shell -> Visor:
//   {"type":"tasks.reset","tasks":[Task...],"active":hwnd}
//   {"type":"task.added","task":Task}
//   {"type":"task.changed","task":Task}
//   {"type":"task.removed","hwnd":hwnd}
//   {"type":"task.activated","hwnd":hwnd}      (0: nothing we track)
//   {"type":"quit"}                             (handing the session to Explorer)
// Task = {"hwnd":n,"title":"...","pid":n,"path":"...","flashing":bool}
// HWNDs travel as JSON numbers; their values fit in 32 bits.
namespace visor::link {

constexpr unsigned long kLinkMagic = 0x56534C31; // 'VSL1'
constexpr wchar_t kShellLinkClass[] = L"VisorShellLink";
constexpr wchar_t kShellCreatedMessage[] = L"VisorShellCreated";

} // namespace visor::link
