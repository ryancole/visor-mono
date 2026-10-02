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

    // The `visor` key bindings in wm.conf land here: "launcher", "menu",
    // "keys", "run", "theme" (the picker), "theme next" / "theme previous",
    // or "theme <name or .theme path>".
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
        }
    }
}
