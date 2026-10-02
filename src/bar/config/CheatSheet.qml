import QtQuick
import Visor

// The key-binding cheat sheet (Super+K, as in Omarchy): every `bindd` in
// wm.conf with its description, in the groups the file puts them in, plus
// the keys that aren't visor-wm's. Esc or the same key closes it.
PopupWindow {
    id: sheet

    readonly property int columnWidth: 300
    readonly property int rowHeight: 26

    // Keys that aren't in wm.conf: Windows' own, and visor-shell's.
    readonly property var fixedGroups: [
        [
            { label: "Win + L", description: "Lock" },
            { label: "Ctrl + Shift + Esc", description: "Task Manager" },
            { label: "Ctrl + Alt + Del", description: "Security options" },
        ],
        [
            { label: "Ctrl + Alt + R", description: "Run" },
            { label: "Ctrl + Alt + T", description: "Terminal" },
            { label: "Ctrl + Alt + E", description: "File manager" },
            { label: "Ctrl + Alt + Q", description: "Quit to Explorer" },
        ],
    ]

    width: 3 * columnWidth + 2 * 24
    height: flow.implicitHeight + title.height + 48
    color: Theme.popupBackground
    placement: PopupWindow.Center

    onOpened: keys.forceActiveFocus()

    Item {
        id: keys
        anchors.fill: parent
        focus: true

        Label {
            id: title
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: 24
            anchors.topMargin: 18
            text: KeyBindings.available ? "Key bindings" : "Key bindings (visor-wm isn't running)"
            font.pixelSize: Theme.fontSize + 4
            font.weight: Font.DemiBold
            height: 30
        }

        Flow {
            id: flow
            anchors.top: title.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 24
            anchors.topMargin: 8
            spacing: 0

            Repeater {
                // visor-wm's groups, then the fixed ones.
                model: KeyBindings.groupCount + sheet.fixedGroups.length
                delegate: Column {
                    id: group
                    required property int index
                    readonly property var bindings: index < KeyBindings.groupCount
                        ? KeyBindings.group(index)
                        : sheet.fixedGroups[index - KeyBindings.groupCount]
                    width: sheet.columnWidth
                    bottomPadding: 16

                    Repeater {
                        model: group.bindings
                        delegate: Item {
                            required property var modelData
                            width: sheet.columnWidth
                            height: sheet.rowHeight

                            Label {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: 150
                                text: modelData.label
                                color: Theme.accent
                                font.weight: Font.DemiBold
                            }
                            Label {
                                anchors.left: parent.left
                                anchors.leftMargin: 150
                                anchors.right: parent.right
                                anchors.rightMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.description
                            }
                        }
                    }
                }
            }
        }
    }
}
