import QtQuick
import Visor

// The app launcher (Start, in Windows' terms; Walker in Omarchy's): a search
// box under the bar, then what's on the desktop as a grid of icons (where
// Start has its pinned apps), then every app A-Z. Opened by a bare Win press
// or Win+S (wm.conf), or the bar's button.
//
// Type to search (fuzzy: "wt" finds Windows Terminal): All apps becomes the
// matching apps, and only it scrolls; the desktop stays as it is. Arrow keys
// pick, Tab goes between the desktop and the list, Enter opens,
// Ctrl+Shift+Enter opens as administrator, Esc closes. If nothing matches,
// Enter runs what you typed as a command (notepad, a path, a URL).
//
// In replace mode UWP apps (Settings, Calculator) can't show a window; they
// are listed dimmed, after everything else.
PopupWindow {
    id: launcher

    readonly property int rowHeight: 40
    readonly property int pageRows: 8
    readonly property int columns: 8
    readonly property int tileHeight: 88
    // The selection: a desktop tile, or (-1) the list's current row.
    property int tile: -1

    width: 800
    height: 680
    color: Theme.popupBackground
    placement: PopupWindow.Below

    onOpened: {
        input.text = ""
        Apps.query = ""
        selectFirst()
        input.forceActiveFocus()
    }

    // The first desktop tile, or the best match once something is typed.
    function selectFirst() {
        if (DesktopItems.count > 0 && Apps.query === "")
            selectTile(0)
        else
            selectRow(0)
    }

    function selectTile(i) {
        tile = Math.max(0, Math.min(i, DesktopItems.count - 1))
        list.currentIndex = -1
    }

    function selectRow(i) {
        tile = -1
        list.currentIndex = Math.max(0, Math.min(i, Apps.count - 1))
    }

    function down() {
        if (tile >= 0) {
            if (tile + columns < DesktopItems.count)
                selectTile(tile + columns)
            else if (Apps.count > 0)
                selectRow(0)
        } else if (list.currentIndex < Apps.count - 1) {
            selectRow(list.currentIndex + 1)
        } else if (DesktopItems.count > 0) {
            selectTile(0) // wrap
        } else {
            selectRow(0)
        }
    }

    function up() {
        if (tile >= columns)
            selectTile(tile - columns)
        else if (tile < 0 && list.currentIndex > 0)
            selectRow(list.currentIndex - 1)
        else if (tile < 0 && DesktopItems.count > 0)
            selectTile(Math.floor((DesktopItems.count - 1) / columns) * columns)
        else
            selectRow(Apps.count - 1) // wrap
    }

    function activate(modifiers) {
        const admin = (modifiers & Qt.ControlModifier) && (modifiers & Qt.ShiftModifier)
        if (tile >= 0)
            DesktopItems.launch(tile, admin)
        else if (Apps.count > 0)
            Apps.launch(list.currentIndex, admin)
        else if (input.text.trim() !== "")
            Shell.run(input.text)
        close()
    }

    component Heading: Item {
        property alias text: label.text
        width: parent.width
        height: 32
        Label {
            id: label
            anchors.left: parent.left
            anchors.leftMargin: 20
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 6
            color: Theme.subtext
            font.pixelSize: Theme.fontSize - 1
            font.weight: Font.DemiBold
        }
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
                launcher.selectFirst()
            }
            Keys.onUpPressed: launcher.up()
            Keys.onDownPressed: launcher.down()
            Keys.onReturnPressed: event => launcher.activate(event.modifiers)
            Keys.onEnterPressed: event => launcher.activate(event.modifiers)
            Keys.onPressed: event => {
                const inGrid = launcher.tile >= 0
                if (event.key === Qt.Key_Left && inGrid) {
                    if (launcher.tile > 0)
                        launcher.selectTile(launcher.tile - 1)
                } else if (event.key === Qt.Key_Right && inGrid) {
                    if (launcher.tile < DesktopItems.count - 1)
                        launcher.selectTile(launcher.tile + 1)
                } else if ((event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) && DesktopItems.count > 0) {
                    if (inGrid)
                        launcher.selectRow(0)
                    else
                        launcher.selectTile(0)
                } else if (event.key === Qt.Key_PageUp) {
                    if (!inGrid)
                        launcher.selectRow(list.currentIndex - launcher.pageRows)
                } else if (event.key === Qt.Key_PageDown) {
                    launcher.selectRow(inGrid ? 0 : list.currentIndex + launcher.pageRows)
                } else {
                    return
                }
                event.accepted = true
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

    // The desktop: always shown, whatever is typed.
    Column {
        id: desktop
        anchors.top: search.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        visible: DesktopItems.count > 0
        height: visible ? implicitHeight : 0

        Heading {
            text: "Desktop"
        }

        Grid {
            columns: launcher.columns

            Repeater {
                model: DesktopItems

                delegate: Item {
                    id: tile
                    required property int index
                    required property string name
                    required property url icon

                    width: desktop.width / launcher.columns
                    height: launcher.tileHeight

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 2
                        radius: 6
                        color: Theme.surface
                        visible: launcher.tile === tile.index
                    }

                    Image {
                        id: tileIcon
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 12
                        width: 32
                        height: 32
                        sourceSize: Qt.size(32, 32)
                        source: tile.icon
                        asynchronous: true
                        smooth: true
                    }

                    Label {
                        anchors.top: tileIcon.bottom
                        anchors.topMargin: 6
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignTop
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        text: tile.name
                        font.pixelSize: Theme.fontSize - 1
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onPositionChanged: launcher.selectTile(tile.index)
                        onClicked: {
                            launcher.selectTile(tile.index)
                            launcher.activate(0)
                        }
                    }
                }
            }
        }
    }

    Heading {
        id: allApps
        anchors.top: desktop.bottom
        text: "All apps"
    }

    // Every app, or the matches; the only part that scrolls.
    ListView {
        id: list
        anchors.top: allApps.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 6
        anchors.topMargin: 2
        clip: true
        model: Apps
        currentIndex: 0
        highlightMoveDuration: 0
        highlightResizeDuration: 0

        highlight: Rectangle {
            radius: 6
            color: Theme.surface
            visible: launcher.tile < 0
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
                onPositionChanged: launcher.selectRow(row.index)
                onClicked: {
                    launcher.selectRow(row.index)
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
