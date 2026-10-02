import QtQuick
import Visor

// Open windows as icons, like taskbar buttons. Only shown when visor-shell is
// running (Tasks.available); under Explorer the taskbar already does this.
//
// Click: focus (or minimise if already focused). Middle-click: close.
Row {
    id: list
    visible: Tasks.available && Tasks.count > 0
    spacing: 2

    Repeater {
        model: Tasks
        delegate: Rectangle {
            id: button
            required property var hwnd
            required property string title
            required property url icon
            required property bool active
            required property bool flashing

            width: 28
            height: 24
            radius: 6
            color: flashing ? Theme.accent
                 : active ? Theme.surface
                 : mouse.containsMouse ? "#14ffffff" : "transparent"

            Image {
                anchors.centerIn: parent
                width: 16
                height: 16
                sourceSize: Qt.size(16, 16)
                source: button.icon
                smooth: true
            }

            // Focused-window marker.
            Rectangle {
                visible: button.active
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                width: 10
                height: 2
                radius: 1
                color: Theme.accent
            }

            MouseArea {
                id: mouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                onClicked: event => event.button === Qt.MiddleButton ? Tasks.close(button.hwnd)
                                                                      : Tasks.activate(button.hwnd)
            }
        }
    }
}
