#pragma once

#include "services/shelllink.h"

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

// The open app windows (what the taskbar would show), as a list model. Fed by
// visor-shell; empty when visor runs without it (`available` is false).
//
//   Repeater {
//       model: Tasks
//       delegate: Image {
//           required property var hwnd
//           required property url icon
//           required property bool active
//           source: icon
//           MouseArea { anchors.fill: parent; onClicked: Tasks.activate(parent.hwnd) }
//       }
//   }
//
// Roles: hwnd, title, appName, processPath, active, flashing, icon (an
// image:// URL for the window's icon).
class Tasks : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // True while connected to visor-shell.
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        HwndRole = Qt::UserRole + 1,
        TitleRole,
        AppNameRole,
        ProcessPathRole,
        ActiveRole,
        FlashingRole,
        IconRole,
    };

    explicit Tasks(QObject *parent = nullptr);

    bool available() const;
    int count() const { return int(m_tasks.size()); }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Taskbar-button behaviour: brings the window forward (restoring it if
    // minimised), or minimises it if it is already in front.
    Q_INVOKABLE void activate(double hwnd);
    Q_INVOKABLE void minimize(double hwnd);
    // Asks the window to close (WM_CLOSE), like the taskbar's "Close window".
    Q_INVOKABLE void close(double hwnd);

signals:
    void availableChanged();
    void countChanged();

private:
    int indexOf(quintptr hwnd) const;
    void reset();

    QList<ShellLink::Task> m_tasks;
    quintptr m_active = 0;
};
