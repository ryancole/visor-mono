import QtQuick
import Visor

// Virtual desktops (run by visor-wm), as numbered pills; the one on screen is
// highlighted. Like Windows, nothing shows until there is a second desktop
// (Win+Ctrl+D).
//
// Click: switch to it. Scroll: previous / next.
Row {
    id: desktops
    visible: Workspaces.available && Workspaces.count > 1
    spacing: 4

    Repeater {
        model: Workspaces
        delegate: Rectangle {
            id: pill
            required property int index
            required property string name
            required property bool active
            required property int windows

            width: active ? 28 : 20
            height: 20
            radius: 10
            color: active ? Theme.accent
                 : mouse.containsMouse ? "#33ffffff" : Theme.surface
            Behavior on width { NumberAnimation { duration: 120 } }

            Label {
                anchors.centerIn: parent
                text: pill.index + 1
                color: pill.active ? Theme.background : (pill.windows > 0 ? Theme.text : Theme.subtext)
                font.weight: pill.active ? Font.DemiBold : Font.Normal
            }

            MouseArea {
                id: mouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: Workspaces.activate(pill.index)
                onWheel: wheel => wheel.angleDelta.y > 0 ? Workspaces.previous() : Workspaces.next()
            }
        }
    }
}
