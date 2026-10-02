#pragma once

#include "services/shelllink.h"

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

// The desktops (Windows-style virtual desktops) run by visor-wm, as a list
// model. Empty when visor-wm isn't running (`available` is false).
//
//   Repeater {
//       model: Workspaces
//       delegate: Text {
//           required property int index
//           required property bool active
//           text: index + 1
//           font.bold: active
//           MouseArea { anchors.fill: parent; onClicked: Workspaces.activate(parent.index) }
//       }
//   }
//
// Roles: name ("Desktop 1"), active, windows (how many app windows it has).
class Workspaces : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    // Index of the desktop on screen.
    Q_PROPERTY(int active READ active NOTIFY changed)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        ActiveRole,
        WindowsRole,
    };

    explicit Workspaces(QObject *parent = nullptr);

    bool available() const { return !m_workspaces.isEmpty(); }
    int count() const { return int(m_workspaces.size()); }
    int active() const { return m_active; }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Switches to desktop `index` (like Win+Ctrl+arrows, or picking it in
    // Task View).
    Q_INVOKABLE void activate(int index);
    Q_INVOKABLE void next() { activate(m_active + 1); }
    Q_INVOKABLE void previous() { activate(m_active - 1); }

signals:
    void changed();

private:
    void reset();

    QList<ShellLink::Workspace> m_workspaces;
    int m_active = 0;
};
