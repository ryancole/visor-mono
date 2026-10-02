#pragma once

#include <QQuickWindow>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

// An overlay that never takes focus: the volume display, toast pop-ups.
// Frameless, always on top, no taskbar button, and WS_EX_NOACTIVATE like the
// bars, so it can appear over whatever you're typing in without costing a
// keystroke. PopupWindow is the opposite: it takes focus and closes when it
// loses it. The window is transparent by default, so the config draws the
// shape itself and can fade it.
//
// open() places it on the focused window's monitor (BottomRight: the primary
// one, where Windows shows toasts) and shows it. With a `timeout`, `shown`
// drops back to false that many ms later (open() again restarts the clock),
// which a config can fade out on before calling close():
//
//   OsdWindow {
//       id: osd
//       placement: OsdWindow.Bottom
//       timeout: 2000
//       width: 300; height: 60
//       Rectangle {
//           anchors.fill: parent; radius: 8; color: Theme.popupBackground
//           opacity: osd.shown ? 1 : 0
//           Behavior on opacity { NumberAnimation { duration: 150 } }
//       }
//       onShownChanged: if (!shown) closeLater.restart()
//       Timer { id: closeLater; interval: 150; onTriggered: if (!osd.shown) osd.close() }
//   }
//   ... osd.open()
class OsdWindow : public QQuickWindow
{
    Q_OBJECT
    QML_ELEMENT

    // Bottom is where Windows 11 puts its volume flyout (bottom centre,
    // above the taskbar) and BottomRight its toasts; Top is under the bar.
    Q_PROPERTY(Placement placement READ placement WRITE setPlacement NOTIFY placementChanged)
    // Gap from the work area's edges.
    Q_PROPERTY(int margin READ margin WRITE setMargin NOTIFY marginChanged)
    // Clicks go through to whatever is underneath: an indicator, not a
    // control.
    Q_PROPERTY(bool clickThrough READ clickThrough WRITE setClickThrough NOTIFY clickThroughChanged)
    // ms from open() until `shown` turns false; 0 keeps it until close().
    Q_PROPERTY(int timeout READ timeout WRITE setTimeout NOTIFY timeoutChanged)
    Q_PROPERTY(bool shown READ shown NOTIFY shownChanged)

public:
    enum Placement { Bottom, BottomRight, Center, Top };
    Q_ENUM(Placement)

    explicit OsdWindow(QWindow *parent = nullptr);

    Placement placement() const { return m_placement; }
    void setPlacement(Placement placement);
    int margin() const { return m_margin; }
    void setMargin(int margin);
    bool clickThrough() const { return m_clickThrough; }
    void setClickThrough(bool through);
    int timeout() const { return m_timeout; }
    void setTimeout(int ms);
    bool shown() const { return m_shown; }

    // `screen`: as PanelWindow's screen property (null: the focused
    // window's monitor, or the primary one for BottomRight).
    Q_INVOKABLE void open(QObject *screen = nullptr);
    Q_INVOKABLE void close();

signals:
    void placementChanged();
    void marginChanged();
    void clickThroughChanged();
    void timeoutChanged();
    void shownChanged();

private:
    void place();
    void setShown(bool shown);

    Placement m_placement = Bottom;
    int m_margin = 24;
    bool m_clickThrough = false;
    int m_timeout = 0;
    bool m_shown = false;
    QTimer m_timer;
};
