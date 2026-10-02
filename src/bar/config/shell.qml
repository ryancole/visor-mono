import QtQuick
import Visor

// Root of the default visor config. Copy this folder to ~/.config/visor to
// customise it; saving any .qml file in the folder reloads the bar instantly.
ShellRoot {
    // One of each pop-up for the whole desktop; they open on the monitor
    // you're working on.
    Launcher { id: launcher }
    SystemMenu { id: systemMenu }
    CheatSheet { id: cheatSheet }

    // The `visor` key bindings in wm.conf land here.
    Connections {
        target: Shell
        function onCommand(name) {
            switch (name) {
            case "launcher": launcher.toggle(); break
            case "menu": systemMenu.toggle(); break
            case "keys": cheatSheet.toggle(); break
            case "run": Shell.showRunDialog(); break
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
