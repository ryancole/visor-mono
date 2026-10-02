import QtQuick
import Visor

PanelWindow {
    id: bar

    // The pop-ups the launcher button opens (set by shell.qml).
    property PopupWindow launcherPopup
    property PopupWindow menuPopup

    edge: PanelWindow.Top
    thickness: Theme.barHeight
    color: Theme.background

    SystemClock {
        id: clock
        precision: SystemClock.Minutes
    }

    // Left: desktops and open windows (with visor-shell / visor-wm), focused
    // app and window title.
    Row {
        id: left
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(0, center.x - x - 24)
        spacing: 8

        // Like Start: left-click for the launcher, right-click for the
        // power menu (Win+X).
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 28
            height: 24
            radius: 6
            color: launcherMouse.containsMouse ? Theme.surface : "transparent"

            Icon {
                anchors.centerIn: parent
                glyph: "" // AllApps
                color: Theme.accent
                font.pixelSize: 16
            }

            MouseArea {
                id: launcherMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: mouse => {
                    const popup = mouse.button === Qt.RightButton ? bar.menuPopup : bar.launcherPopup
                    if (popup)
                        popup.toggle(bar.screen)
                }
            }
        }

        Desktops {
            anchors.verticalCenter: parent.verticalCenter
        }

        TaskList {
            anchors.verticalCenter: parent.verticalCenter
        }

        Rectangle {
            visible: ActiveWindow.appName !== ""
            anchors.verticalCenter: parent.verticalCenter
            width: appLabel.implicitWidth + 16
            height: 20
            radius: 10
            color: Theme.surface

            Label {
                id: appLabel
                anchors.centerIn: parent
                text: ActiveWindow.appName
                color: Theme.accent
                font.weight: Font.DemiBold
            }
        }

        Label {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, left.width - x)
            text: ActiveWindow.title
            color: Theme.subtext
        }
    }

    // Center: clock.
    Label {
        id: center
        anchors.centerIn: parent
        text: Qt.formatDateTime(clock.date, "ddd d MMM   HH:mm")
        font.weight: Font.DemiBold
    }

    // Right: media and volume.
    Row {
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        spacing: 16

        TrayArea {
            anchors.verticalCenter: parent.verticalCenter
        }

        // Click: play/pause. Hidden when nothing is playing.
        Item {
            visible: Media.available && Media.title !== ""
            width: media.implicitWidth
            height: parent.height

            Row {
                id: media
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Media.playing ? "\uE769" : "\uE768" // Pause / Play
                    color: Theme.accent
                }
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, 280)
                    text: Media.artist !== "" ? Media.artist + " – " + Media.title : Media.title
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: mouse => mouse.button === Qt.RightButton ? Media.next() : Media.playPause()
            }
        }

        // Click: mute. Scroll: volume.
        Item {
            width: volume.implicitWidth
            height: parent.height
            opacity: Audio.muted ? 0.5 : 1

            Row {
                id: volume
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Audio.muted ? "\uE74F"
                         : Audio.volume < 0.01 ? "\uE992"
                         : Audio.volume < 0.34 ? "\uE993"
                         : Audio.volume < 0.67 ? "\uE994" : "\uE995"
                }
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: Math.round(Audio.volume * 100) + "%"
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: Audio.toggleMute()
                onWheel: wheel => Audio.volume += wheel.angleDelta.y > 0 ? 0.02 : -0.02
            }
        }
    }
}