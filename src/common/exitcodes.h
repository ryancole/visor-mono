#pragma once

// Exit codes visor-shell uses to tell visor-session what to do next.
//
// Never exit 0 while running as the shell: Winlogon may then start the
// machine-wide shell (explorer) on its own, behind visor-session's back.
namespace visor::exitcode {

constexpr int StartExplorer = 2;  // deliberate quit: hand the session to Explorer
constexpr int Restart = 3;        // restart visor-shell immediately
constexpr int AlreadyRunning = 4; // another visor-shell owns this session
constexpr int Refused = 5;        // another shell (e.g. Explorer) is already the shell

} // namespace visor::exitcode
