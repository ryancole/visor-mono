#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

// The window that currently has keyboard focus, tracked with WinEvent hooks
// (EVENT_SYSTEM_FOREGROUND and title changes). No polling.
//
//   Text { text: ActiveWindow.title }
class ActiveWindow : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    // Executable name without extension, e.g. "firefox".
    Q_PROPERTY(QString appName READ appName NOTIFY processChanged)
    Q_PROPERTY(QString processPath READ processPath NOTIFY processChanged)
    Q_PROPERTY(int processId READ processId NOTIFY processChanged)
    Q_PROPERTY(QString className READ className NOTIFY processChanged)

public:
    explicit ActiveWindow(QObject *parent = nullptr);
    ~ActiveWindow() override;

    QString title() const { return m_title; }
    QString appName() const { return m_appName; }
    QString processPath() const { return m_processPath; }
    int processId() const { return int(m_pid); }
    QString className() const { return m_className; }

    // Called from the WinEvent hook procedure.
    void onForegroundChanged(void *hwnd);
    void onNameChanged(void *hwnd);

signals:
    void titleChanged();
    void processChanged();

private:
    void refreshTitle();

    void *m_hwnd = nullptr;
    void *m_foregroundHook = nullptr;
    void *m_nameHook = nullptr;
    void *m_minimizeHook = nullptr;
    unsigned long m_pid = 0;
    QString m_title;
    QString m_appName;
    QString m_processPath;
    QString m_className;
};
