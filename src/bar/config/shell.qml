import QtQuick
import Visor

// Root of the default visor config. Copy this folder to ~/.config/visor to
// customise it; saving any .qml file in the folder reloads the bar instantly.
ShellRoot {
    // One of each pop-up for the whole desktop; they open on the monitor
    // you're working on.
    Launcher { id: launcher }
    SystemMenu { id: systemMenu; themePicker: themePicker }
    CheatSheet { id: cheatSheet }
    ThemePicker { id: themePicker }
    NotificationCenter { id: notificationCenter }
    QuickSettings { id: quickSettings }
    Switcher { id: switcher }
    // Overlays that never take focus: the volume / brightness display, and
    // toast pop-ups (replace mode only; under Explorer, Windows shows both).
    Osd { id: osd }
    Toasts {}

    // The `visor` key bindings in wm.conf land here: "launcher", "menu",
    // "keys", "run", "theme" (the picker), "theme next" / "theme previous",
    // "theme <name or .theme path>", "volume up" / "volume down" /
    // "volume mute", "brightness up" / "brightness down", "notifications",
    // "quicksettings" (Win+A), "switcher next" / "switcher previous" (Alt+Tab).
    Connections {
        target: Shell
        function onCommand(name) {
            const space = name.indexOf(" ")
            const command = space < 0 ? name : name.slice(0, space)
            const argument = space < 0 ? "" : name.slice(space + 1).trim()
            switch (command) {
            case "launcher": launcher.toggle(); break
            case "menu": systemMenu.toggle(); break
            case "keys": cheatSheet.toggle(); break
            case "run": Shell.showRunDialog(); break
            case "theme":
                if (argument === "next") Themes.next()
                else if (argument === "previous") Themes.previous()
                else if (argument) Themes.apply(argument)
                else themePicker.toggle()
                break
            case "volume":
                // As Windows' keys: 2% steps, and a step unmutes.
                if (argument === "mute") {
                    Audio.toggleMute()
                } else if (argument === "up" || argument === "down") {
                    Audio.muted = false
                    Audio.volume = Math.min(1, Math.max(0, Audio.volume + (argument === "up" ? 0.02 : -0.02)))
                }
                osd.showVolume()
                break
            case "brightness":
                if (argument === "up" || argument === "down")
                    Brightness.level = Math.min(1, Math.max(0, Brightness.level + (argument === "up" ? 0.1 : -0.1)))
                osd.showBrightness()
                break
            case "notifications": notificationCenter.toggle(); break
            case "quicksettings": quickSettings.toggle(); break
            case "switcher": switcher.step(argument === "previous" ? -1 : 1); break
            }
        }
    }

    // One bar per monitor; bars come and go as monitors are (un)plugged.
    Instantiator {
        model: Qt.application.screens
        delegate: Bar {
            required property var modelData
            screen: modelData
            launcherPopup: launcher
            menuPopup: systemMenu
            notificationsPopup: notificationCenter
            quickSettingsPopup: quickSettings
        }
    }
}
