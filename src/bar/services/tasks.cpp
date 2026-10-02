#include "services/tasks.h"

#include <QFileInfo>

#include <windows.h>

Tasks::Tasks(QObject *parent)
    : QAbstractListModel(parent)
{
    ShellLink *link = ShellLink::instance();
    if (!link)
        return;

    // The model keeps its own copy so rows change only between begin/end
    // notifications, as views expect.
    reset();
    connect(link, &ShellLink::connectedChanged, this, &Tasks::availableChanged);
    connect(link, &ShellLink::tasksReset, this, &Tasks::reset);
    connect(link, &ShellLink::taskAdded, this, [this](const ShellLink::Task &task) {
        const int row = int(m_tasks.size());
        beginInsertRows({}, row, row);
        m_tasks.append(task);
        endInsertRows();
        emit countChanged();
    });
    connect(link, &ShellLink::taskChanged, this, [this](const ShellLink::Task &task) {
        const int row = indexOf(task.hwnd);
        if (row < 0)
            return;
        m_tasks[row] = task;
        emit dataChanged(index(row), index(row));
    });
    connect(link, &ShellLink::taskRemoved, this, [this](quintptr hwnd) {
        const int row = indexOf(hwnd);
        if (row < 0)
            return;
        beginRemoveRows({}, row, row);
        m_tasks.removeAt(row);
        endRemoveRows();
        emit countChanged();
    });
    connect(link, &ShellLink::activeTaskChanged, this, [this] {
        const int before = indexOf(m_active);
        m_active = ShellLink::instance()->activeTask();
        const int after = indexOf(m_active);
        for (int row : {before, after}) {
            if (row >= 0)
                emit dataChanged(index(row), index(row), {ActiveRole});
        }
    });
}

bool Tasks::available() const
{
    return ShellLink::instance() && ShellLink::instance()->connected();
}

void Tasks::reset()
{
    beginResetModel();
    m_tasks = ShellLink::instance()->tasks();
    m_active = ShellLink::instance()->activeTask();
    endResetModel();
    emit countChanged();
}

int Tasks::indexOf(quintptr hwnd) const
{
    for (qsizetype i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks[i].hwnd == hwnd)
            return int(i);
    }
    return -1;
}

int Tasks::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_tasks.size());
}

QVariant Tasks::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_tasks.size())
        return {};
    const ShellLink::Task &t = m_tasks[index.row()];
    switch (role) {
    case HwndRole:
        return double(t.hwnd);
    case Qt::DisplayRole:
    case TitleRole:
        return t.title;
    case AppNameRole:
        return QFileInfo(t.path).completeBaseName();
    case ProcessPathRole:
        return t.path;
    case ActiveRole:
        return t.hwnd == m_active;
    case FlashingRole:
        return t.flashing;
    case IconRole:
        return QStringLiteral("image://visor-window-icon/%1").arg(t.hwnd);
    default:
        return {};
    }
}

QHash<int, QByteArray> Tasks::roleNames() const
{
    return {
        {HwndRole, "hwnd"},
        {TitleRole, "title"},
        {AppNameRole, "appName"},
        {ProcessPathRole, "processPath"},
        {ActiveRole, "active"},
        {FlashingRole, "flashing"},
        {IconRole, "icon"},
    };
}

void Tasks::activate(double hwnd)
{
    const auto w = reinterpret_cast<HWND>(quintptr(hwnd));
    if (!IsWindow(w))
        return;
    // The bar never takes focus, so the foreground window is still whatever
    // the user was in when they clicked.
    if (w == GetForegroundWindow() && !IsIconic(w)) {
        ShowWindowAsync(w, SW_MINIMIZE);
        return;
    }
    if (IsIconic(w))
        ShowWindowAsync(w, SW_RESTORE);
    // Clicks on the bar don't give us foreground rights by themselves.
    if (ShellLink *link = ShellLink::instance())
        link->grantForeground(0);
    SetForegroundWindow(w);
}

QVariantList Tasks::zOrder() const
{
    QVariantList list;
    for (HWND w = GetTopWindow(nullptr); w; w = GetWindow(w, GW_HWNDNEXT)) {
        const int i = indexOf(quintptr(w));
        if (i < 0)
            continue;
        const QModelIndex row = index(i);
        list.append(QVariantMap{{QStringLiteral("hwnd"), double(quintptr(w))},
                                {QStringLiteral("title"), data(row, TitleRole)},
                                {QStringLiteral("appName"), data(row, AppNameRole)},
                                {QStringLiteral("icon"), data(row, IconRole)}});
    }
    return list;
}

void Tasks::bringToFront(double hwnd)
{
    const auto w = reinterpret_cast<HWND>(quintptr(hwnd));
    if (!IsWindow(w))
        return;
    if (IsIconic(w))
        ShowWindowAsync(w, SW_RESTORE);
    if (ShellLink *link = ShellLink::instance())
        link->grantForeground(0);
    SetForegroundWindow(w);
}

void Tasks::minimize(double hwnd)
{
    const auto w = reinterpret_cast<HWND>(quintptr(hwnd));
    if (IsWindow(w))
        ShowWindowAsync(w, SW_MINIMIZE);
}

void Tasks::close(double hwnd)
{
    const auto w = reinterpret_cast<HWND>(quintptr(hwnd));
    if (IsWindow(w))
        PostMessageW(w, WM_CLOSE, 0, 0);
}
