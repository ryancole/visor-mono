import QtQuick
import Visor

// The system menu, like the "Shut down or sign out" part of Windows' Win+X
// menu: lock, sign out, sleep, restart, shut down, and, with visor-shell,
// quitting (to Explorer in replace mode; in hosted mode Explorer is there
// already, so it is just Visor and visor-shell that go); plus the Windows
// theme picker (Omarchy's menu has one too; Windows keeps it in Settings).
// Opens under the bar's launcher button
// (right-click it) or with Win+X. Up/Down and Enter, or click; Esc closes.
PopupWindow {
    id: menu

    readonly property int rowHeight: 36
    property int current: 0
    // Set by shell.qml: the picker the "Theme" row opens.
    property PopupWindow themePicker

    // {glyph, text, action}; glyphs are Segoe Fluent Icons.
    readonly property var actions: {
        const list = [
            { glyph: "", text: "Themes", action: () => { if (menu.themePicker) menu.themePicker.open() } },
            { glyph: "", text: "Lock", action: () => Shell.lock() },
            { glyph: "", text: "Sign out", action: () => Shell.signOut() },
            { glyph: "", text: "Sleep", action: () => Shell.sleep() },
            { glyph: "", text: "Restart", action: () => Shell.restart() },
            { glyph: "", text: "Shut down", action: () => Shell.shutDown() },
        ]
        if (Shell.available) {
            list.push({ glyph: "\uEC50", text: Shell.replacingExplorer ? "Quit to Explorer" : "Quit Visor",
                        action: () => Shell.quitToExplorer() })
        }
        return list
    }

    width: 220
    height: actions.length * rowHeight + 12
    color: Theme.popupBackground
    placement: PopupWindow.BelowLeft

    onOpened: {
        current = 0
        keys.forceActiveFocus()
    }

    function run(index) {
        const action = actions[index].action
        close()
        action()
    }

    Item {
        id: keys
        anchors.fill: parent
        focus: true
        Keys.onUpPressed: menu.current = (menu.current + menu.actions.length - 1) % menu.actions.length
        Keys.onDownPressed: menu.current = (menu.current + 1) % menu.actions.length
        Keys.onReturnPressed: menu.run(menu.current)
        Keys.onEnterPressed: menu.run(menu.current)

        Column {
            anchors.fill: parent
            anchors.margins: 6

            Repeater {
                model: menu.actions
                delegate: Rectangle {
                    id: row
                    required property int index
                    required property var modelData

                    width: parent.width
                    height: menu.rowHeight
                    radius: 6
                    color: menu.current === index ? Theme.surface : "transparent"

                    Icon {
                        id: glyph
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: row.modelData.glyph
                        font.pixelSize: 16
                        color: menu.current === row.index ? Theme.accent : Theme.text
                    }

                    Label {
                        anchors.left: glyph.right
                        anchors.leftMargin: 12
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData.text
                        font.pixelSize: Theme.fontSize + 1
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onPositionChanged: menu.current = row.index
                        onClicked: menu.run(row.index)
                    }
                }
            }
        }
    }
}
