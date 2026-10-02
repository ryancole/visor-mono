#pragma once

#include <QAbstractListModel>
#include <QQuickImageProvider>
#include <QtQml/qqmlregistration.h>

// The launcher's results: the installed apps (see AppIndex) that match
// `query`, best first. With an empty query it's the recently opened apps,
// then every app A-Z, like Start's "All" list.
//
//   ListView {
//       model: Apps
//       delegate: Row {
//           required property int index
//           required property string name
//           required property url icon
//           required property bool launchable
//           Image { source: icon }
//           Text { text: name; opacity: launchable ? 1 : 0.5 }
//           MouseArea { onClicked: Apps.launch(index) }
//       }
//   }
//   TextInput { onTextChanged: Apps.query = text }
//
// Roles: key, name, id, icon (an image:// URL), packaged, launchable (false
// for UWP apps in replace mode, which can't open a window without
// Explorer), recent (opened before, and shown first for an empty query).
class Apps : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // False until the index has been built the first time.
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)

public:
    enum Role {
        KeyRole = Qt::UserRole + 1,
        NameRole,
        IdRole,
        IconRole,
        PackagedRole,
        LaunchableRole,
        RecentRole,
    };

    explicit Apps(QObject *parent = nullptr);

    QString query() const { return m_query; }
    void setQuery(const QString &query);
    int count() const { return int(m_rows.size()); }
    bool ready() const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Opens the app in row `row`. asAdmin: Ctrl+Shift+Enter in Start.
    Q_INVOKABLE void launch(int row, bool asAdmin = false);
    // Rebuilds the index (it also rebuilds itself when apps come and go).
    Q_INVOKABLE void refresh();

    // Score of `query` against `name`, 0 if it doesn't match: every character
    // of the query in order, favouring the start of the name and of words,
    // and runs of consecutive characters.
    static int fuzzyScore(const QString &query, const QString &name);

signals:
    void queryChanged();
    void countChanged();
    void readyChanged();

private:
    void rebuild();

    QString m_query;
    QList<int> m_rows;      // indices into AppIndex::apps()
    QList<int> m_recentRows;
};

// image://visor-app-icon/<key>: an app's icon from the shell (the shortcut's
// icon, or a packaged app's logo).
class AppIconProvider : public QQuickImageProvider
{
public:
    AppIconProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
