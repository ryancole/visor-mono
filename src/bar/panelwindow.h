#pragma once

#include <QPointer>
#include <QQmlParserStatus>
#include <QQuickWindow>
#include <QtQml/qqmlregistration.h>

class QScreen;

// A window docked to one edge of a monitor. By default it registers as a
// Windows "app bar", so the OS shrinks the work area and maximized windows
// don't cover it (the same mechanism the taskbar uses).
//
//   PanelWindow {
//       edge: PanelWindow.Top
//       thickness: 32
//       screen: Qt.application.screens[1]   // optional, defaults to primary
//       Text { anchors.centerIn: parent; text: "hello" }
//   }
class PanelWindow : public QQuickWindow, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT

    Q_PROPERTY(Edge edge READ edge WRITE setEdge NOTIFY edgeChanged)
    // Size perpendicular to the edge, in device-independent pixels.
    Q_PROPERTY(int thickness READ thickness WRITE setThickness NOTIFY thicknessChanged)
    // Reserve screen space so other windows don't overlap the panel.
    Q_PROPERTY(bool exclusive READ exclusive WRITE setExclusive NOTIFY exclusiveChanged)
    // Drop below other windows while a fullscreen app is on this monitor.
    Q_PROPERTY(bool hideOnFullscreen READ hideOnFullscreen WRITE setHideOnFullscreen NOTIFY
                   hideOnFullscreenChanged)
    // Any object with a `name` property matching a monitor: a QML screen from
    // Qt.application.screens, or a QScreen. Null means the primary monitor.
    Q_PROPERTY(QObject *screen READ screenObject WRITE setScreenObject NOTIFY screenObjectChanged)
    Q_PROPERTY(bool fullscreenAppActive READ fullscreenAppActive NOTIFY fullscreenAppActiveChanged)
    // Shadows QWindow::visible so panels are shown by default (a plain QML
    // Window starts hidden) and so the initial value is applied only once the
    // window is fully set up.
    Q_PROPERTY(bool visible READ panelVisible WRITE setPanelVisible NOTIFY panelVisibleChanged)

public:
    enum Edge { Top, Bottom, Left, Right };
    Q_ENUM(Edge)

    explicit PanelWindow(QWindow *parent = nullptr);
    ~PanelWindow() override;

    Edge edge() const { return m_edge; }
    void setEdge(Edge edge);
    int thickness() const { return m_thickness; }
    void setThickness(int thickness);
    bool exclusive() const { return m_exclusive; }
    void setExclusive(bool exclusive);
    bool hideOnFullscreen() const { return m_hideOnFullscreen; }
    void setHideOnFullscreen(bool hide);
    QObject *screenObject() const { return m_screenObject; }
    void setScreenObject(QObject *screen);
    bool fullscreenAppActive() const { return m_fullscreenApp; }
    bool panelVisible() const { return m_wantVisible; }
    void setPanelVisible(bool visible);

    void classBegin() override {}
    void componentComplete() override;

signals:
    void edgeChanged();
    void thicknessChanged();
    void exclusiveChanged();
    void hideOnFullscreenChanged();
    void screenObjectChanged();
    void fullscreenAppActiveChanged();
    void panelVisibleChanged();

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    QScreen *targetScreen() const;
    void scheduleLayout();
    void layout();
    void registerAppBar();
    void unregisterAppBar();
    void setFullscreenApp(bool active);
    void applyZOrder();

    Edge m_edge = Top;
    int m_thickness = 32;
    bool m_exclusive = true;
    bool m_hideOnFullscreen = true;
    bool m_fullscreenApp = false;
    bool m_wantVisible = true;
    bool m_complete = false;
    bool m_appBarRegistered = false;
    bool m_layoutPending = false;
    QRect m_reservedRect; // physical px, last rect passed to ABM_SETPOS
    QPointer<QObject> m_screenObject;
    QPointer<QScreen> m_trackedScreen;
};
