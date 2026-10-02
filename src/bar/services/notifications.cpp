// C++/WinRT first: Qt's keyword macros (signals, slots, emit) must not leak
// into the WinRT projection headers.
#include <unknwn.h>
#include <winrt/base.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/Windows.UI.Notifications.Management.h>

#include <winevt.h>

#include "services/notifications.h"

#include "services/appindex.h"
#include "services/shelllink.h"

#include <QDebug>
#include <QMetaObject>
#include <QSettings>

#include <algorithm>
#include <mutex>

namespace wun = winrt::Windows::UI::Notifications;
namespace wunm = winrt::Windows::UI::Notifications::Management;
using winrt::Windows::Foundation::AsyncStatus;

namespace {

QString toQString(const winrt::hstring &s)
{
    return QString::fromWCharArray(s.c_str(), int(s.size()));
}

QSettings store()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("visor"), QStringLiteral("notifications"));
}

// What the listener exposes of a toast, from any thread. The text comes from
// the generic binding, which the platform fills in for the old templates
// too; the first line is the title.
Notifications::Item itemFrom(const wun::UserNotification &n)
{
    Notifications::Item item;
    item.id = n.Id();
    try {
        const auto info = n.AppInfo();
        item.app = toQString(info.DisplayInfo().DisplayName());
        item.appId = toQString(info.AppUserModelId());
    } catch (const winrt::hresult_error &) {
    }
    try {
        if (const auto binding = n.Notification().Visual().GetBinding(wun::KnownNotificationBindings::ToastGeneric())) {
            QStringList texts;
            for (const auto &element : binding.GetTextElements()) {
                const QString text = toQString(element.Text()).trimmed();
                if (!text.isEmpty())
                    texts.append(text);
            }
            if (!texts.isEmpty()) {
                item.title = texts.takeFirst();
                item.body = texts.join(QLatin1Char('\n'));
            }
        }
    } catch (const winrt::hresult_error &) {
    }
    item.time = QDateTime::fromSecsSinceEpoch(winrt::clock::to_time_t(n.CreationTime()));
    return item;
}

} // namespace

// WinRT completions and event-log callbacks arrive on other threads. They
// hold a shared Bridge and post work to the GUI thread through it;
// Notifications clears it on destruction so late callbacks become no-ops.
struct NotificationsBridge
{
    std::mutex mutex;
    Notifications *target = nullptr;

    template<typename F>
    void post(F fn)
    {
        std::scoped_lock lock(mutex);
        if (target)
            QMetaObject::invokeMethod(target, [t = target, fn] { fn(t); }, Qt::QueuedConnection);
    }
};

namespace {

// The change signal. The listener's own NotificationChanged event needs
// package identity (it fails with "element not found" for a plain exe), and
// the platform's database on disk changes without any file notification
// (the service keeps it open; NTFS updates the directory lazily). What does
// work: the platform logs every delivery and clearing to its operational
// event log channel, which the Event Log API pushes to subscribers.
constexpr wchar_t kPlatformChannel[] = L"Microsoft-Windows-PushNotification-Platform/Operational";
// 3153: a toast was delivered; 3055: toasts were cleared.
constexpr wchar_t kPlatformQuery[] = L"*[System[(EventID=3153 or EventID=3055)]]";

DWORD WINAPI onPlatformEvent(EVT_SUBSCRIBE_NOTIFY_ACTION action, PVOID context, EVT_HANDLE)
{
    if (action == EvtSubscribeActionDeliver)
        static_cast<NotificationsBridge *>(context)->post([](Notifications *n) { n->refresh(); });
    return 0;
}

} // namespace

struct Notifications::Impl
{
    std::shared_ptr<NotificationsBridge> bridge = std::make_shared<NotificationsBridge>();
    wunm::UserNotificationListener listener{nullptr};
    wunm::UserNotificationListener::NotificationChanged_revoker changedToken;
    EVT_HANDLE subscription = nullptr;
    bool started = false;

    void unsubscribe()
    {
        if (subscription)
            EvtClose(subscription); // no callbacks after this returns
        subscription = nullptr;
    }
};

Notifications::Notifications(QObject *parent)
    : QAbstractListModel(parent)
    , d(std::make_unique<Impl>())
{
    d->bridge->target = this;
    m_lastRead = store().value(QStringLiteral("lastRead")).toUInt();

    // A delivery logs several events; one read after the last.
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(150);
    connect(&m_refreshTimer, &QTimer::timeout, this, &Notifications::refresh);

    // Icons come from the app index, which is built a little after start.
    if (AppIndex *apps = AppIndex::instance()) {
        connect(apps, &AppIndex::changed, this, [this] {
            if (!m_items.isEmpty())
                emit dataChanged(index(0), index(int(m_items.size()) - 1), {IconRole});
        });
    }
    if (ShellLink *link = ShellLink::instance()) {
        // Queued: the shell's mode arrives inside a cross-process SendMessage
        // (WM_COPYDATA), where an outgoing COM call such as the listener's is
        // refused with E_UNEXPECTED.
        connect(link, &ShellLink::connectedChanged, this, &Notifications::updateMode, Qt::QueuedConnection);
        updateMode();
    }
}

Notifications::~Notifications()
{
    d->unsubscribe();
    {
        std::scoped_lock lock(d->bridge->mutex);
        d->bridge->target = nullptr;
    }
    d->changedToken.revoke();
}

// Only without Explorer; with it, Windows shows toasts and has the bell.
void Notifications::updateMode()
{
    ShellLink *link = ShellLink::instance();
    if (link && link->mode() == QLatin1String("replace"))
        start();
    else
        stop();
}

void Notifications::start()
{
    if (d->started)
        return;
    d->started = true;
    try {
        d->listener = wunm::UserNotificationListener::Current();
    } catch (const winrt::hresult_error &e) {
        // Seen right after sign-in, before the platform is ready: try again.
        if (++m_attempts < 5) {
            qInfo("notifications: listener not ready (%ls); retrying", e.message().c_str());
            QTimer::singleShot(2000, this, [this] {
                d->started = false;
                start();
            });
            return;
        }
        qWarning("Notifications: listener unavailable: %ls", e.message().c_str());
        m_access = QStringLiteral("unavailable");
        emit stateChanged();
        return;
    }
    m_access = QStringLiteral("unspecified");
    emit stateChanged();
    try {
        d->listener.RequestAccessAsync().Completed([bridge = d->bridge](const auto &op, AsyncStatus status) {
            if (status != AsyncStatus::Completed)
                return;
            const int result = int(op.GetResults());
            bridge->post([result](Notifications *n) { n->accessDecided(result); });
        });
    } catch (const winrt::hresult_error &e) {
        qWarning("Notifications: access request failed: %ls", e.message().c_str());
    }
}

void Notifications::stop()
{
    if (!d->started)
        return;
    d->started = false;
    d->unsubscribe();
    d->changedToken.revoke();
    d->listener = nullptr;
    m_refreshTimer.stop();
    beginResetModel();
    m_items.clear();
    endResetModel();
    m_access.clear();
    m_primed = false;
    m_highest = 0;
    emit stateChanged();
    emit changed();
}

void Notifications::accessDecided(int status)
{
    using wunm::UserNotificationListenerAccessStatus;
    switch (UserNotificationListenerAccessStatus(status)) {
    case UserNotificationListenerAccessStatus::Allowed: m_access = QStringLiteral("allowed"); break;
    case UserNotificationListenerAccessStatus::Denied: m_access = QStringLiteral("denied"); break;
    default: m_access = QStringLiteral("unspecified"); break;
    }
    qInfo().noquote() << "notifications: access" << m_access;
    emit stateChanged();
    if (!available())
        return;
    watchPlatform();
    refresh();
}

void Notifications::watchPlatform()
{
    // The listener's own event, should Visor ever have package identity.
    try {
        d->changedToken = d->listener.NotificationChanged(
            winrt::auto_revoke, [bridge = d->bridge](const auto &, const auto &) {
                bridge->post([](Notifications *n) { n->m_refreshTimer.start(); });
            });
    } catch (const winrt::hresult_error &) {
    }
    d->unsubscribe();
    d->subscription = EvtSubscribe(nullptr, nullptr, kPlatformChannel, kPlatformQuery, nullptr, d->bridge.get(),
                                   onPlatformEvent, EvtSubscribeToFutureEvents);
    if (!d->subscription)
        qWarning() << "Notifications: cannot watch the platform's event log, error" << GetLastError();
}

void Notifications::refresh()
{
    if (!d->listener || !available())
        return;
    try {
        d->listener.GetNotificationsAsync(wun::NotificationKinds::Toast)
            .Completed([bridge = d->bridge](const auto &op, AsyncStatus status) {
                if (status != AsyncStatus::Completed)
                    return;
                QList<Item> items;
                try {
                    for (const auto &n : op.GetResults())
                        items.append(itemFrom(n));
                } catch (const winrt::hresult_error &e) {
                    qWarning("Notifications: reading failed: %ls", e.message().c_str());
                    return;
                }
                bridge->post([items](Notifications *n) { n->applyList(items); });
            });
    } catch (const winrt::hresult_error &e) {
        qWarning("Notifications: reading failed: %ls", e.message().c_str());
    }
}

void Notifications::applyList(QList<Item> items)
{
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) { return a.id > b.id; });
    // What's new since the last read, oldest first, for the pop-up. The
    // first read is the backlog: history, not pop-ups (Windows doesn't
    // re-show toasts either).
    QList<Item> fresh;
    quint32 highest = m_highest;
    for (const Item &item : std::as_const(items)) {
        if (item.id > m_highest)
            fresh.prepend(item);
        highest = std::max(highest, item.id);
    }
    beginResetModel();
    m_items = items;
    endResetModel();
    emit changed();
    if (m_primed) {
        for (const Item &item : std::as_const(fresh))
            emit arrived(toMap(item));
    }
    m_highest = highest;
    m_primed = true;
}

int Notifications::unread() const
{
    int n = 0;
    for (const Item &item : m_items) {
        if (item.id > m_lastRead)
            ++n;
    }
    return n;
}

int Notifications::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QVariant Notifications::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const Item &item = m_items[index.row()];
    switch (role) {
    case IdRole: return int(item.id);
    case AppRole: return item.app;
    case AppIdRole: return item.appId;
    case IconRole: return iconFor(item.appId);
    case TitleRole: return item.title;
    case BodyRole: return item.body;
    case TimeRole: return item.time;
    case UnreadRole: return item.id > m_lastRead;
    default: return {};
    }
}

QHash<int, QByteArray> Notifications::roleNames() const
{
    return {
        {IdRole, "notificationId"}, {AppRole, "app"},   {AppIdRole, "appId"}, {IconRole, "icon"},
        {TitleRole, "title"},       {BodyRole, "body"}, {TimeRole, "time"},   {UnreadRole, "unread"},
    };
}

// The app's icon from the launcher's index (the AUMID is the key there too).
QString Notifications::iconFor(const QString &appId) const
{
    const AppIndex *apps = AppIndex::instance();
    if (!apps || appId.isEmpty())
        return {};
    for (const AppIndex::App &app : apps->apps()) {
        if (app.id.compare(appId, Qt::CaseInsensitive) == 0)
            return QStringLiteral("image://visor-app-icon/%1").arg(app.key);
    }
    return {};
}

QVariantMap Notifications::toMap(const Item &item) const
{
    return {
        {QStringLiteral("id"), int(item.id)},     {QStringLiteral("app"), item.app},
        {QStringLiteral("appId"), item.appId},    {QStringLiteral("icon"), iconFor(item.appId)},
        {QStringLiteral("title"), item.title},    {QStringLiteral("body"), item.body},
        {QStringLiteral("time"), item.time},
    };
}

void Notifications::dismiss(int notificationId)
{
    if (d->listener) {
        try {
            d->listener.RemoveNotification(quint32(notificationId));
        } catch (const winrt::hresult_error &e) {
            qWarning("Notifications: dismiss failed: %ls", e.message().c_str());
        }
    }
    for (qsizetype i = 0; i < m_items.size(); ++i) {
        if (int(m_items[i].id) == notificationId) {
            beginRemoveRows({}, int(i), int(i));
            m_items.removeAt(i);
            endRemoveRows();
            emit changed();
            break;
        }
    }
}

void Notifications::clearAll()
{
    if (d->listener) {
        try {
            d->listener.ClearNotifications();
        } catch (const winrt::hresult_error &e) {
            qWarning("Notifications: clear failed: %ls", e.message().c_str());
        }
    }
    if (m_items.isEmpty())
        return;
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit changed();
}

void Notifications::markRead()
{
    const quint32 latest = m_items.isEmpty() ? m_lastRead : std::max(m_lastRead, m_items.first().id);
    if (latest == m_lastRead)
        return;
    m_lastRead = latest;
    store().setValue(QStringLiteral("lastRead"), m_lastRead);
    if (!m_items.isEmpty())
        emit dataChanged(index(0), index(int(m_items.size()) - 1), {UnreadRole});
    emit changed();
}
