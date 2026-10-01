#pragma once

#include "services/shelllink.h"

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

// Notification-area icons (Shell_NotifyIcon), hosted by visor-shell. Empty
// when visor runs without it (`available` is false); under Explorer the
// taskbar shows them instead. Icons an app has hidden are left out.
//
//   Repeater {
//       model: SystemTray
//       delegate: Image {
//           required property int iconId
//           required property url icon
//           source: icon
//           MouseArea {
//               anchors.fill: parent
//               acceptedButtons: Qt.LeftButton | Qt.RightButton
//               onClicked: e => SystemTray.click(parent.iconId, e.button === Qt.RightButton ? "right" : "left")
//           }
//       }
//   }
//
// Roles: iconId, tooltip, icon (an image:// URL), processId.
class SystemTray : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TooltipRole,
        IconRole,
        ProcessIdRole,
    };

    explicit SystemTray(QObject *parent = nullptr);

    bool available() const;
    int count() const { return int(m_icons.size()); }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Forwards a click at the current cursor position to the icon's app, as
    // the taskbar would. button: "left", "right", "middle" or "double".
    Q_INVOKABLE void click(int iconId, const QString &button = QStringLiteral("left"));

signals:
    void availableChanged();
    void countChanged();

private:
    int indexOf(int id) const;
    void reset();
    void update(const ShellLink::TrayIcon &icon);
    void remove(int id);

    QList<ShellLink::TrayIcon> m_icons; // visible icons only
};
