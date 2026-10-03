import QtQuick
import Visor

// Virtual desktops (run by visor-wm), the way Omarchy's bar shows its
// workspaces: the one on screen is a dot (in the accent colour), the others
// their numbers, dimmer when empty. Shown even with one desktop, as Windows
// always keeps Task View on the taskbar; Win+Ctrl+D adds one.
//
// Click: switch to it. Scroll: previous / next.
Row {
    id: desktops
    visible: Workspaces.available
    spacing: 0

    Repeater {
        model: Workspaces
        delegate: Item {
            id: desktop
            required property int index
            required property string name
            required property bool active
            required property int windows

            width: 20
            height: 20

            Rectangle {
                visible: desktop.active
                anchors.centerIn: parent
                width: 10
                height: 10
                radius: 5
                color: Theme.accent
            }

            Label {
                visible: !desktop.active
                anchors.centerIn: parent
                text: desktop.index + 1
                color: mouse.containsMouse || desktop.windows > 0 ? Theme.text : Theme.subtext
            }

            MouseArea {
                id: mouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: Workspaces.activate(desktop.index)
                onWheel: wheel => wheel.angleDelta.y > 0 ? Workspaces.previous() : Workspaces.next()
            }
        }
    }
}
