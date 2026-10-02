#include "services/workspaces.h"

#include <QJsonObject>

Workspaces::Workspaces(QObject *parent)
    : QAbstractListModel(parent)
{
    ShellLink *link = ShellLink::instance();
    if (!link)
        return;
    // A handful of rows that all change together on a switch: a reset is
    // simplest and cheap.
    reset();
    connect(link, &ShellLink::workspacesChanged, this, &Workspaces::reset);
}

void Workspaces::reset()
{
    beginResetModel();
    const ShellLink *link = ShellLink::instance();
    m_workspaces = link->workspaces();
    m_active = link->activeWorkspace();
    endResetModel();
    emit changed();
}

int Workspaces::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_workspaces.size());
}

QVariant Workspaces::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_workspaces.size())
        return {};
    const ShellLink::Workspace &w = m_workspaces[index.row()];
    switch (role) {
    case NameRole:
        return w.name;
    case ActiveRole:
        return index.row() == m_active;
    case WindowsRole:
        return w.windows;
    default:
        return {};
    }
}

QHash<int, QByteArray> Workspaces::roleNames() const
{
    return {
        {NameRole, "name"},
        {ActiveRole, "active"},
        {WindowsRole, "windows"},
    };
}

void Workspaces::activate(int index)
{
    ShellLink *link = ShellLink::instance();
    if (!link || index < 0 || index >= m_workspaces.size() || index == m_active)
        return;
    // visor-wm focuses a window on the new desktop; a click on the bar
    // doesn't give it the right to, so pass ours on.
    link->grantForeground(link->wmPid());
    link->send({{"type", "workspace.activate"}, {"index", index}});
}
