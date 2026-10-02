import QtQuick
import Visor

// The window switcher (Alt+Tab), as Windows 11 draws it: a panel in the
// middle of the monitor you're working on with a live preview of each
// window on this desktop, most recently used first, the chosen one framed
// in the accent colour. Alt+Tab steps forward and Alt+Shift+Tab back (the
// bindings in wm.conf; visor-wm takes the keys, so Windows doesn't switch
// windows underneath), the arrows move too, and releasing Alt, Enter or a
// click goes to the chosen window. Esc leaves things as they were.
PopupWindow {
    id: switcher

    readonly property int thumbWidth: 220
    readonly property int thumbHeight: 124
    readonly property int captionHeight: 28
    readonly property int cellSpacing: 12
    readonly property int perRow: 5
    readonly property int padding: 20

    // The windows, front to back when the switcher opened:
    // {hwnd, title, appName, icon}.
    property var windows: []
    property int current: 0
    readonly property int columns: Math.max(1, Math.min(windows.length, perRow))
    readonly property int rows: Math.max(1, Math.ceil(windows.length / perRow))

    width: columns * (thumbWidth + cellSpacing) - cellSpacing + 2 * padding
    height: rows * (thumbHeight + captionHeight + cellSpacing) - cellSpacing + 2 * padding
    color: Theme.popupBackground
    placement: PopupWindow.Center

    // One step forward (+1) or back (-1); opens the switcher first if need
    // be, so the first Alt+Tab lands on the previous window, as in Windows.
    function step(delta) {
        if (!visible) {
            windows = Tasks.zOrder()
            if (windows.length === 0)
                return
            current = 0
            open()
        }
        current = (current + delta + windows.length) % windows.length
    }

    function pick() {
        const chosen = windows[current]
        close()
        if (chosen !== undefined)
            Tasks.bringToFront(chosen.hwnd)
    }

    onOpened: keys.forceActiveFocus()

    Item {
        id: keys
        anchors.fill: parent
        focus: true
        // Alt coming up picks, like Windows. Tab here means the hook didn't
        // take it (an elevated app had focus); it still steps.
        Keys.onReleased: event => {
            if (event.key === Qt.Key_Alt)
                switcher.pick()
        }
        Keys.onPressed: event => {
            switch (event.key) {
            case Qt.Key_Tab: switcher.step(1); break
            case Qt.Key_Backtab: switcher.step(-1); break
            case Qt.Key_Right: switcher.step(1); break
            case Qt.Key_Left: switcher.step(-1); break
            case Qt.Key_Down: switcher.step(switcher.perRow); break
            case Qt.Key_Up: switcher.step(-switcher.perRow); break
            case Qt.Key_Return:
            case Qt.Key_Enter: switcher.pick(); break
            default: return
            }
            event.accepted = true
        }

        Grid {
            anchors.fill: parent
            anchors.margins: switcher.padding
            columns: switcher.columns
            spacing: switcher.cellSpacing

            Repeater {
                model: switcher.windows
                delegate: Item {
                    id: cell
                    required property int index
                    required property var modelData
                    readonly property bool selected: index === switcher.current

                    width: switcher.thumbWidth
                    height: switcher.thumbHeight + switcher.captionHeight

                    // Icon and title above the preview, as Windows 11 has them.
                    Row {
                        id: caption
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        height: switcher.captionHeight
                        spacing: 8

                        Image {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 16
                            height: 16
                            sourceSize: Qt.size(16, 16)
                            source: cell.modelData.icon
                            smooth: true
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 24
                            text: cell.modelData.title !== "" ? cell.modelData.title : cell.modelData.appName
                            font.weight: cell.selected ? Font.DemiBold : Font.Normal
                        }
                    }

                    Rectangle {
                        id: frame
                        anchors.top: caption.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        radius: 6
                        color: Theme.surface
                        border.width: 2
                        border.color: cell.selected ? Theme.accent : "transparent"

                        WindowThumbnail {
                            anchors.fill: parent
                            anchors.margins: 6
                            hwnd: cell.modelData.hwnd
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: switcher.current = cell.index
                        onClicked: {
                            switcher.current = cell.index
                            switcher.pick()
                        }
                    }
                }
            }
        }
    }
}
