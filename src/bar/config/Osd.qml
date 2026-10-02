import QtQuick
import Visor

// The on-screen display for volume and brightness: Windows 11's pill (icon,
// level bar, number) at the bottom centre of the monitor you're working on,
// gone again two seconds later. shell.qml shows it for the `visor volume`
// and `visor brightness` commands; it also shows itself when the brightness
// changes, since brightness keys are handled by the OS itself. It never
// takes focus, and clicks go through it.
OsdWindow {
    id: osd

    property string glyph: ""
    property real level: 0
    property bool dimmed: false  // muted, or no device
    property string caption: ""  // shown instead of the number

    width: 300
    height: 64
    placement: OsdWindow.Bottom
    margin: 32
    timeout: 2000
    clickThrough: true

    function showVolume() {
        dimmed = !Audio.available || Audio.muted
        level = Audio.available ? Audio.volume : 0
        caption = Audio.available ? "" : "No output device"
        glyph = !Audio.available || Audio.muted ? ""  // Mute
              : Audio.volume < 0.01 ? ""
              : Audio.volume < 0.34 ? ""
              : Audio.volume < 0.67 ? "" : ""
        open()
    }

    function showBrightness() {
        dimmed = !Brightness.available
        level = Brightness.level
        caption = Brightness.available ? "" : "No brightness control"
        glyph = "" // Brightness
        open()
    }

    // Keys the OS handles itself still deserve a display.
    Connections {
        target: Brightness
        function onLevelChanged() { osd.showBrightness() }
    }

    // Fade out, then release the window.
    onShownChanged: if (!shown) closeLater.restart()
    Timer {
        id: closeLater
        interval: 200
        onTriggered: if (!osd.shown) osd.close()
    }

    Rectangle {
        id: pill
        anchors.fill: parent
        radius: 8
        color: Theme.popupBackground
        opacity: osd.shown ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 150 } }

        Icon {
            id: icon
            anchors.left: parent.left
            anchors.leftMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            glyph: osd.glyph
            color: osd.dimmed ? Theme.subtext : Theme.text
            font.pixelSize: 20
        }

        // The level, as Windows' slider shows it.
        Rectangle {
            id: track
            anchors.left: icon.right
            anchors.leftMargin: 18
            anchors.right: number.left
            anchors.rightMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            height: 4
            radius: 2
            color: Theme.surface

            Rectangle {
                width: parent.width * osd.level
                height: parent.height
                radius: parent.radius
                color: osd.dimmed ? Theme.subtext : Theme.accent
                Behavior on width { NumberAnimation { duration: 80 } }
            }
        }

        Label {
            id: number
            anchors.right: parent.right
            anchors.rightMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            width: osd.caption !== "" ? implicitWidth : 32
            horizontalAlignment: Text.AlignRight
            text: osd.caption !== "" ? osd.caption : Math.round(osd.level * 100)
            color: osd.dimmed ? Theme.subtext : Theme.text
            font.pixelSize: Theme.fontSize + 2
            font.weight: Font.DemiBold
        }
    }
}
