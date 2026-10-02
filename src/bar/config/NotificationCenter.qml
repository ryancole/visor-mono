import QtQuick
import Visor

// Notification Center's role: the toasts that came and went, newest first,
// under the bar's bell or on Win+N (Windows' key for it). Up/Down pick,
// Delete dismisses one, Esc closes; "Clear all" does what Windows' does.
// Opening it marks everything read, which is what the bell's number counts.
// Replace mode only: under Explorer, Windows has the real one.
PopupWindow {
    id: center

    readonly property int maxHeight: 520

    width: 380
    height: header.height + Math.min(list.contentHeight, maxHeight) + 24
    color: Theme.popupBackground
    placement: PopupWindow.BelowRight

    onOpened: {
        Notifications.markRead()
        list.currentIndex = 0
        keys.forceActiveFocus()
    }

    function dismissCurrent() {
        const item = list.itemAtIndex(list.currentIndex)
        if (item)
            Notifications.dismiss(item.notificationId)
    }

    Item {
        id: keys
        anchors.fill: parent
        focus: true
        Keys.onUpPressed: list.decrementCurrentIndex()
        Keys.onDownPressed: list.incrementCurrentIndex()
        Keys.onDeletePressed: center.dismissCurrent()

        Item {
            id: header
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 18
            anchors.topMargin: 14
            height: 28

            Label {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: "Notifications"
                font.pixelSize: Theme.fontSize + 2
                font.weight: Font.DemiBold
            }

            Label {
                visible: Notifications.count > 0
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: "Clear all"
                color: clearMouse.containsMouse ? Theme.text : Theme.accent

                MouseArea {
                    id: clearMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        Notifications.clearAll()
                        center.close()
                    }
                }
            }
        }

        // Nothing to show, or no way to show it.
        Label {
            visible: Notifications.count === 0
            anchors.top: header.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 18
            anchors.topMargin: 10
            height: 40
            wrapMode: Text.Wrap
            text: Notifications.available ? "No new notifications"
                : Notifications.access === "denied" || Notifications.access === "unspecified"
                    ? "Visor needs notification access (Settings > Privacy & security > Notifications)."
                    : "Notifications aren't available."
            color: Theme.subtext
        }

        ListView {
            id: list
            anchors.top: header.bottom
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 6
            anchors.topMargin: 4
            visible: Notifications.count > 0
            clip: true
            model: Notifications
            highlightMoveDuration: 0
            highlightResizeDuration: 0
            keyNavigationWraps: true

            highlight: Rectangle {
                radius: 6
                color: Theme.surface
            }

            delegate: Item {
                id: row
                required property int index
                required property int notificationId
                required property string app
                required property string icon
                required property string title
                required property string body
                required property var time

                readonly property string when: Qt.formatDateTime(time, "yyyy-MM-dd") === Qt.formatDateTime(new Date(), "yyyy-MM-dd")
                    ? Qt.formatTime(time, "HH:mm") : Qt.formatDateTime(time, "d MMM HH:mm")

                width: list.width
                height: text.implicitHeight + 20

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: list.currentIndex = row.index
                }

                Item {
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.top: parent.top
                    anchors.topMargin: 12
                    width: 24
                    height: 24
                    Image {
                        anchors.fill: parent
                        visible: row.icon !== ""
                        source: row.icon
                        sourceSize: Qt.size(24, 24)
                        smooth: true
                    }
                    Icon {
                        anchors.centerIn: parent
                        visible: row.icon === ""
                        glyph: "" // Ringer
                        color: Theme.accent
                    }
                }

                Column {
                    id: text
                    anchors.left: parent.left
                    anchors.leftMargin: 48
                    anchors.right: parent.right
                    anchors.rightMargin: 40
                    anchors.top: parent.top
                    anchors.topMargin: 10
                    spacing: 2

                    Label {
                        width: parent.width
                        text: row.app + "  ·  " + row.when
                        color: Theme.subtext
                        font.pixelSize: Theme.fontSize - 1
                    }
                    Label {
                        width: parent.width
                        visible: text !== ""
                        text: row.title
                        font.weight: Font.DemiBold
                    }
                    Label {
                        width: parent.width
                        visible: text !== ""
                        text: row.body
                        color: Theme.subtext
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                    }
                }

                Rectangle {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 8
                    width: 24
                    height: 24
                    radius: 6
                    color: dismissMouse.containsMouse ? Theme.hover : "transparent"

                    Icon {
                        anchors.centerIn: parent
                        glyph: "" // ChromeClose
                        color: Theme.subtext
                        font.pixelSize: 10
                    }
                    MouseArea {
                        id: dismissMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: Notifications.dismiss(row.notificationId)
                    }
                }
            }
        }
    }
}
