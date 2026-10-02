# visor

A lightweight status bar for Windows, with a launcher and menus, configured in QML. Inspired by
[Quickshell](https://quickshell.org): declarative layout, reactive bindings to
system data, and live reload, without a browser engine.

- **Native and small.** C++ / Qt Quick, no WebView. ~28 MB private memory and
  zero idle CPU with the default software renderer.
- **Event-driven.** System data comes from OS notifications (WinEvent hooks,
  Core Audio callbacks, media session events), never polling.
- **Live reload.** Save any file in the config folder and the bar reloads. If
  the new config doesn't compile, the running bar stays up and the error is
  logged.

Built with everything else; see the [top-level README](../../README.md).

## Config

visor loads the first of:

1. `--config <file>`
2. `%VISOR_CONFIG%`
3. `~/.config/visor/shell.qml`
4. `src/bar/config/shell.qml` (debug builds only)
5. `config/shell.qml` next to the exe (the default config)

Copy `src/bar/config` to `~/.config/visor` to start customising. Colours follow
Windows' dark/light mode and accent (see [Themes](../../README.md#themes));
`Theme.qml` turns them into the config's palette and holds the fonts and sizes.

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
| `PopupWindow` | element | A pop-up that takes keyboard focus (the launcher, menus): frameless, topmost, rounded. `open(screen?)` activates it on the focused window's monitor (or `screen`), `close()`, `toggle(screen?)`; `placement` (`Below` the bar centred, `BelowLeft`, `BelowRight`, `Center`), `margin`, `closeOnDeactivate`. Closes on Esc and when focus goes elsewhere; signals `opened`, `closed`. |
| `WindowThumbnail` | element | A live preview of another window, drawn by DWM into the item's rectangle (fitted, centred): `hwnd` (as `Tasks` gives it), `sourceSize`. Paints nothing itself; a minimised window shows blank. |
| `OsdWindow` | element | An overlay that never takes focus (the volume display, toasts): frameless, topmost, transparent by default, `WS_EX_NOACTIVATE` like the bars. `open(screen?)` places and shows it without activating, `close()`; `placement` (`Bottom` centre, where Windows 11 puts its volume flyout; `BottomRight`, where it puts toasts, on the primary monitor; `Center`; `Top`), `margin`, `clickThrough`, `timeout` (ms until `shown` turns false again, for a fade; 0 keeps it). |
| `SystemClock` | element | `date`, `hours`, `minutes`, `seconds`; wakes only on `precision` boundaries (`Seconds`/`Minutes`/`Hours`). |
| `ActiveWindow` | singleton | Focused window: `title`, `appName`, `processPath`, `processId`, `className`. |
| `Audio` | singleton | Default output device: `volume` (0–1, writable), `muted` (writable), `deviceName`, `toggleMute()`. |
| `Brightness` | singleton | The built-in display's brightness, where the hardware has it (`available`; laptops): `level` (0–1, writable), changing live when the brightness keys, which the OS handles itself, move it. |
| `Media` | singleton | Current media session: `title`, `artist`, `album`, `appId`, `playing`, `playPause()`, `next()`, `previous()`. |
| `SystemTray` | singleton model | Notification-area icons, hosted by [visor-shell](../shell) (empty without it). Roles: `iconId`, `tooltip`, `icon`, `processId`. `click(iconId, button)` with `"left"`, `"right"`, `"middle"` or `"double"` forwards the click to the app. |
| `Workspaces` | singleton model | Virtual desktops, from [visor-wm](../wm) (empty without it; `available` says which). Roles: `name` ("Desktop 1"), `active`, `windows`. Properties `count`, `active` (index). `activate(index)`, `next()`, `previous()`. The default config's `Desktops.qml` shows them once there are two or more. |
| `Tasks` | singleton model | Open app windows, from [visor-shell](../shell) (empty without it; `available` says which). Roles: `hwnd`, `title`, `appName`, `processPath`, `active`, `flashing`, `icon` (an `image://` URL). `activate(hwnd)` (focus, or minimise if focused), `minimize(hwnd)`, `close(hwnd)`, `bringToFront(hwnd)` (focus, whatever is in front), `zOrder()` (the windows front to back, as `{hwnd, title, appName, icon}` objects: Alt+Tab's order). |
| `Apps` | singleton model | The installed apps (`shell:AppsFolder`) matching `query`, best first; recently opened first for an empty query. Roles: `key`, `name`, `id`, `icon` (an `image://` URL), `packaged`, `launchable` (false for UWP apps in replace mode), `recent`. `launch(row, asAdmin = false)`, `refresh()`, `ready`, `count`. |
| `KeyBindings` | singleton model | visor-wm's key bindings, for a cheat sheet (empty without it). Roles: `keys` ("SUPER+Return"), `label` ("Win + Enter"), `description`, `dispatcher`, `argument`, `group`. `groupCount`, `group(n)` (the n-th group as `{label, description}` objects). |
| `Shell` | singleton | The session: `available`, `mode` (`"replace"`, `"hosted"` or `""`), `replacingExplorer` (what the notifications, OSD and switcher gate on: in hosted mode and plain Visor, Windows draws those); signal `command(name)` for the `visor` key bindings in `wm.conf` (e.g. `"launcher"`, `"theme next"`); `run(commandLine)`, `showRunDialog()`, `lock()`, `signOut()`, `sleep()`, `restart()`, `shutDown()`, `quitToExplorer()` (quits visor-shell and visor; in hosted mode Explorer is there already, so the default menu labels it "Quit Visor"). |
| `Notifications` | singleton model | Toast notifications in replace mode, from the notification platform's listener API (empty under Explorer, which shows them itself): the history, newest first, with `available`, `access` (`\"allowed\"`, `\"denied\"`, `\"unspecified\"`), `count`, `unread`. Roles: `notificationId`, `app`, `appId`, `icon` (an `image://` URL, from the app index, or `\"\"`), `title`, `body`, `time`, `unread`. Signal `arrived(notification)` for each new one while running (`{id, app, appId, icon, title, body, time}`); `dismiss(id)`, `clearAll()` (from the platform too), `markRead()`, `refresh()`. |
| `Themes` | singleton model | Windows' personalisation, and the `.theme` files to switch it with (Windows' own, the user's, and Visor's shipped ones). Colours derived from Windows' mode and accent, live: `light`, `appsLight`, `background`, `surface`, `text`, `subtext`, `accent`, `wallpaper`. Roles: `key` (the `.theme` path), `name`, `light`, `accent`, `wallpaper`, `current`, `source` (`"Windows"`, `"Visor"` or `""`). Properties `count`, `current` (path), `currentName`. `apply(keyOrName)` does what picking the theme in Settings does (plus the Terminal scheme for Visor's themes); `next()`, `previous()`, `indexOf(keyOrName)`, `refresh()`. |

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

The default config adds `Launcher.qml`, `SystemMenu.qml`, `CheatSheet.qml`, `ThemePicker.qml`, `NotificationCenter.qml` and `Switcher.qml` (each a `PopupWindow`), opened from `shell.qml` when `Shell.command` names them (`launcher`, `menu`, `keys`, `theme`, `notifications`, `switcher next` / `switcher previous`; `run` shows the Run dialog, `theme next` / `theme <name>` switch themes, `volume up` / `down` / `mute` and `brightness up` / `down` change those and show the display) and from the buttons in `Bar.qml`; plus two `OsdWindow`s, `Osd.qml` (the volume and brightness display) and `Toasts.qml` (notification pop-ups).

## Layout

```
src/bar/            C++ sources and CMake for visor.exe
src/bar/config/     The default QML config, wm.conf, and the shipped themes (themes/*.theme)
src/bar/resources/  Icon and Windows resource script
src/bar/services/   System data services exposed to QML
```
