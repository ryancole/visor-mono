import QtQuick
import Visor

// Quick Settings, as Windows 11's: what opens from the taskbar's network,
// volume and battery icons, or Win+A. Buttons for the Wi-Fi and Bluetooth
// radios (only the ones the machine has), battery saver as a status, then
// the brightness slider (laptops) and the volume slider with the output
// device's name. Under Explorer the last row opens Settings for the rest;
// in replace mode Settings can't run, so there is no row.
// Night light has no supported API, so it isn't here.
PopupWindow {
    id: panel

    readonly property bool settingsAvailable: !Shell.replacingExplorer

    width: 360
    height: content.implicitHeight + 36
    color: Theme.popupBackground
    placement: PopupWindow.BelowRight

    onOpened: keys.forceActiveFocus()

    // A Quick Settings button: accent when on.
    component Tile: Rectangle {
        id: tile
        property string glyph
        property string label
        property bool on: false
        property bool clickable: true
        signal clicked()

        width: 96
        height: 56
        radius: 6
        color: on ? Theme.accent : tileMouse.containsMouse && clickable ? Theme.hover : Theme.surface

        Column {
            anchors.centerIn: parent
            spacing: 4
            Icon {
                anchors.horizontalCenter: parent.horizontalCenter
                glyph: tile.glyph
                color: tile.on ? Theme.background : Theme.text
                font.pixelSize: 16
            }
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(implicitWidth, tile.width - 12)
                horizontalAlignment: Text.AlignHCenter
                text: tile.label
                color: tile.on ? Theme.background : Theme.text
                font.pixelSize: Theme.fontSize - 1
            }
        }

        MouseArea {
            id: tileMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: tile.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (tile.clickable) tile.clicked()
        }
    }

    // A slider row, as Windows' brightness and volume sliders: icon, track,
    // number. Click or drag the track, or scroll over the row.
    component LevelSlider: Item {
        id: slider
        property string glyph
        property real value: 0 // 0 - 1
        property bool dimmed: false
        signal moved(real level)
        signal glyphClicked()

        width: parent.width
        height: 32

        function set(x) {
            moved(Math.min(1, Math.max(0, x / trackArea.width)))
        }

        Icon {
            id: icon
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            glyph: slider.glyph
            color: slider.dimmed ? Theme.subtext : Theme.text
            font.pixelSize: 16

            MouseArea {
                anchors.fill: parent
                anchors.margins: -6
                cursorShape: Qt.PointingHandCursor
                onClicked: slider.glyphClicked()
            }
        }

        Item {
            id: trackArea
            anchors.left: icon.right
            anchors.leftMargin: 16
            anchors.right: number.left
            anchors.rightMargin: 16
            anchors.top: parent.top
            anchors.bottom: parent.bottom

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: 4
                radius: 2
                color: Theme.surface

                Rectangle {
                    width: parent.width * slider.value
                    height: parent.height
                    radius: parent.radius
                    color: slider.dimmed ? Theme.subtext : Theme.accent
                }
            }

            Rectangle {
                x: parent.width * slider.value - width / 2
                anchors.verticalCenter: parent.verticalCenter
                width: 16
                height: 16
                radius: 8
                color: Theme.popupBackground
                border.width: 4
                border.color: slider.dimmed ? Theme.subtext : Theme.accent
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onPressed: mouse => slider.set(mouse.x)
                onPositionChanged: mouse => { if (pressed) slider.set(mouse.x) }
            }
        }

        Label {
            id: number
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: 32
            horizontalAlignment: Text.AlignRight
            text: Math.round(slider.value * 100)
            color: slider.dimmed ? Theme.subtext : Theme.text
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            onWheel: wheel => slider.moved(Math.min(1, Math.max(0, slider.value + (wheel.angleDelta.y > 0 ? 0.02 : -0.02))))
        }
    }

    Item {
        id: keys
        anchors.fill: parent
        focus: true

        Column {
            id: content
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 18
            spacing: 16

            Row {
                spacing: 8

                Tile {
                    visible: Radios.wifiAvailable
                    glyph: ""
                    label: on && Network.kind === "wifi" && Network.name !== "" ? Network.name : "Wi-Fi"
                    on: Radios.wifiOn
                    onClicked: Radios.wifiOn = !Radios.wifiOn
                }
                Tile {
                    visible: Radios.bluetoothAvailable
                    glyph: ""
                    label: "Bluetooth"
                    on: Radios.bluetoothOn
                    onClicked: Radios.bluetoothOn = !Radios.bluetoothOn
                }
                Tile {
                    visible: Battery.present
                    glyph: ""
                    label: "Battery saver"
                    on: Battery.saver
                    clickable: false // Windows has no API for the switch
                }
                Tile {
                    visible: !Radios.wifiAvailable && !Radios.bluetoothAvailable && !Battery.present
                    glyph: Network.kind === "ethernet" ? "" : ""
                    label: Network.connected ? Network.name : "No internet"
                    clickable: false
                }
            }

            LevelSlider {
                visible: Brightness.available
                glyph: ""
                value: Brightness.level
                onMoved: level => Brightness.level = level
            }

            LevelSlider {
                glyph: !Audio.available || Audio.muted ? ""
                     : Audio.volume < 0.01 ? ""
                     : Audio.volume < 0.34 ? ""
                     : Audio.volume < 0.67 ? "" : ""
                value: Audio.available ? Audio.volume : 0
                dimmed: !Audio.available || Audio.muted
                onMoved: level => { Audio.muted = false; Audio.volume = level }
                onGlyphClicked: Audio.toggleMute()
            }

            // Where the sound goes, and how the machine is connected and powered.
            Column {
                width: parent.width
                spacing: 4
                Label {
                    width: parent.width
                    text: Audio.available ? Audio.deviceName : "No output device"
                    color: Theme.subtext
                }
                Label {
                    width: parent.width
                    text: (Network.kind === "none" ? "Not connected"
                           : (Network.connected ? "" : "No internet: ") + Network.name + (Network.metered ? " (metered)" : ""))
                          + (Battery.present ? "   ·   Battery " + Battery.percent + "%"
                             + (Battery.charging ? ", charging" : Battery.pluggedIn ? ", plugged in" : "") : "")
                    color: Theme.subtext
                }
            }

            // Windows keeps the rest in Settings.
            Label {
                visible: panel.settingsAvailable
                text: "All settings"
                color: settingsMouse.containsMouse ? Theme.text : Theme.accent

                MouseArea {
                    id: settingsMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        panel.close()
                        Shell.run("explorer.exe ms-settings:")
                    }
                }
            }
        }
    }
}
