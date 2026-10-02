#pragma once

#include <QQuickWindow>
#include <QtQml/qqmlregistration.h>

// A pop-up that takes keyboard focus: the launcher, a menu, an overlay.
// Frameless, always on top, no taskbar button, rounded corners. Unlike
// PanelWindow it is activated when opened, so text fields in it get typing,
// and it closes again when focus goes elsewhere or on Escape.
//
// open() puts it on the monitor of the window that had focus (which gets
// focus back on close), or on `screen` if given (a bar passes its own when
// clicked): below the top of the work area, i.e. under the bar, centred or
// at the left, or in the middle of the screen.
//
//   PopupWindow {
//       id: launcher
//       width: 640; height: 400
//       color: Theme.background
//       TextInput { focus: true; ... }
//   }
//   ... launcher.toggle()
class PopupWindow : public QQuickWindow
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(Placement placement READ placement WRITE setPlacement NOTIFY placementChanged)
    // Gap between the work area's top edge and the window, for Below.
    Q_PROPERTY(int margin READ margin WRITE setMargin NOTIFY marginChanged)
    // Close when another window takes focus (default). Off for an overlay
    // that should stay while you click around.
    Q_PROPERTY(bool closeOnDeactivate READ closeOnDeactivate WRITE setCloseOnDeactivate NOTIFY
                   closeOnDeactivateChanged)

public:
    enum Placement { Below, BelowLeft, Center };
    Q_ENUM(Placement)

    explicit PopupWindow(QWindow *parent = nullptr);

    Placement placement() const { return m_placement; }
    void setPlacement(Placement placement);
    int margin() const { return m_margin; }
    void setMargin(int margin);
    bool closeOnDeactivate() const { return m_closeOnDeactivate; }
    void setCloseOnDeactivate(bool close);

    // `screen`: as PanelWindow's screen property (null: the focused
    // window's monitor).
    Q_INVOKABLE void open(QObject *screen = nullptr);
    Q_INVOKABLE void close();
    Q_INVOKABLE void toggle(QObject *screen = nullptr);

signals:
    void placementChanged();
    void marginChanged();
    void closeOnDeactivateChanged();
    void opened();
    void closed();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override;

private:
    void place();

    Placement m_placement = Below;
    int m_margin = 8;
    bool m_closeOnDeactivate = true;
    void *m_previous = nullptr; // the foreground window before open()
};
