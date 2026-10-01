#include "services/systemtray.h"

#include <windows.h>

SystemTray::SystemTray(QObject *parent)
    : QAbstractListModel(parent)
{
    ShellLink *link = ShellLink::instance();
    if (!link)
        return;
    reset();
    connect(link, &ShellLink::connectedChanged, this, &SystemTray::availableChanged);
    connect(link, &ShellLink::trayReset, this, &SystemTray::reset);
    connect(link, &ShellLink::trayIconAdded, this, &SystemTray::update);
    connect(link, &ShellLink::trayIconChanged, this, &SystemTray::update);
    connect(link, &ShellLink::trayIconRemoved, this, &SystemTray::remove);
}

bool SystemTray::available() const
{
    return ShellLink::instance() && ShellLink::instance()->connected();
}

void SystemTray::reset()
{
    beginResetModel();
    m_icons.clear();
    for (const ShellLink::TrayIcon &icon : ShellLink::instance()->trayIcons()) {
        if (!icon.hidden)
            m_icons.append(icon);
    }
    endResetModel();
    emit countChanged();
}

int SystemTray::indexOf(int id) const
{
    for (qsizetype i = 0; i < m_icons.size(); ++i) {
        if (m_icons[i].id == id)
            return int(i);
    }
    return -1;
}

// Adds, updates, or (when the app hid it) removes a row.
void SystemTray::update(const ShellLink::TrayIcon &icon)
{
    const int row = indexOf(icon.id);
    if (icon.hidden) {
        if (row >= 0)
            remove(icon.id);
        return;
    }
    if (row >= 0) {
        m_icons[row] = icon;
        emit dataChanged(index(row), index(row));
        return;
    }
    const int end = int(m_icons.size());
    beginInsertRows({}, end, end);
    m_icons.append(icon);
    endInsertRows();
    emit countChanged();
}

void SystemTray::remove(int id)
{
    const int row = indexOf(id);
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    m_icons.removeAt(row);
    endRemoveRows();
    emit countChanged();
}

int SystemTray::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_icons.size());
}

QVariant SystemTray::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_icons.size())
        return {};
    const ShellLink::TrayIcon &t = m_icons[index.row()];
    switch (role) {
    case IdRole:
        return t.id;
    case Qt::DisplayRole:
    case TooltipRole:
        return t.tip;
    case IconRole:
        // A new HICON comes with every image change, so the URL changes too.
        return t.icon ? QStringLiteral("image://visor-tray-icon/%1").arg(t.icon) : QString();
    case ProcessIdRole:
        return t.pid;
    default:
        return {};
    }
}

QHash<int, QByteArray> SystemTray::roleNames() const
{
    return {
        {IdRole, "iconId"},
        {TooltipRole, "tooltip"},
        {IconRole, "icon"},
        {ProcessIdRole, "processId"},
    };
}

void SystemTray::click(int iconId, const QString &button)
{
    const int row = indexOf(iconId);
    if (row < 0 || !ShellLink::instance())
        return;
    // The app needs the foreground for its menu, or the menu won't close on
    // Escape or a click elsewhere.
    ShellLink::instance()->grantForeground(m_icons[row].pid);
    POINT pt{};
    GetCursorPos(&pt); // physical pixels, as apps expect
    ShellLink::instance()->send({{"type", "tray.click"},
                                 {"id", iconId},
                                 {"button", button},
                                 {"x", int(pt.x)},
                                 {"y", int(pt.y)}});
}
