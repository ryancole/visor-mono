import QtQuick
import Visor

// Notification-area icons. Only shown when visor-shell hosts the tray
// (SystemTray.available); under Explorer the taskbar shows them.
//
// Left click, right click (menu), middle click and double click go to the app.
Row {
    visible: SystemTray.available && SystemTray.count > 0
    spacing: 2

    Repeater {
        model: SystemTray
        delegate: Rectangle {
            id: item
            required property int iconId
            required property string tooltip
            required property string icon

            width: 24
            height: 24
            radius: 6
            color: mouse.containsMouse ? Theme.surface : "transparent"

            Image {
                anchors.centerIn: parent
                width: 16
                height: 16
                sourceSize: Qt.size(16, 16)
                source: item.icon
                smooth: true
            }

            MouseArea {
                id: mouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                onClicked: event => SystemTray.click(item.iconId,
                                                     event.button === Qt.RightButton ? "right"
                                                   : event.button === Qt.MiddleButton ? "middle" : "left")
                onDoubleClicked: event => {
                    if (event.button === Qt.LeftButton)
                        SystemTray.click(item.iconId, "double")
                }
            }
        }
    }
}
