#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

// The session, as seen from QML: what visor runs under, the commands
// visor-wm's `visor` key bindings send, launching, and the power actions
// (what Windows' Win+X "Shut down or sign out" menu does).
//
//   Connections {
//       target: Shell
//       function onCommand(name) { if (name === "launcher") launcher.toggle() }
//   }
//   MouseArea { onClicked: Shell.lock() }
class ShellControl : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Shell)
    QML_SINGLETON

    // True while connected to visor-shell.
    Q_PROPERTY(bool available READ available NOTIFY changed)
    // "replace" (visor-shell is the Windows shell), "hosted" (alongside
    // Explorer), or "" (no visor-shell: a plain app under Explorer).
    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    // True in replace mode: no Explorer, so UWP apps (Settings, Store apps)
    // can't open a window.
    Q_PROPERTY(bool replacingExplorer READ replacingExplorer NOTIFY changed)

public:
    explicit ShellControl(QObject *parent = nullptr);

    bool available() const;
    QString mode() const;
    bool replacingExplorer() const { return mode() == QLatin1String("replace"); }

    // Runs a command line (`wt.exe -d C:\`, a document, a URL), on a worker
    // thread; nothing blocks if it hangs.
    Q_INVOKABLE void run(const QString &commandLine);
    // Shell32's own Run dialog (Win+R).
    Q_INVOKABLE void showRunDialog();

    Q_INVOKABLE void lock();
    Q_INVOKABLE void signOut();
    Q_INVOKABLE void sleep();
    Q_INVOKABLE void restart();
    Q_INVOKABLE void shutDown();
    // Asks visor-shell to hand the session to Explorer (Ctrl+Alt+Q).
    Q_INVOKABLE void quitToExplorer();

signals:
    void changed();
    // A `visor <name>` key binding was pressed in visor-wm.
    void command(const QString &name);
};
