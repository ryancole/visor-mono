import QtQuick
import Visor

// Root of the default visor config. Copy this folder to ~/.config/visor to
// customise it; saving any .qml file in the folder reloads the bar instantly.
ShellRoot {
    // One bar per monitor; bars come and go as monitors are (un)plugged.
    Instantiator {
        model: Qt.application.screens
        delegate: Bar {
            required property var modelData
            screen: modelData
        }
    }
}
