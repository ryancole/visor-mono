# visor

A lightweight status bar for Windows, configured in QML. Inspired by
[Quickshell](https://quickshell.org): declarative layout, reactive bindings to
system data, and live reload, without a browser engine.

- **Native and small.** C++ / Qt Quick, no WebView. ~28 MB private memory and
  zero idle CPU with the default software renderer.
- **Event-driven.** System data comes from OS notifications (WinEvent hooks,
  Core Audio callbacks, media session events), never polling.
- **Live reload.** Save any file in the config folder and the bar reloads. If
  the new config doesn't compile, the running bar stays up and the error is
  logged.

## Building

Requirements: Visual Studio 2022+ with the C++ workload, CMake 3.21+, Ninja,
and Python 3 (used once, to fetch Qt).

```powershell
pwsh etc/bootstrap.ps1        # installs pinned Qt 6.10.3 into .deps/ (gitignored)
pwsh etc/build.ps1            # debug build -> build/debug/visor.exe
pwsh etc/build.ps1 release -Run
```

`etc/build.ps1` loads the MSVC environment for you. From a VS Developer prompt or
an IDE with CMake presets support you can use `cmake --preset debug` /
`cmake --build --preset debug` directly.

## Config

visor loads the first of:

1. `--config <file>`
2. `%VISOR_CONFIG%`
3. `~/.config/visor/shell.qml`
4. `src/config/shell.qml` (debug builds only)
5. `config/shell.qml` next to the exe (the default config)

Copy `src/config` to `~/.config/visor` to start customising.

Run it from a terminal to see logs and QML errors.

## Settings

visor's tray icon (left or right click) has the app-level settings:

- **Renderer**: *CPU* (default, ~28 MB) or *GPU* (Direct3D 11, needed for
  shader effects, ~85 MB because it loads the graphics driver). Saved to
  `%APPDATA%\visor\visor.ini`; switching restarts visor, since Qt picks the
  renderer before any window is created. `--renderer cpu|gpu` overrides the
  saved value for one run.
- **Reload config**, **Open config folder**, **Quit**.

The same controls are available to QML through the `Visor` singleton, so a
config can put them in the bar itself.

## QML API (`import Visor`)

| Type | Kind | What it gives you |
| --- | --- | --- |
| `Visor` | singleton | visor itself: `version`, `configPath`, `renderer` (`Visor.Cpu`/`Visor.Gpu`, writable: saves and restarts), `activeRenderer`, `reload()`, `restart()`, `quit()`, `openConfigFolder()`. |
| `ShellRoot` | element | Non-visual root that holds windows, `Instantiator`s, etc. |
| `PanelWindow` | element | Window docked to a monitor edge. `edge`, `thickness`, `screen`, `exclusive` (reserve space like the taskbar), `hideOnFullscreen`, `fullscreenAppActive`. |
| `SystemClock` | element | `date`, `hours`, `minutes`, `seconds`; wakes only on `precision` boundaries (`Seconds`/`Minutes`/`Hours`). |
| `ActiveWindow` | singleton | Focused window: `title`, `appName`, `processPath`, `processId`, `className`. |
| `Audio` | singleton | Default output device: `volume` (0–1, writable), `muted` (writable), `deviceName`, `toggleMute()`. |
| `Media` | singleton | Current media session: `title`, `artist`, `album`, `appId`, `playing`, `playPause()`, `next()`, `previous()`. |
| `Tasks` | singleton model | Open app windows, from [visor-shell](../visor-shell) (empty without it; `available` says which). Roles: `hwnd`, `title`, `appName`, `processPath`, `active`, `flashing`, `icon` (an `image://` URL). `activate(hwnd)` (focus, or minimise if focused), `minimize(hwnd)`, `close(hwnd)`. |

One bar per monitor:

```qml
import QtQuick
import Visor

ShellRoot {
    Instantiator {
        model: Qt.application.screens
        delegate: PanelWindow {
            required property var modelData
            screen: modelData
            edge: PanelWindow.Top
            thickness: 32
            color: "#cc101014"

            SystemClock { id: clock; precision: SystemClock.Minutes }
            Text {
                anchors.centerIn: parent
                color: "white"
                text: Qt.formatDateTime(clock.date, "HH:mm") + "  " + ActiveWindow.title
            }
        }
    }
}
```

## Layout

```
src/            C++ sources, CMake, and the default QML config (src/config)
src/resources/  Icon and Windows resource script
src/services/   System data services exposed to QML
etc/            Scripts (bootstrap, build, icon generator)
.deps/          Local Qt toolchain (created by etc/bootstrap.ps1, not committed)
```
