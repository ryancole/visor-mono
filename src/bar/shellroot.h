#pragma once

#include <QObject>
#include <QQmlListProperty>
#include <QtQml/qqmlregistration.h>

// Non-visual root object for a config. It only exists to hold children, so a
// config can declare several windows, Instantiators, Connections, etc.
//
//   ShellRoot {
//       PanelWindow { ... }
//       PanelWindow { edge: PanelWindow.Bottom; ... }
//   }
class ShellRoot : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQmlListProperty<QObject> data READ data CONSTANT)
    Q_CLASSINFO("DefaultProperty", "data")

public:
    using QObject::QObject;

    QQmlListProperty<QObject> data()
    {
        return QQmlListProperty<QObject>(this, &m_data);
    }

private:
    QList<QObject *> m_data;
};
