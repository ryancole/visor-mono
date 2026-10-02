import QtQuick
import Visor

// Toast pop-ups, in replace mode (under Explorer Windows shows its own): in
// the bottom-right corner of the primary monitor, the newest at the bottom,
// as Windows 11 does. Each stays five seconds (longer while the pointer is
// over it), then goes to the Notification Center (NotificationCenter.qml,
// the bell in the bar); the X, or a click, sends it there at once. A toast's
// action belongs to its app and the listener API doesn't pass it on, so a
// click can't do more than that.
OsdWindow {
    id: toasts

    readonly property int toastWidth: 360
    readonly property int maxShown: 3

    width: toastWidth
    height: Math.max(1, column.implicitHeight)
    placement: OsdWindow.BottomRight
    margin: 16
    timeout: 0

    ListModel { id: pending }

    Connections {
        target: Notifications
        function onArrived(n) {
            pending.append({ nid: n.id, app: n.app, icon: n.icon, title: n.title, body: n.body })
            while (pending.count > toasts.maxShown)
                pending.remove(0)
            toasts.open()
        }
    }

    function drop(index) {
        pending.remove(index)
        if (pending.count === 0)
            close()
    }

    Column {
        id: column
        width: parent.width
        spacing: 8

        Repeater {
            model: pending
            delegate: Rectangle {
                id: toast
                required property int index
                required property int nid
                required property string app
                required property string icon
                required property string title
                required property string body

                width: toasts.toastWidth
                height: content.implicitHeight + 28
                radius: 8
                color: Theme.popupBackground
                border.width: 1
                border.color: Theme.hover

                Timer {
                    interval: 5000
                    running: !hover.containsMouse
                    onTriggered: toasts.drop(toast.index)
                }

                MouseArea {
                    id: hover
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: toasts.drop(toast.index)
                }

                Row {
                    id: content
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 14
                    anchors.rightMargin: 40
                    spacing: 12

                    Item {
                        width: 28
                        height: 28
                        Image {
                            anchors.fill: parent
                            visible: toast.icon !== ""
                            source: toast.icon
                            sourceSize: Qt.size(28, 28)
                            smooth: true
                        }
                        Icon {
                            anchors.centerIn: parent
                            visible: toast.icon === ""
                            glyph: "" // Ringer
                            color: Theme.accent
                            font.pixelSize: 18
                        }
                    }

                    Column {
                        width: parent.width - 40
                        spacing: 2

                        Label {
                            width: parent.width
                            text: toast.app
                            color: Theme.subtext
                            font.pixelSize: Theme.fontSize - 1
                        }
                        Label {
                            width: parent.width
                            visible: text !== ""
                            text: toast.title
                            font.weight: Font.DemiBold
                        }
                        Label {
                            width: parent.width
                            visible: text !== ""
                            text: toast.body
                            color: Theme.subtext
                            wrapMode: Text.Wrap
                            maximumLineCount: 4
                        }
                    }
                }

                // Dismiss (to the Notification Center), like Windows' X.
                Rectangle {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 8
                    width: 24
                    height: 24
                    radius: 6
                    color: closeMouse.containsMouse ? Theme.hover : "transparent"

                    Icon {
                        anchors.centerIn: parent
                        glyph: "" // ChromeClose
                        color: Theme.subtext
                        font.pixelSize: 10
                    }
                    MouseArea {
                        id: closeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: toasts.drop(toast.index)
                    }
                }
            }
        }
    }
}
