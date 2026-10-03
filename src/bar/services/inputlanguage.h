#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <memory>

// The keyboard layout of the window you're typing in, as the taskbar's input
// indicator shows it ("ENG"), and how many layouts are installed. Windows
// shows the indicator only when there is more than one, so a bar should
// show it for `count > 1`. Follows the focused window (each has its own
// layout) and the language change Windows reports when it switches.
// next() switches the focused window's layout, as Win+Space does.
//
//   Label { visible: InputLanguage.count > 1; text: InputLanguage.code }
class InputLanguage : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int count READ count NOTIFY changed)
    // The language's three-letter code, upper case: "ENG", "DEU".
    Q_PROPERTY(QString code READ code NOTIFY changed)
    // Its display name: "English (United States)".
    Q_PROPERTY(QString name READ name NOTIFY changed)

public:
    explicit InputLanguage(QObject *parent = nullptr);
    ~InputLanguage() override;

    int count() const { return m_count; }
    QString code() const { return m_code; }
    QString name() const { return m_name; }

    Q_INVOKABLE void next();
    Q_INVOKABLE void refresh();

signals:
    void changed();

private:
    struct Impl;

    std::unique_ptr<Impl> d;
    int m_count = 0;
    QString m_code;
    QString m_name;
};
