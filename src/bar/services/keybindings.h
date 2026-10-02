#pragma once

#include "services/shelllink.h"

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

// visor-wm's key bindings (the `bind` lines in wm.conf), as a list model,
// for a cheat sheet. Empty when visor-wm isn't running.
//
//   Repeater {
//       model: KeyBindings
//       delegate: Text {
//           required property string label        // "Win + Enter"
//           required property string description  // "Terminal" (bindd), or the dispatcher
//           required property int group           // bindings separated by a blank line
//           text: label + "  " + description
//       }
//   }
//
// Roles: keys ("SUPER+Return", as in the config), label, description,
// dispatcher, argument, group.
class KeyBindings : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int groupCount READ groupCount NOTIFY changed)

public:
    enum Role {
        KeysRole = Qt::UserRole + 1,
        LabelRole,
        DescriptionRole,
        DispatcherRole,
        ArgumentRole,
        GroupRole,
    };

    explicit KeyBindings(QObject *parent = nullptr);

    bool available() const { return !m_bindings.isEmpty(); }
    int count() const { return int(m_bindings.size()); }
    int groupCount() const { return m_groups.size(); }
    // The bindings of the n-th group (0-based), as {label, description}
    // objects, for laying groups out side by side.
    Q_INVOKABLE QVariantList group(int n) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // "SUPER SHIFT+left" -> "Win + Shift + ←", the way Windows names keys.
    static QString label(const QString &keys);

signals:
    void changed();

private:
    void reset();

    QList<ShellLink::Binding> m_bindings;
    QList<int> m_groups; // the distinct group numbers, in order
};
