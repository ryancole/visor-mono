#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>

// Toast notifications, in replace mode: what Windows' Notification Center
// would show, read through the UserNotificationListener API. The
// notification platform keeps running without Explorer and records every
// toast; only the UI is gone. Under Explorer the model stays empty
// (`available` false), since Windows shows them itself.
//
// A new notification is announced with `arrived`, for a pop-up; the model is
// the history, newest first, for a Notification Center of your own.
// dismiss() and clearAll() remove notifications from the platform, as
// clearing Notification Center does. `unread` counts those that arrived
// since markRead(), across restarts.
//
//   Connections { target: Notifications; function onArrived(n) { toast.show(n.app, n.title, n.body) } }
//   ListView { model: Notifications; delegate: Text { required property string title; text: title } }
//
// Roles: notificationId, app (display name), appId (AUMID), icon (an
// image:// URL, or ""), title, body, time, unread.
class Notifications : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // True in replace mode once the platform has allowed access.
    Q_PROPERTY(bool available READ available NOTIFY stateChanged)
    // "allowed", "denied" or "unspecified" (Settings > Privacy & security >
    // Notifications decides, per app), "unavailable" when the API is missing,
    // or "" under Explorer.
    Q_PROPERTY(QString access READ access NOTIFY stateChanged)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int unread READ unread NOTIFY changed)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        AppRole,
        AppIdRole,
        IconRole,
        TitleRole,
        BodyRole,
        TimeRole,
        UnreadRole,
    };

    struct Item
    {
        quint32 id = 0; // the platform's, as the listener numbers them
        QString app;
        QString appId;
        QString title;
        QString body;
        QDateTime time;
    };

    explicit Notifications(QObject *parent = nullptr);
    ~Notifications() override;

    bool available() const { return m_access == QLatin1String("allowed"); }
    QString access() const { return m_access; }
    int count() const { return int(m_items.size()); }
    int unread() const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Removes one notification, or all of them, from the platform too.
    Q_INVOKABLE void dismiss(int notificationId);
    Q_INVOKABLE void clearAll();
    // Everything so far counts as seen (the bell's number goes to 0).
    Q_INVOKABLE void markRead();
    Q_INVOKABLE void refresh();

signals:
    void stateChanged();
    void changed();
    // A notification that arrived while running (not the backlog at start):
    // {id, app, appId, icon, title, body, time}.
    void arrived(const QVariantMap &notification);

private:
    struct Impl;
    friend struct NotificationsBridge;

    void updateMode();
    void start();
    void stop();
    void accessDecided(int status);
    void watchPlatform();
    void applyList(QList<Item> items);
    QString iconFor(const QString &appId) const;
    QVariantMap toMap(const Item &item) const;

    std::unique_ptr<Impl> d;
    QList<Item> m_items;
    QString m_access;
    quint32 m_lastRead = 0;   // markRead()'s high-water mark, saved
    quint32 m_highest = 0;    // highest id seen by this instance
    bool m_primed = false;    // the first list has been read (it's the backlog)
    int m_attempts = 0;       // at creating the listener
    QTimer m_refreshTimer;
};
