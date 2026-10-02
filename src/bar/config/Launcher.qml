import QtQuick
import Visor

// The app launcher (Start, in Windows' terms; Walker in Omarchy's): a search
// box under the bar with the matching apps below it. Opened by a bare Win
// press or Win+S (wm.conf), or the bar's button.
//
// Type to search (fuzzy: "wt" finds Windows Terminal). Up/Down pick,
// Enter opens, Ctrl+Shift+Enter opens as administrator, Esc closes. With
// nothing typed, the apps you opened recently come first. If nothing
// matches, Enter runs what you typed as a command (notepad, a path, a URL).
//
// In replace mode UWP apps (Settings, Calculator) can't show a window; they
// are listed dimmed, after everything else.
PopupWindow {
    id: launcher

    readonly property int rowHeight: 40
    readonly property int maxRows: 8

    width: 640
    height: search.height + Math.min(list.contentHeight, maxRows * rowHeight + 2 * 24) + 12
    color: Theme.popupBackground
    placement: PopupWindow.Below

    onOpened: {
        input.text = ""
        Apps.query = ""
        list.currentIndex = 0
        input.forceActiveFocus()
    }

    function activate(modifiers) {
        if (Apps.count > 0) {
            const admin = (modifiers & Qt.ControlModifier) && (modifiers & Qt.ShiftModifier)
            Apps.launch(list.currentIndex, admin)
        } else if (input.text.trim() !== "") {
            Shell.run(input.text)
        }
        close()
    }

    // Search box.
    Item {
        id: search
        width: parent.width
        height: 56

        Icon {
            anchors.left: parent.left
            anchors.leftMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            glyph: "" // Search
            color: Theme.subtext
            font.pixelSize: 18
        }

        TextInput {
            id: input
            anchors.left: parent.left
            anchors.leftMargin: 52
            anchors.right: parent.right
            anchors.rightMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.text
            font.family: Theme.font
            font.pixelSize: 18
            selectionColor: Theme.accent
            selectedTextColor: Theme.background
            clip: true
            focus: true

            onTextChanged: {
                Apps.query = text
                list.currentIndex = 0
            }
            Keys.onUpPressed: list.decrementCurrentIndex()
            Keys.onDownPressed: list.incrementCurrentIndex()
            Keys.onReturnPressed: event => launcher.activate(event.modifiers)
            Keys.onEnterPressed: event => launcher.activate(event.modifiers)
            Keys.onPressed: event => {
                if (event.key === Qt.Key_PageUp) {
                    list.currentIndex = Math.max(0, list.currentIndex - launcher.maxRows)
                    event.accepted = true
                } else if (event.key === Qt.Key_PageDown) {
                    list.currentIndex = Math.min(Apps.count - 1, list.currentIndex + launcher.maxRows)
                    event.accepted = true
                }
            }

            Label {
                anchors.fill: parent
                visible: input.text === ""
                text: Apps.ready ? "Search apps" : "Indexing apps…"
                color: Theme.subtext
                font.pixelSize: 18
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            height: 1
            color: Theme.surface
        }
    }

    // Results.
    ListView {
        id: list
        anchors.top: search.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 6
        anchors.topMargin: 4
        clip: true
        model: Apps
        currentIndex: 0
        highlightMoveDuration: 0
        highlightResizeDuration: 0
        keyNavigationWraps: true

        // "Recent" and "All apps" headings, for an empty search.
        section.property: "recent"
        section.criteria: ViewSection.FullString
        section.delegate: Item {
            required property string section
            width: list.width
            visible: Apps.query === ""
            height: visible ? 24 : 0
            Label {
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                text: parent.section === "true" ? "Recent" : "All apps"
                color: Theme.subtext
                font.pixelSize: Theme.fontSize - 1
                font.weight: Font.DemiBold
            }
        }

        highlight: Rectangle {
            radius: 6
            color: Theme.surface
        }

        delegate: Item {
            id: row
            required property int index
            required property string name
            required property url icon
            required property bool launchable
            required property bool packaged

            width: list.width
            height: launcher.rowHeight
            opacity: launchable ? 1 : 0.45

            Image {
                id: appIcon
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                width: 24
                height: 24
                sourceSize: Qt.size(24, 24)
                source: row.icon
                asynchronous: true
                smooth: true
            }

            Label {
                anchors.left: appIcon.right
                anchors.leftMargin: 12
                anchors.right: kind.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: row.name
                font.pixelSize: Theme.fontSize + 1
            }

            Label {
                id: kind
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                text: !row.launchable ? "Needs Explorer" : ""
                color: Theme.subtext
            }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onPositionChanged: list.currentIndex = row.index
                onClicked: {
                    list.currentIndex = row.index
                    launcher.activate(0)
                }
            }
        }

        // Nothing matched: Enter runs the text as a command.
        Label {
            anchors.centerIn: parent
            visible: Apps.count === 0 && Apps.ready
            text: input.text.trim() === "" ? "No apps" : "Press Enter to run \"" + input.text.trim() + "\""
            color: Theme.subtext
        }
    }
}
