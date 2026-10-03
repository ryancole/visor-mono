import QtQuick
import Visor

PanelWindow {
    id: bar

    // The pop-ups the bar's buttons open (set by shell.qml).
    property PopupWindow launcherPopup
    property PopupWindow menuPopup
    property PopupWindow notificationsPopup
    property PopupWindow quickSettingsPopup

    edge: PanelWindow.Top
    thickness: Theme.barHeight
    color: Theme.background

    SystemClock {
        id: clock
        precision: SystemClock.Minutes
    }

    // Left: desktops and open windows (with visor-shell / visor-wm).
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
                glyph: "\uE71D" // AllApps
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
    }

    // Centre: the clock, with the status indicators Windows' taskbar has,
    // each only when it has something to say: the microphone, camera and
    // location in use (Windows' privacy indicators), the keyboard layout
    // (only with more than one installed, as Windows shows it), and Windows
    // Update's "restart required".
    Row {
        id: center
        anchors.centerIn: parent
        spacing: 14

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Icon { visible: Privacy.microphone; glyph: "\uE720"; color: Theme.accent }
            Icon { visible: Privacy.camera; glyph: "\uE714"; color: Theme.accent }
            Icon { visible: Privacy.location; glyph: "\uE707"; color: Theme.accent }
        }

        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: Qt.formatDateTime(clock.date, "ddd d MMM   HH:mm")
            font.weight: Font.DemiBold
        }

        // Click: the next layout, as Win+Space.
        Rectangle {
            visible: InputLanguage.count > 1
            anchors.verticalCenter: parent.verticalCenter
            width: layoutLabel.implicitWidth + 12
            height: 22
            radius: 4
            color: layoutMouse.containsMouse ? Theme.surface : "transparent"

            Label {
                id: layoutLabel
                anchors.centerIn: parent
                text: InputLanguage.code
                font.pixelSize: Theme.fontSize - 1
            }

            MouseArea {
                id: layoutMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: InputLanguage.next()
            }
        }

        // Click: the menu (its Restart row); under Explorer, Windows Update.
        Icon {
            visible: Updates.restartRequired
            anchors.verticalCenter: parent.verticalCenter
            glyph: "\uE777"
            color: Theme.accent

            MouseArea {
                anchors.fill: parent
                anchors.margins: -4
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (Shell.replacingExplorer) {
                        if (bar.menuPopup)
                            bar.menuPopup.toggle(bar.screen)
                    } else {
                        Shell.run("explorer.exe ms-settings:windowsupdate")
                    }
                }
            }
        }
    }

    // Right: tray, volume and notifications. No now-playing here: Windows
    // shows media only in the volume flyout and on the lock screen, and the
    // `Media` type is there for a config that wants it anyway.
    Row {
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        spacing: 16

        TrayArea {
            anchors.verticalCenter: parent.verticalCenter
        }

        // Windows' cluster: network, battery (laptops) and volume. Click
        // opens Quick Settings, as on the taskbar (Win+A too); right-click
        // mutes, scrolling changes the volume.
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: cluster.implicitWidth + 16
            height: 24
            radius: 6
            color: clusterMouse.containsMouse ? Theme.surface : "transparent"

            Row {
                id: cluster
                anchors.centerIn: parent
                spacing: 10

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: !Network.connected && Network.kind !== "wifi" ? "\uF384"
                         : Network.kind === "ethernet" ? "\uE839"
                         : Network.kind === "cellular" ? ["\uE871", "\uE86C", "\uE86D", "\uE86E", "\uE86F", "\uE870"][Network.signal]
                         : Network.kind === "wifi" ? (!Network.connected ? "\uEB63"
                                                      : Network.signal <= 1 ? "\uE872"
                                                      : Network.signal === 2 ? "\uE873"
                                                      : Network.signal === 3 ? "\uE874" : "\uE701")
                         : "\uF384"
                }

                Row {
                    visible: Battery.present
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        // E850-E859 by tenth, E83F full; charging E85A-E863, E83E full.
                        glyph: {
                            const tenth = Math.min(10, Math.round(Battery.percent / 10))
                            if (tenth >= 10)
                                return Battery.charging ? "\uE83E" : "\uE83F"
                            return String.fromCharCode((Battery.charging ? 0xE85A : 0xE850) + tenth)
                        }
                    }
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: Battery.percent + "%"
                    }
                }

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    opacity: Audio.muted ? 0.5 : 1
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Audio.muted ? ""
                             : Audio.volume < 0.01 ? ""
                             : Audio.volume < 0.34 ? ""
                             : Audio.volume < 0.67 ? "" : ""
                    }
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: Math.round(Audio.volume * 100) + "%"
                    }
                }
            }

            MouseArea {
                id: clusterMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: mouse => {
                    if (mouse.button === Qt.RightButton)
                        Audio.toggleMute()
                    else if (bar.quickSettingsPopup)
                        bar.quickSettingsPopup.toggle(bar.screen)
                }
                onWheel: wheel => Audio.volume += wheel.angleDelta.y > 0 ? 0.02 : -0.02
            }
        }

        // Notifications: the bell, with how many came since you last looked;
        // a moon while Do not disturb is on, as Windows 11's taskbar shows.
        // Replace mode only; under Explorer the taskbar has its own.
        Item {
            visible: Notifications.available
            width: 24
            height: parent.height

            Icon {
                anchors.centerIn: parent
                glyph: Notifications.doNotDisturb ? "\uE708" : Notifications.unread > 0 ? "" : "" // RingerSolid / Ringer
                font.pixelSize: 15
            }

            Rectangle {
                visible: Notifications.unread > 0
                anchors.top: parent.top
                anchors.topMargin: 4
                anchors.right: parent.right
                width: 14
                height: 14
                radius: 7
                color: Theme.accent

                Label {
                    anchors.centerIn: parent
                    text: Math.min(Notifications.unread, 9)
                    color: Theme.background
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: if (bar.notificationsPopup) bar.notificationsPopup.toggle(bar.screen)
            }
        }
    }
}
