import QtQuick
import Visor

// The theme picker: Windows' themes (wallpaper, dark/light mode, accent
// colour), as Settings > Personalization > Themes lists them, plus the ones
// Visor ships. Up/Down pick, Enter or a click applies, Esc closes. Applying
// does what Settings does, and everything that follows Windows follows: the
// bar, window frames, apps, and, for Visor's themes, the Terminal scheme.
// Opened from the Win+X menu or `visor, theme` in wm.conf;
// Super+Ctrl+Shift+Space cycles themes without opening it. Under Explorer
// the last row opens Settings itself.
PopupWindow {
    id: picker

    readonly property int rowHeight: 48
    readonly property int maxRows: 9
    readonly property bool settingsAvailable: !Shell.replacingExplorer

    width: 400
    height: title.height + Math.min(list.contentHeight, maxRows * rowHeight) + (settingsRow.visible ? settingsRow.height : 0) + 30
    color: Theme.popupBackground
    placement: PopupWindow.Below

    onOpened: {
        list.currentIndex = Math.max(0, Themes.indexOf(Themes.current))
        keys.forceActiveFocus()
    }

    function pick(index) {
        const item = list.itemAtIndex(index)
        close()
        if (item)
            Themes.apply(item.key)
    }

    Item {
        id: keys
        anchors.fill: parent
        focus: true
        Keys.onUpPressed: list.decrementCurrentIndex()
        Keys.onDownPressed: list.incrementCurrentIndex()
        Keys.onReturnPressed: picker.pick(list.currentIndex)
        Keys.onEnterPressed: picker.pick(list.currentIndex)

        Label {
            id: title
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 18
            anchors.topMargin: 14
            height: 24
            text: Themes.count > 0 ? "Themes" : "No themes found"
            font.pixelSize: Theme.fontSize + 2
            font.weight: Font.DemiBold
        }

        ListView {
            id: list
            anchors.top: title.bottom
            anchors.bottom: settingsRow.visible ? settingsRow.top : parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 6
            anchors.topMargin: 4
            clip: true
            model: Themes
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
                required property string key
                required property string name
                required property bool light
                required property color accent
                required property string wallpaper
                required property bool current
                required property string source

                width: list.width
                height: picker.rowHeight

                // Swatch: the wallpaper, with the accent in the corner, as
                // Settings previews a theme.
                Rectangle {
                    id: swatch
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    width: 56
                    height: 32
                    radius: 5
                    color: row.light ? "#f3f3f3" : "#202020"
                    border.width: 1
                    border.color: Theme.surface
                    clip: true

                    Image {
                        anchors.fill: parent
                        anchors.margins: 1
                        visible: row.wallpaper !== ""
                        source: row.wallpaper !== "" ? "file:///" + row.wallpaper : ""
                        sourceSize: Qt.size(112, 64)
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        smooth: true
                    }
                    Rectangle {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 4
                        width: 10
                        height: 10
                        radius: 5
                        color: row.accent
                        border.width: 1
                        border.color: row.light ? "#80000000" : "#80ffffff"
                    }
                }

                Label {
                    anchors.left: swatch.right
                    anchors.leftMargin: 12
                    anchors.right: tag.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.name
                    font.pixelSize: Theme.fontSize + 1
                    font.weight: row.current ? Font.DemiBold : Font.Normal
                }

                Label {
                    id: tag
                    anchors.right: parent.right
                    anchors.rightMargin: 14 + 24 // room for the check mark
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.source
                    color: Theme.subtext
                }

                Icon {
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    visible: row.current
                    glyph: "" // CheckMark
                    color: Theme.accent
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onPositionChanged: list.currentIndex = row.index
                    onClicked: picker.pick(row.index)
                }
            }
        }

        // Where Windows keeps the rest (colours, backgrounds, saving a
        // theme): Settings, when Explorer is around to run it.
        Rectangle {
            id: settingsRow
            visible: picker.settingsAvailable
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 6
            height: 36
            radius: 6
            color: settingsMouse.containsMouse ? Theme.surface : "transparent"

            Icon {
                id: settingsGlyph
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                glyph: "" // Settings
                color: Theme.subtext
            }
            Label {
                anchors.left: settingsGlyph.right
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: "Personalization settings…"
                color: Theme.subtext
            }
            MouseArea {
                id: settingsMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    picker.close()
                    Shell.run("explorer.exe ms-settings:themes")
                }
            }
        }
    }
}
